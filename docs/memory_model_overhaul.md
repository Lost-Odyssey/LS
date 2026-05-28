# LS 内存模型整改 — 分阶段实施计划

> **目标**：系统性消除 codegen 层反复出现的 double-free / leak / UAF 类 bug，
> 将分散在 147+ 站点的手写 move/clone/free 逻辑收拢为可维护的统一抽象。
>
> **约束**：每个阶段结束时必须满足：
> 1. **CG_DEBUG 完备**：新增/修改的每个 alloc/free/clone/move/drop 站点有 `#if CG_DEBUG` 跟踪
> 2. **memcheck 零回归**：`ls run --memcheck` 全部现有 memcheck 测试 0 leak / 0 dfree / 0 ifree
> 3. **ctest 零回归**：`ctest --output-on-failure -C Release` 全部现有测试 PASS
> 4. **JIT + AOT 双通道**：每个新增 e2e 测试必须同时验证 `ls run` 和 `ls compile && exec`
> 5. **发现 bug 即加测试**：任何阶段中发现的 bug 必须先补 `.ls` 端到端测试 + memcheck 测试

---

## 问题概述

从 BF-001 到 BF-034，36 个 bugfix 中 28+ 是内存安全问题。反复出现三个结构性缺陷：

| # | 缺陷 | 典型 bug | 波及范围 |
|---|------|----------|----------|
| 1 | **双轨临时值跟踪**：`temp_string_slots` + `__argtmp` scope 注册并行，同一 string 双注册 → double-free | BF-008, BF-025, `print(s.upper())` | codegen_print_call, AST_CALL 通用路径, codegen_block_call |
| 2 | **`cap == 0` 语义重载**：Static `.rodata`（不需 free）与 `&string` borrowed 参数（不拥有堆内存但需 clone 获取所有权）共用 `cap == 0` | BF-032 | emit_string_clone_val, emit_enum_ctor, emit_struct_clone_val 所有消费站点 |
| 3 | **所有权转移站点遗漏**：每新增容器操作需在 5+ 个位置手写 move/clone/mark 分支，遗漏一处即 bug | BF-001/004/007/022/024, bug_23 (`v[i]=b`) | vec.push, vec.insert, map.set, `v[i]=`, `a[i]=`, enum ctor, struct ctor, match binder, 闭包捕获 |

---

## 阶段总览

| 阶段 | 名称 | 核心改动 | 预计工作量 | 风险 |
|------|------|----------|------------|------|
| M-1 | 消除双轨临时值跟踪 | 移除 `__argtmp`/`expr_produces_dynamic_string`，统一走 `temp_string_slots` | 小 | 低 |
| M-2 | 拆分 `cap == 0` 语义 | 引入 `cap == -2` 表示 borrowed；审查所有 cap 判断站点 | 中 | 中 |
| M-3 | 统一所有权转移 API | 新增 `cg_ownership_transfer` / `cg_ownership_access`，替换分散站点 | 大 | 中 |
| M-4 | 补齐遗漏站点 + 对称审查 | 系统性审查 vec[i]=, map[k]=, array[i]= 全类型分支 | 中 | 低 |
| M-5 | 加强 checker 端 move 分析 | 编译期拒绝 move 后使用 | 中 | 低 |
| M-6 | 内存模型回归测试套件 | 新增 `memcheck_overhaul.ls` 极端测试覆盖所有修复点 | 小 | 无 |

---

## M-1：消除双轨临时值跟踪

### 1.1 问题描述

当前存在两套独立的临时动态 string 跟踪机制：

**机制 A — `temp_string_slots`（语句级）**
- 注册：`cg_push_temp_string(ctx, str_val)` → 在函数 entry block 创建 alloca，存入 `temp_string_slots[]`
- 释放：`cg_flush_temps(ctx, mark, skip_last)` → 遍历 `temp_string_slots[mark..count)`，逐个 `emit_string_free`
- 调用点：`codegen_string_method`（upper/lower/substr/trim/replace/copy）、`codegen_format_string`、string concat、`codegen_block_call`（返回 string）、用户函数 `AST_CALL`（返回 string）等，共 20+ 处

**机制 B — `__argtmp` scope 注册（作用域级）**
- 注册：在 `codegen_print_call` 中，对 `expr_produces_dynamic_string()` 返回 `true` 的参数，创建 alloca `"str.argtmp"`，`cg_scope_define("__argtmp", alloca, TYPE_STRING)`
- 释放：`emit_scope_cleanup()` 在作用域退出时检测到 TYPE_STRING 的 `__argtmp` 符号，调 `emit_string_free`

**冲突**：当一个 string 同时被两套机制注册时（例如 `print(s.upper())`），`cg_flush_temps` free 一次 + scope cleanup free 一次 → double-free。

历史上已通过在 `expr_produces_dynamic_string` 中逐个添加 `return false` 排除项来绕过（BF-008 排除 Block call，BF-025 排除用户函数 call），但这是打地鼠——每新增一种产生动态 string 的路径都可能漏掉。

### 1.2 改动计划

**步骤 1**：移除 `codegen_print_call` 中的 `__argtmp` 注册块（已在本次 session 完成）

确认 `codegen_print_call` 中不再有 `cg_scope_define("__argtmp", ...)` 调用。所有动态 string 参数的清理统一由 `cg_flush_temps` 在 `AST_EXPR_STMT` 结束时执行。

**步骤 2**：移除 `expr_produces_dynamic_string` 函数

该函数当前无调用点（唯一调用者已删除）。移除函数体和所有相关注释。保守起见，搜索全文确认无引用后再删除。

**步骤 3**：审查所有 `cg_scope_define` 调用中 name 以 `"__"` 开头的注册

搜索 `cg_scope_define(ctx->current_scope, "__` 确保无其他内部临时注册会产生类似冲突。特别关注：
- `__argtmp` → 已移除
- `__ret` → return value skip（不释放，是正确行为）
- 其他 `__` 前缀符号

**步骤 4**：验证 `cg_flush_temps` 覆盖所有 `print()` 参数路径

检查以下 `print()` 用法在 `AST_EXPR_STMT` 时 temp 被正确释放：
- `print(s.upper())` — string 方法返回值
- `print("a" + "b")` — string 拼接
- `print(f"x={val}")` — f-string（作为 print 参数内联展开，不经过独立 `cg_push_temp_string`，需确认 print 内联展开路径的 text part 不泄漏）
- `print(my_fn())` — 用户函数返回 string
- `print(my_block())` — Block call 返回 string
- `print(to_string(42))` — to_string 返回 string

### 1.3 新增测试

```
tests/samples/test_mem_m1_temp_unify.ls
```

```ls
// M-1 验证：print 参数中各种动态 string 来源不 leak 不 double-free
fn make_str() -> string {
    return "dynamic".upper()
}

fn main() -> int {
    string s = "hello"

    // Case 1: string 方法
    print(s.upper())

    // Case 2: 拼接
    print("a" + "b")

    // Case 3: f-string
    int n = 42
    print(f"n={n}")

    // Case 4: 用户函数返回
    print(make_str())

    // Case 5: 链式方法
    print("  HELLO  ".trim().lower())

    // Case 6: 多参数
    print(s.upper(), s.lower(), "literal")

    return 0
}
```

**验证命令**：
```
ls run --memcheck tests/samples/test_mem_m1_temp_unify.ls
ls compile --memcheck tests/samples/test_mem_m1_temp_unify.ls -o tmp.exe && tmp.exe
```

两者均须输出 `[memcheck] OK clean`。

### 1.4 CG_DEBUG 检查清单

- `cg_push_temp_string`：已有 `#if CG_DEBUG` 块（确认其打印 slot 地址和 cap）
- `cg_flush_temps`：已有 `emit_string_free` 内部的 `#if CG_DEBUG`（会打印 `str.free` 或 `str.skip`）
- 移除的 `__argtmp` 路径：无需新增（已删除）
- **无新增 alloc/free 点，无需新增 CG_DEBUG 块**

### 1.5 完成标准

- [x] `codegen_print_call` 中无 `cg_scope_define("__argtmp", ...)`
- [x] `expr_produces_dynamic_string` 函数已删除（仅余历史注释引用）
- [x] 代码中无 `__argtmp` 注册逻辑（注释提及不算）
- [x] `ctest --output-on-failure -C Release` 全部 PASS
- [x] `ls run --memcheck test_mem_m1_temp_unify.ls` → OK clean
- [x] AOT memcheck 同上

---

## M-2：拆分 `cap == 0` 语义

### 2.1 问题描述

`LsString` 的 `cap` 字段承载三个状态：

| cap 值 | 当前含义 | 问题 |
|--------|----------|------|
| `> 0` | Owned（堆分配） | ✅ 无歧义 |
| `== 0` | Static `.rodata` **或** `&string` borrowed 参数 | ⚠️ 语义重载 |
| `== -1` | Moved（所有权已转移） | ✅ 无歧义 |

关键矛盾：`emit_string_clone_val` 对 `cap == 0` 返回原值（不克隆），这对 Static `.rodata` 是正确的（不需要克隆，data 指向不可变全局常量），但对 borrowed 参数是**错误的**——borrowed 参数的 data 指向 caller 的堆 buffer，如果消费站点（enum ctor、struct ctor）不克隆就存入 payload，caller 退出后 payload 持有悬垂指针。

### 2.2 设计决策

引入 `cap == -2` 表示 **Borrowed**（不拥有堆内存，data 指向别人的堆 buffer）。

完整四态：

| cap 值 | 名称 | 含义 | 释放规则 | 克隆规则 |
|--------|------|------|----------|----------|
| `> 0` | **OWNED** | 堆分配，当前变量持有所有权 | free(data) | malloc+memcpy |
| `== 0` | **STATIC** | data 指向 `.rodata` 全局常量 | 跳过 | 跳过（返回原值） |
| `== -1` | **MOVED** | 所有权已转移 | 跳过 | 不应发生（编译器应阻止） |
| `== -2` | **BORROWED** | data 指向 caller 的堆 buffer | 跳过 | malloc+memcpy（必须克隆才能获取所有权） |

### 2.3 改动计划

#### 步骤 1：定义常量（`common.h`）

```c
/* LsString cap 语义常量 */
#define LS_CAP_STATIC    0    /* data 指向 .rodata 常量 */
#define LS_CAP_MOVED    (-1)  /* 所有权已转移 */
#define LS_CAP_BORROWED (-2)  /* 借用，data 指向别人的堆 buffer */
```

#### 步骤 2：修改 `codegen_fn_decl` 中 `&string` 参数标记

当前（`codegen_fn_decl`）：
```c
// 对 TYPE_STRING 参数，将 cap 设为 0
LLVMValueRef cap0 = LLVMConstInt(i32_type, 0, 0);
new_str = LLVMBuildInsertValue(ctx->builder, ..., cap0, 2, "...");
```

改为：
```c
LLVMValueRef cap_borrowed = LLVMConstInt(i32_type, (uint64_t)(int32_t)-2, 1);
new_str = LLVMBuildInsertValue(ctx->builder, ..., cap_borrowed, 2, "...");
```

#### 步骤 3：修改 `emit_string_clone_val` 的判断条件

当前：
```c
// if cap > 0 → clone, else → return as-is
```

改为：
```c
// if cap > 0 || cap == -2 → clone (owned 或 borrowed 都需要克隆)
// if cap == 0 → return as-is (static .rodata)
// if cap == -1 → return as-is (moved, 不应到达这里)
```

具体 LLVM IR：将 `cap > 0` 条件改为 `cap > 0 || cap == -2`：
```c
LLVMValueRef is_owned = LLVMBuildICmp(ctx->builder, LLVMIntSGT, cap, zero, "sc.owned");
LLVMValueRef neg2 = LLVMConstInt(i32_type, (uint64_t)(int32_t)-2, 1);
LLVMValueRef is_borrowed = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cap, neg2, "sc.borr");
LLVMValueRef need_clone = LLVMBuildOr(ctx->builder, is_owned, is_borrowed, "sc.need");
```

#### 步骤 4：修改 `emit_string_free_with_cont` 的判断条件

当前：`cap > 0` → free，`cap <= 0` → skip。

这仍然正确：`cap == -2`（borrowed）也 `<= 0`，会被 skip（不释放别人的内存）。**无需改动**，但需确认注释更新。

#### 步骤 5：修改 `mark_string_moved` 的判断条件

当前：`cap > 0` 时设 `cap = -1`。

Borrowed（`cap == -2`）是否需要 move 标记？不需要——borrowed string 不持有堆内存，move 后 scope cleanup 本来就会 skip（`cap == -2 <= 0`）。**无需改动**，但需确认 `cap == -2` 不被意外设为 `-1`。

#### 步骤 6：审查所有 `cap` 判断站点

全文搜索 `cap.*>.*0`、`cap.*==.*0`、`cap.*<.*0`、`cap.*-1`，逐个确认：

| 站点 | 当前逻辑 | 是否需要改动 |
|------|----------|------------|
| `emit_string_free_with_cont` | `cap > 0 → free` | ✅ 不变（-2 不 free） |
| `mark_string_moved` | `cap > 0 → set -1` | ✅ 不变（-2 不需要 move） |
| `emit_string_clone_val` | `cap > 0 → clone` | ⚠️ **改为 `cap > 0 \|\| cap == -2`** |
| `emit_string_append_inline` | `cap == 0 → malloc new + copy` | ⚠️ 审查：对 borrowed 参数 `&!string` 的 append 路径 |
| `ls_string_from_literal` | `cap = 0` | ✅ 不变（仅用于 .rodata） |
| 闭包 env drop | `cap > 0 → free capture` | ✅ 不变 |
| vec clone loop | `emit_string_clone_val` | ✅ 间接受益 |
| map helper `COPY_VAL` | `emit_string_clone_val` | ✅ 间接受益 |
| `emit_enum_ctor` | BF-032 手动处理 | ⚠️ 可简化——不再需要 AST_IDENT 特判 |

#### 步骤 7：修改 `emit_enum_ctor` 简化 BF-032 修复

BF-032 的修复是针对 `cap == 0` 重载的 workaround：对 AST_IDENT 源无条件 malloc+memcpy。有了 `cap == -2` 后，`emit_string_clone_val` 本身就能正确处理 borrowed → clone，因此 `emit_enum_ctor` 可以回到统一的 `emit_string_clone_val` 路径。

### 2.4 新增测试

```
tests/samples/test_mem_m2_cap_borrowed.ls
```

```ls
// M-2 验证：borrowed string 参数在各种消费站点的正确性

enum Wrapper {
    Val(string)
    None
}

struct Box {
    string content
}

fn wrap_enum(string s) -> Wrapper {
    return Val(s)            // s 是 borrowed (cap=-2), enum ctor 必须 clone
}

fn wrap_struct(string s) -> Box {
    return Box{content: s}   // s 是 borrowed, struct ctor 必须 clone
}

fn identity(string s) -> string {
    return s.copy()          // 显式 copy，确认 borrowed → clone
}

fn main() -> int {
    string owned = "hello".upper()

    // Case 1: borrowed → enum ctor
    Wrapper w = wrap_enum(owned)
    match w {
        Val(v) => print(v)
        None => print("none")
    }

    // Case 2: borrowed → struct ctor
    Box b = wrap_struct(owned)
    print(b.content)

    // Case 3: borrowed → identity
    string id = identity(owned)
    print(id)

    // Case 4: owned 仍然 live（上面传参是借用，不是 move）
    print(owned)

    return 0
}
```

**预期输出**：`HELLO` 四次。

**验证**：JIT memcheck + AOT memcheck 均 OK clean。

### 2.5 CG_DEBUG 检查清单

- `codegen_fn_decl` 中 borrowed 参数标记 `cap = -2`：新增 `#if CG_DEBUG` 打印 `[cg] param.borrow name=%-12s cap=-2`
- `emit_string_clone_val` 中 borrowed clone 路径：已有 `#if CG_DEBUG` 块（会打印 `str.clone`），确认 `cap == -2` 时也能正确触发
- `emit_string_free_with_cont` 中 skip 路径：已有 `#if CG_DEBUG` 打印 `str.skip`，确认 `cap == -2` 时显示 `(borrowed)` 而非 `(static or moved)`

### 2.6 完成标准

- [x] `common.h` 定义 `LS_CAP_STATIC / LS_CAP_MOVED / LS_CAP_BORROWED` 三个常量
- [x] `codegen_fn_decl` 中 `&string` 参数使用 `cap = -2`
- [x] `emit_string_clone_val` 对 `cap == -2` 执行克隆
- [x] `emit_string_free_with_cont` 对 `cap == -2` 跳过释放（已满足，确认注释）
- [x] `emit_enum_ctor` 的 BF-032 workaround 简化为统一的 `emit_string_clone_val`
- [x] 所有 `cap` 判断站点已审查并更新注释
- [x] `cap` 判断站点逐个确认正确
- [x] `ctest` 全 PASS
- [x] `ls run --memcheck test_mem_m2_cap_borrowed.ls` → OK clean（JIT + AOT）
- [x] CG_DEBUG 打开后，borrowed 参数的 `param.borrow` 日志可见

---

## M-3：统一所有权转移 API

### 3.1 问题描述

当前每个"值存入容器"操作（push、set、assign、enum ctor、struct ctor、match binder return、闭包捕获）各自手写 20-40 行 move/clone/mark 逻辑。全文共 81 处 clone 调用 + 69 处 move/temp 调用 + 78 处 free/cleanup 调用。

每新增一种类型或容器操作，需要在 5+ 个位置添加对称分支（struct 加了 → 忘了 enum；push 加了 → 忘了 index assign）。

### 3.2 设计：`cg_store_owned` API

新增统一的"将值存入目标位置并正确处理所有权"函数：

```c
/* 所有权转移模式 */
typedef enum {
    CG_XFER_INTO_CONTAINER,   /* v.push(x) / v[i]=x / map.set(k,v) / enum_ctor / struct_ctor */
    CG_XFER_ASSIGN_VAR,       /* string a = b / a = expr */
    CG_XFER_RETURN,           /* return val */
} CgTransferKind;

/* 统一的"值存入目标"操作。
   dst_ptr: 目标位置（GEP 或 alloca）
   val:     SSA 值
   type:    LS 类型
   source:  AST 源节点（用于判断 IDENT/rvalue/borrowed）
   kind:    转移模式
   
   根据 type 和 source 自动选择：
   - POD (int/f64/bool/char/object/*T): 直接 store
   - string:
     - source 是 rvalue (AST_CALL/AST_FORMAT_STRING/AST_BINARY concat): 直接 store + mark temp moved
     - source 是 IDENT (命名变量):
       - INTO_CONTAINER: store + mark_string_moved(source)
       - ASSIGN_VAR: clone + store
       - RETURN: store + mark_string_moved(source)
     - source 是 borrowed binder: clone + store
   - struct(has_drop): 同 string，clone via emit_struct_clone_val
   - enum(has_drop): 同 string，clone via emit_enum_clone_val
   - vec(T): 直接 store + mark source if IDENT (vec 是 move 类型)
   - map(K,V): 直接 store + mark source if IDENT
   - Block(...): 直接 store + null source env if IDENT
*/
static void cg_store_owned(CodegenContext *ctx,
                           LLVMValueRef dst_ptr,
                           LLVMValueRef val,
                           Type *type,
                           AstNode *source,
                           CgTransferKind kind);
```

### 3.3 改动计划

#### 步骤 1：实现 `cg_store_owned`

在 `codegen.c` 中实现，位置在 `mark_string_moved` 之后。函数内部使用已有的 `emit_string_clone_val` / `emit_struct_clone_val` / `emit_enum_clone_val` / `mark_string_moved` 等原语。

#### 步骤 2：逐站点替换 — vec.push

当前：~50 行手写逻辑（string clone/move/mark、struct clone/mark、enum clone/mark、Block env null）
替换为：`cg_store_owned(ctx, elem_ptr, val, elem_type, arg0, CG_XFER_INTO_CONTAINER)`

#### 步骤 3：逐站点替换 — vec[i] = val

当前：~30 行（drop old + store new + mark source for string/struct/enum）
替换为：先 `emit_drop_at(gep, type)` 释放旧值，再 `cg_store_owned(ctx, gep, val, elem_type, rhs, CG_XFER_INTO_CONTAINER)`

#### 步骤 4：逐站点替换 — enum ctor payload

当前：`emit_enum_ctor` 中 ~60 行 per-type 分支
替换为：`cg_store_owned(ctx, payload_gep, val, payload_type, arg, CG_XFER_INTO_CONTAINER)`

#### 步骤 5：逐站点替换 — struct ctor 字段

当前：`AST_NEW_EXPR` 中 per-field 手写 clone/move 逻辑
替换为：per-field `cg_store_owned(ctx, field_gep, val, field_type, init_expr, CG_XFER_INTO_CONTAINER)`

#### 步骤 6：逐站点替换 — 变量赋值

当前：`AST_ASSIGN` 中 `string a = b` 的 clone/move 逻辑（~40 行分 IDENT/expression/self-assign 三路）
替换为：`cg_store_owned(ctx, sym->value, val, type, rhs, CG_XFER_ASSIGN_VAR)`

注意：保留 `a = a + b` in-place append 优化路径，不经过 `cg_store_owned`。

#### 步骤 7：逐站点替换 — vec.insert

与 vec.push 完全对称。

#### 步骤 8：验证 — 不替换的站点

以下站点逻辑特殊，保持手写：
- `return val`：涉及 scope cleanup skip list，与 `cg_store_owned` 不完全匹配
- 闭包捕获：by-ref 捕获（vec/map）不走 store_owned 路径
- `map.set`：由 emit_map_helpers_for 生成的辅助函数处理，内部有自己的 clone 逻辑

### 3.4 新增测试

```
tests/samples/test_mem_m3_xfer_unified.ls
```

```ls
// M-3 验证：统一所有权转移 API 在各站点的正确性

struct Person {
    string name
    int age
}

enum Value {
    Str(string)
    Num(int)
    None
}

fn main() -> int {
    // 1. vec.push string IDENT → move
    string s = "hello".upper()
    vec(string) v1 = []
    v1.push(s)
    print(v1[0])

    // 2. vec[i] = string IDENT → move
    vec(string) v2 = ["aaa".copy(), "bbb".copy()]
    string x = "ccc".copy()
    v2[0] = x
    print(v2[0])

    // 3. enum ctor with string → clone when borrowed
    string name = "Bob".upper()
    Value val = Str(name.copy())
    match val {
        Str(sv) => print(sv)
        Num(n) => print(n)
        None => print("none")
    }

    // 4. struct ctor with string
    Person p = Person{name: "Alice".upper(), age: 30}
    print(p.name)

    // 5. vec swap via index assign (双向)
    vec(string) v3 = ["AAA".copy(), "BBB".copy()]
    string a = v3[0].copy()
    string b = v3[1].copy()
    v3[0] = b
    v3[1] = a
    print(v3[0])
    print(v3[1])

    return 0
}
```

**验证**：JIT memcheck + AOT memcheck 均 OK clean。

### 3.5 CG_DEBUG 检查清单

`cg_store_owned` 内部：
- 每种 transfer 路径新增 `#if CG_DEBUG` 打印：
  - `[cg] xfer.move  type=string dst=... src=IDENT`
  - `[cg] xfer.clone type=string dst=... src=IDENT`
  - `[cg] xfer.temp  type=string dst=... src=rvalue`
  - `[cg] xfer.pod   type=int    dst=...`

### 3.6 实施策略：逐站点替换

**不要一次全部替换**。每替换一个站点后：
1. `ctest` 全 PASS
2. `ls run --memcheck memcheck_edge.ls` → OK clean
3. `ls run --memcheck test_mem_m3_xfer_unified.ls` → OK clean
4. git commit

替换顺序（从低风险到高风险）：
1. `vec[i] = val`（刚修复过，最熟悉）
2. `vec.push`（逻辑最重，替换收益最大）
3. `enum ctor`
4. `struct ctor`
5. `vec.insert`
6. `AST_ASSIGN string`

### 3.7 完成标准

- [x] `cg_store_owned` 函数已实现，覆盖 string/struct(has_drop)/enum(has_drop)/vec/map/Block 全类型
- [x] vec.push / vec[i]= / enum ctor / struct ctor / vec.insert / AST_ASSIGN 六站点已替换
- [x] 替换前后生成的 LLVM IR 语义等价
- [x] `ctest` 全 PASS
- [x] `ls run --memcheck test_mem_m3_xfer_unified.ls` → OK clean
- [x] `ls run --memcheck memcheck_edge.ls` → OK clean
- [x] CG_DEBUG 打开后 `xfer.*` 日志可见

---

## M-4：补齐遗漏站点 + 对称审查

### 4.1 问题描述

历史上多次出现"为类型 A 加了处理、忘了类型 B"的 bug 模式。M-3 的统一 API 能防止未来遗漏，但当前仍有存量遗漏需要排查。

### 4.2 审查矩阵

逐个检查以下矩阵的每个单元格：

**行 = 操作站点**，**列 = 值类型**

| 操作 \ 类型 | string | struct(drop) | enum(drop) | vec(T) | map(K,V) | Block | POD |
|-------------|--------|-------------|------------|--------|----------|-------|-----|
| `vec.push(val)` | ✅ move | ✅ move | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ env null | ✅ store |
| `vec[i] = val` | ✅ move (M-1修) | ⬜ 审查 drop old | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ store |
| `vec.insert(i, val)` | ✅ move | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ store |
| `map.set(k, v)` | ✅ clone | ⬜ 审查 | ✅ clone | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ store |
| `enum ctor(payload)` | ✅ clone/move | ✅ clone | ⬜ 审查嵌套 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ store |
| `struct ctor{field:}` | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ store |
| `array[i] = val` | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | N/A | N/A | N/A | ✅ store |
| `string a = expr` | ✅ clone/move | N/A | N/A | N/A | N/A | N/A | N/A |
| `struct a = expr` | N/A | ⬜ 审查 | N/A | N/A | N/A | N/A | N/A |
| `return val` | ✅ skip cleanup | ✅ skip cleanup | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ pop env | ✅ |
| `fn_call(arg)` | ✅ borrow ABI | ✅ clone | ✅ clone | ✅ borrow | ✅ borrow | ✅ store | ✅ |
| 闭包捕获 | ✅ by-move | ✅ by-move | ⬜ 审查 | ✅ by-ref | ✅ by-ref | N/A | ✅ by-copy |
| match binder | ✅ clone | ⬜ 审查 | ✅ clone | ⬜ 审查 | ⬜ 审查 | ⬜ 审查 | ✅ copy |

⬜ = 需要审查。每个审查结果要么确认"已正确"，要么标记为 bug 并修复 + 补测试。

### 4.3 审查方法

对每个 ⬜ 单元格：
1. 在 `codegen.c` 中找到对应代码路径
2. 编写最小 `.ls` 测试用例
3. 用 `ls run --memcheck` 运行
4. 确认 0 leak / 0 dfree
5. 如果失败 → 修复 + 补 ctest

### 4.4 新增测试

```
tests/samples/test_mem_m4_matrix.ls
```

覆盖审查矩阵中所有 ⬜ 单元格的测试用例。预计 20-30 个 case。

### 4.5 CG_DEBUG 检查清单

- 每个修复的站点必须有 `#if CG_DEBUG` 跟踪
- 如果修复涉及新的 clone/drop 路径，确认 `[cg] *.clone` / `[cg] *.drop` 日志覆盖

### 4.6 完成标准

- [x] 审查矩阵所有 ⬜ 标记为 ✅（确认正确）或已修复
- [x] 每个修复有对应的 `.ls` 测试
- [x] `ctest` 全 PASS
- [x] `ls run --memcheck test_mem_m4_matrix.ls` → OK clean
- [x] 新发现的 bug 编号录入 `bugfix_registry.md`

### 4.7 M-4.5：vec[i].field 临时 has_drop struct 泄漏（派生任务，已修复）

**问题**：`vit[0].name`（`vit` 为 `vec(has_drop struct)`）中，`vit[0]` 经
`AST_INDEX` 路径返回元素的**深拷贝**（容器仍保留自己的副本，见 `codegen.c`
vec[i] READ 路径）。字段访问在 `AST_FIELD` 路径把该深拷贝 spill 到 `tmp.struct`
alloca，读完目标字段后**这个临时 struct 从不被 drop**，其它 string 字段泄漏。

**为何只在 field access 出现**：
- 所有权转移形式（`Item it = vit[0]`）：`codegen_expr` 返回的深拷贝直接 store
  进命名变量，由变量作用域 drop 释放——基线已正确，无泄漏。
- `match vec[i]`（enum 临时）：match 自身的 rvalue-temp subject drop 逻辑
  （`emit_enum_drop(subj_alloca)`）已正确释放——基线已正确。
- `vec[i].method()`：LS 不支持（编译期报 "cannot take address of object for
  method call"），不存在该泄漏场景。
- 因此真正的存量泄漏**仅限 has_drop struct 经 vec[i] 的字段访问**。

**修复**：引入语句级 `temp_drop` 跟踪（`codegen.h` 的 `temp_drop_*` 字段 +
`cg_push_temp_drop` / `cg_flush_temp_drops`）。在 `AST_FIELD` 的 spill 分支，当
object 为 `AST_INDEX` 且 struct `has_drop` 时注册 spill slot；`cg_flush_temps`
在语句边界对其 `emit_struct_drop`。被访问字段在其后独立 clone，故 drop 临时
struct 不影响返回值。`temp_drop_marks[i]` 记录 push 时 `temp_string_count`，与既
有 string-temp mark 语义对齐。

> 教训：最初尝试在通用 vec[i] 路径无条件 push + 在各消费点 pop，破坏了 enum
> match / VAR_DECL 等本就正确的转移路径（json e2e 堆损坏）。正确边界是只在
> **确定丢弃的 field-access 临时**注册 drop，不触碰任何所有权转移点。

**测试**：`tests/samples/test_mem_m4_5_drop_temp.ls`（`test_mem_m4_5_jit` +
`test_mem_m4_5_aot`），覆盖直接字段访问 / 循环内字段访问 / POD 字段访问（struct
其它 string 字段仍需释放）/ 所有权转移对照 / enum match 对照。JIT + AOT memcheck
均 OK clean；CG_DEBUG 下可见 `tdrop.push`。`ctest` 67/67。

---

## M-5：加强 checker 端 move 分析

### 5.1 问题描述

当前 checker（`types.c`）对 move 后使用的检测是不完整的：
- `v.push(s)` 后 checker 知道 `s` 被 move
- 但 `v[0] = s` 后 checker 不知道 `s` 被 move
- `enum ctor(s)` / `struct ctor{field: s}` 后 checker 也不完全跟踪

结果：move 后使用的错误只能在运行时通过 `cap == -1` 检测（打印 warning），而非编译期拒绝。

### 5.2 改动计划

#### 步骤 1：扩展 move 触发点列表

在 checker 的 `check_expr` / `check_statement` 中，以下操作应标记 source 为 MOVED：

| 操作 | 当前 | 目标 |
|------|------|------|
| `v.push(s)` | ✅ 标记 | ✅ |
| `v[i] = s` | ❌ 不标记 | ✅ 标记 |
| `Ctor(s)` enum ctor | ❌ 不标记 | ✅ 标记 |
| `Struct{f: s}` struct ctor | ❌ 不标记 | ✅ 标记 |
| `v.insert(i, s)` | ❌ 不标记 | ✅ 标记 |
| `map.set(k, s)` (val 位) | ⬜ 审查 | ✅ 标记 |

#### 步骤 2：在 move 后使用时报编译错误

对处于 MOVED/MAYBE_MOVED 状态的变量的读操作，报错：
```
[move] file:line:col: variable 's' used after move (moved at file:line:col)
```

#### 步骤 3：处理 clone 语义的 ASSIGN_VAR

`string a = b`（变量赋值）是 clone 语义，`b` 不被 move。确保 checker 不误标记。

### 5.3 新增测试

编译期拒绝测试（negative test）：
```
tests/samples/test_mem_m5_move_after_use.ls
```

```ls
// 期望：编译错误，不能运行
fn main() -> int {
    string s = "hello".upper()
    vec(string) v = []
    v.push(s)
    print(s)    // ← 编译错误：s used after move
    return 0
}
```

### 5.4 CG_DEBUG 检查清单

- checker 端改动不涉及 codegen，无需新增 `#if CG_DEBUG` 块
- 但需确认 checker 的 move 标记与 codegen 的 `mark_string_moved` 一致

### 5.5 完成标准

> **实测修正（2026-05-28）**：5.1/5.2 的"缺口表"写于 M-3 之前，已过时。
> `checker.c`（非文档早期所写的 types.c）已有完善的 move 分析：
> `checker_try_mark_moved` + 分支快照（snapshot/merge）+ 循环 MAYBE_MOVED 降级。
> 逐站点实测结论：
>
> | 操作 | codegen 所有权语义 | checker 行为（实测） | 一致性 |
> |------|------|------|------|
> | `v.push(s)` | move | 标记 MOVED，move-after-use 拒绝 | ✅ |
> | `v[i] = s` | move | 标记 MOVED，move-after-use 拒绝 | ✅（文档说"不标记"是错的） |
> | `map.set(k, s)` | **clone** | ~~标记 MOVED~~ → **本次移除**（BF-039） | ✅（clone 不应标记） |
> | `enum Ctor(s)` | **clone** | 不标记，源仍 live | ✅（文档"应标记"是错的） |
> | `struct {f: s}` | **clone** | 不标记，源仍 live | ✅ |
> | `string a = b` | clone | 不触发 move | ✅ |
>
> **关键修正**：文档 5.2 假设 `Ctor(s)` / `Struct{f:s}` 是 move 因而"应标记"，
> 实际 codegen 是 clone 语义，**不应**标记——checker 现状已正确。唯一的真实
> 不一致是 `map.set`：checker 误标 move（clone 语义），导致误报 + 源 scope drop
> 被 skip。本次移除该误标（见 [BF-039](bugfix_registry.md)）。
>
> 另：实测中发现 `map[key]` / `m.get(key)` 读取返回 value 深拷贝，string value
> 临时使用未注册 temp → 泄漏（与 M-4.5 的 vec[i] 同源）。一并修复（BF-039）。

- [x] move 语义操作（`v.push` / `v[i]=`）的 move-after-use 报编译错误（实测已拒绝）
- [x] clone 语义操作（`enum/struct ctor` / `map.set`）后源仍可用（不误报）
- [x] `string a = b` 不触发 move（clone 语义）
- [x] 正向测试 `test_mem_m5_move_ok.ls`：编译通过 + JIT/AOT memcheck clean
- [x] 负向测试 `test_mem_m5_neg_{push,index,branch}.ls`：move/maybe-move 后使用被拒绝
- [x] `ctest` 全 PASS（70/70，含新增 `test_mem_m5_{jit,aot,neg}`）

---

## M-6：内存模型回归测试套件

### 6.1 目标

新增一个"极端场景"测试文件，覆盖 M-1 到 M-5 修复的所有路径，作为永久的回归守护。

### 6.2 测试文件

```
tests/samples/memcheck_overhaul.ls
```

覆盖以下场景类别：

| 类别 | 场景数 | 覆盖 |
|------|--------|------|
| print 各种动态 string 参数 | 6 | M-1 |
| borrowed string 跨函数边界 | 4 | M-2 |
| vec 元素 swap（index assign） | 2 | M-4 / bug_23 |
| enum ctor with borrowed string | 2 | M-2 / BF-032 |
| struct ctor with string method | 2 | M-4 |
| 循环内 string 分配 + break | 2 | BF-012 |
| match binder return | 2 | BF-029 |
| try 早返路径 string | 2 | BF-012 |
| 闭包捕获 string + struct | 2 | Phase C/F |
| vec(string) push + pop + for | 2 | BF-001 |
| map(string, string) set + get | 2 | M-4 |
| 自递归 enum（Tree） | 1 | BF-015/023 |
| f-string with % literal | 1 | 今日修复 |

**总计 30+ 个 case**，全部在一个 `main()` 中。

### 6.3 CTest 注册

```cmake
# memcheck_overhaul: JIT path
add_test(
    NAME test_memcheck_overhaul_jit
    COMMAND ${LS_EXE} run --memcheck
            ${CMAKE_SOURCE_DIR}/tests/samples/memcheck_overhaul.ls
)
set_tests_properties(test_memcheck_overhaul_jit PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[memcheck\\] OK clean"
)

# memcheck_overhaul: AOT path
add_test(
    NAME test_memcheck_overhaul_aot
    COMMAND ${CMAKE_COMMAND}
        -DLS_EXE=$<TARGET_FILE:ls>
        -DSAMPLE=${CMAKE_SOURCE_DIR}/tests/samples/memcheck_overhaul.ls
        -DWORK_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/tests/test_memcheck_aot.cmake
)
```

### 6.4 完成标准

- [x] `memcheck_overhaul.ls` 15 类 30+ case 全部在 JIT + AOT + memcheck 下 OK clean
- [x] 注册到 CMakeLists.txt（`test_mem_overhaul_{jit,aot}`），成为 ctest 的一部分
- [x] 未来任何 codegen 改动破坏内存安全 → 此测试立即失败

> 实现说明：`tests/samples/memcheck_overhaul.ls` 单文件覆盖 M-1~M-5 全部修复路径
> （print 动态 string / borrowed 跨边界 / enum·struct ctor clone / vec swap /
> push·pop·for / map set·get·index / match binder return / try 早返 / 循环内
> 分配+break / 闭包捕获 / 自递归 Tree drop / f-string %% / vec[i].field 临时
> (M-4.5) / map[key] 临时与转移 (BF-039)）。ctest 72/72。

---

## 附录 A：已确认的存量 Bug（本次 session 发现）

| # | 描述 | 影响 | 阶段修复 |
|---|------|------|----------|
| 1 | `scanner_next` 无条件 `skip_whitespace` 吃掉 f-string 文本中的空格 | `f"{x} y"` → `"xy"` | 已修复（scanner.c） |
| 2 | f-string 文字段 `%` 未转义为 `%%`，被 printf 解析 | `f"50%"` → UB | 已修复（codegen.c，4 处） |
| 3 | `print(s.upper())` double-free：`__argtmp` + `temp_string_slots` 双注册 | memcheck CRASH | 已修复（M-1 的一部分） |
| 4 | `v[0] = b`（`b` 为命名 string 变量）未标记 `b` 为 moved → double-free | memcheck CRASH | 已修复（codegen.c vec[i]= 路径） |

---

## 附录 B：排查清单（每阶段结束必做）

```
□ 1. ctest --output-on-failure -C Release          → 全部 PASS
□ 2. ls run --memcheck memcheck_phase_a.ls          → OK clean
□ 3. ls run --memcheck memcheck_edge.ls             → OK clean
□ 4. ls run --memcheck memcheck_kinds.ls            → OK clean
□ 5. ls run --memcheck <本阶段新增测试>.ls           → OK clean
□ 6. ls compile --memcheck <本阶段新增测试>.ls -o t.exe && t.exe → OK clean
□ 7. cmake -DLS_CG_DEBUG=ON 构建 → 运行新增测试 → CG_DEBUG 日志无异常
□ 8. git diff 审查 → 无遗漏的 #if CG_DEBUG 块（每个新增 alloc/free/clone/move 点）
□ 9. bugfix_registry.md 更新（如有新发现 bug）
```

---

## 附录 C：文件改动清单（预估）

| 文件 | M-1 | M-2 | M-3 | M-4 | M-5 | M-6 |
|------|-----|-----|-----|-----|-----|-----|
| `src/common.h` | — | ✏️ 新增常量 | — | — | — | — |
| `src/codegen.c` | ✏️ 删 `__argtmp` | ✏️ clone/free 条件 | ✏️ 新增 API + 替换 | ✏️ 补遗漏 | — | — |
| `src/types.c` | — | — | — | — | ✏️ move 分析 | — |
| `tests/samples/*.ls` | ✏️ +1 | ✏️ +1 | ✏️ +1 | ✏️ +1 | ✏️ +1 | ✏️ +1 |
| `CMakeLists.txt` | ✏️ +1 test | ✏️ +1 test | ✏️ +1 test | ✏️ +1 test | ✏️ +1 test | ✏️ +2 test |
| `docs/bugfix_registry.md` | ✏️ | ✏️ | — | ✏️ | — | — |
| `docs/ownership.md` | — | ✏️ 四态表 | ✏️ API 描述 | — | ✏️ move 规则 | — |

---

## 附录 D：实施顺序与依赖关系

```
M-1（消除双轨）────► M-2（拆分 cap）────► M-3（统一 API）
                                              │
                                              ▼
                                         M-4（补齐站点）────► M-5（checker move）
                                              │
                                              ▼
                                         M-6（回归套件）
```

- M-1 是前置条件：必须先消除双轨，否则 M-3 的统一 API 仍会遇到双注册问题
- M-2 是 M-3 的前置条件：统一 API 需要依赖 borrowed/static 的正确区分
- M-4 可与 M-3 并行（M-3 替换站点时顺便审查 M-4 矩阵）
- M-5 独立于 M-3/M-4，但最好在之后做（codegen 稳定后再加 checker 约束）
- M-6 在最后，汇总所有修复的回归测试
