# Region — 一等公民内存隔离区（Arena + 逃逸拷贝）

> 状态：设计提案 | 优先级：中（Phase 0 重构可独立先行）

---

## 1. 动机

LS 现有内存管理的问题：

| 问题 | 现状 | Region 的解法 |
|------|------|-------------|
| 信号处理/解析/计算密集型场景频繁 malloc/free | 每次操作独立分配释放，开销大 | Arena 内 O(1) bump 分配，退出整块 O(1) 释放 |
| 闭包 by-ref 生命周期不检查 | CLAUDE.md 明确标注"编译器不检查，用户自行保证" | 闭包禁止逃逸出 region（编译期检查） |
| 大量临时分配后仅少量结果需要保留 | 所有值独立释放，无法批量 | 不逃逸的值 O(1) 释放，逃逸的值按需深拷贝 |

核心设计理念：**用空间的物理隔离（arena bump allocation），降低时间维度的释放复杂度（O(1) bulk free）**。

---

## 2. 语法设计

### 2.1 基本形式

```ls
fn process(data: string) -> int {
    region Buffer {
        var buf = vec(u8)
        var lookup = map(int, string)
        buf.append_many(data)
        lookup.set(42, data)
        return buf.len()   // 逃逸拷贝：i32 是 POD，直接返回
    }
    // '}' → arena O(1) 释放
}
```

### 2.2 语法规则

```
region_stmt ::= "region" IDENTIFIER "{" { statement } "}"
```

- **`region`** 关键字
- **`IDENTIFIER`** region 的名称（用于报错/调试）
- **`{ ... }`** region 体，内部是一个完整的 AST_BLOCK

### 2.3 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 具名/匿名 | 具名 `region Name` | 可读性、调试信息 |
| 嵌套 | 第一版不支持 | 减少语义复杂度 |
| 闭包逃逸 | 编译期禁止 | 避免 env 指针悬垂 |
| Arena 自动增长 | mmap/VirtualAlloc 分页 | 保持 bump 分配连续性 |
| 逃逸触发点 | `return` + 对外部变量 `=` 赋值 | 函数参数传值不触发（callee 同步使用） |

---

## 3. 语义

### 3.1 核心规则

1. **进入 region**：创建 bump allocator（`LsArena`），初始大小 64KB
2. **分配路由**：region 内的 `vec(T)` / `map(K,V)` / `string` / `struct(has_drop)` / `enum(has_drop)` / `new Struct` 的堆分配都从 arena 中 bump 分配
3. **释放路由**：region 内的 `free` 调用被拦截——指针属于 arena 范围则跳过（arena 整体释放），不属于则正常 free
4. **逃逸**：`return` 或对外部变量 `=` 赋值 region 内分配的值时，**自动深拷贝**出 arena（调用现有的 `emit_*_clone_val` 函数族）
5. **退出 region**：`__ls_arena_destroy(arena)`，O(1) 释放整个 arena

### 3.2 值与 arena 的关系

```
region Buffer {
    var v = vec(u8)       // v.data → arena 内存
    v.push(42)             // 扩容 → arena 内 realloc（bump alloc + memcpy）
    var s = string("hi")  // s.data → arena 内存
    var m = map(int,u8)   // m.buckets → arena 内存
    return v.len()        // i32 POD，不逃逸拷贝
}
// ← arena 整体释放，v.data / s.data / m.buckets 全部失效
```

### 3.3 逃逸拷贝什么时候触发

| 场景 | 触发逃逸拷贝？ | 说明 |
|------|---------------|------|
| `return v` 在 region 内 | ✅ | v 深拷贝出 arena |
| `outer = v` 在 region 内（outer 定义在 region 外） | ✅ | v 深拷贝出 arena |
| `f(v)` 在 region 内 | ❌ | f 同步使用，不逃逸 |
| `outer.inner = v.field` 在 region 内 | ✅ | 字段值深拷贝出 arena |
| `var local = v` 在 region 内（local 定义在 region 内） | ❌ | 同 arena 内，不逃逸 |
| `\|x\| body` 在 region 内 | ❌ 编译期错误 | 闭包禁止逃逸 |
| region 内返回字面量 `return 42` | ❌ | POD/字面量不涉及 arena 分配 |

### 3.4 逃逸拷贝的代价

逃逸拷贝复用已有的深拷贝机制：

| 类型 | 拷贝方式 | 代价 |
|------|---------|------|
| `int / f64 / bool / char` | 值拷贝（POD） | O(1) |
| `string (cap>0)` | `emit_string_clone_val` → `malloc + memcpy` | O(len) |
| `vec(T)` + 元素 | `emit_vec_clone_val` → `malloc + 逐元素 clone` | O(len * elem_clone_cost) |
| `map(K,V)` | `__ls_map_XX_clone` → `malloc buckets + 逐节点插入` | O(n) |
| `struct(has_drop)` | `emit_struct_clone_val` → 递归 clone 每个 has_drop 字段 | O(field_count) |
| `enum(has_drop)` | `emit_enum_clone_val` → 递归 clone payload | O(variant_payload_size) |

**不逃逸的值零代价**——arena 释放时只管整块，不管内部有哪些值。

---

## 4. Runtime 层

### 4.1 LsArena 结构体

```c
// runtime/builtins.c

typedef struct LsArena {
    char   *data;       // 内存块基址
    size_t  offset;     // 当前 bump 偏移
    size_t  capacity;   // 当前块总大小
    struct LsArena *next;   // 链表：arena 自动增长时串联
} LsArena;
```

### 4.2 API

```c
// 创建 arena，initial_size = 0 时使用默认值 64KB
LsArena *__ls_arena_create(size_t initial_size);

// Bump 分配，8 字节对齐
void *__ls_arena_alloc(LsArena *arena, size_t size);

// 判断 ptr 是否属于此 arena（用于 free 拦截）
bool __ls_arena_contains(LsArena *arena, void *ptr);

// 销毁整个 arena，遍历 next 链表，VirtualFree/munmap 每页
void __ls_arena_destroy(LsArena *arena);
```

### 4.3 自动增长策略

```
__ls_arena_alloc(arena, size):
    if arena->offset + size <= arena->capacity:
        ptr = arena->data + arena->offset
        arena->offset += align8(size)
        return ptr
    else:
        // 分配新页（默认 1MB，或 max(1MB, size)）
        new_block = VirtualAlloc(NULL, page_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)
        new_arena = malloc(sizeof(LsArena))
        new_arena->data = new_block
        new_arena->offset = 0
        new_arena->capacity = page_size
        new_arena->next = arena->next
        arena->next = new_arena
        // 在新 arena 中分配
        return __ls_arena_alloc(new_arena, size)
```

---

## 5. 编译阶段变更

### 5.1 Scanner

`src/token.h` → 新增 `TOKEN_REGION`
`src/scanner.c` → 关键字表新增 `{"region", 6, TOKEN_REGION}`

### 5.2 AST

`src/ast.h` → 新增 `AstNodeType::AST_REGION`

```c
// 在 AstNode union 中新增：
struct {
    char *name;                  // region 名称（owned）
    AstNode *body;               // AST_BLOCK
    // Checker 填充：
    int escape_count;
    struct {
        char *var_name;          // 逃逸到外部的变量名
        Type *type;              // 变量类型
        AstNode *site;           // 逃逸发生点（return/assign）
    } *escape_vars;
} region;
```

### 5.3 Parser

`src/parser.c` → `parse_statement` 中新增 `TOKEN_REGION` 分支：

```
region Buffer {
    stmt1
    stmt2
}
```

解析流程：consume `region` → consume `IDENTIFIER`（name）→ 调用 `parse_block` 解析 `{ ... }` → 构造 `AST_REGION { name, body = block }`。

### 5.4 Checker

`src/checker.c` → 新增 `check_region` 函数：

```
check_region(c, node):
    push_scope(c)
    c->current_region = node

    check_block(c, node->as.region.body)

    c->current_region = NULL
    pop_scope(c)
```

逃逸检测逻辑：

```
在 check_return 和 check_assign 中新增：
    if (c->current_region != NULL) {
        if (return/assign 的值引用了 region 作用域内的变量)
            → 将该变量加入 c->current_region->escape_vars
    }

在 check_closure 中新增：
    if (c->current_region != NULL)
        → error("closure cannot escape region")
```

### 5.5 Codegen

`src/codegen.h` → `CodegenContext` 新增：

```c
struct {
    LLVMValueRef arena_ptr;     // LsArena* 当前 arena
    const char   *name;         // region 名称
    AstNode      *region_node;  // AST_REGION 节点
    bool          active;       // 是否在 region 内
} current_region;
```

#### 入口 (`codegen_region_enter`)

```llvm
%arena = call @__ls_arena_create(i64 65536)
```

```c
// 声明 arena 函数
ctx->arena_alloc_fn = LLVMGetNamedFunction(ctx->module, "__ls_arena_alloc");
ctx->arena_contains_fn = LLVMGetNamedFunction(ctx->module, "__ls_arena_contains");
ctx->arena_destroy_fn = LLVMGetNamedFunction(ctx->module, "__ls_arena_destroy");

// 设置 region 状态
ctx->current_region.active = true;
ctx->current_region.arena_ptr = %arena;
```

#### `cg_emit_alloc` 改造

```c
LLVMValueRef cg_emit_alloc(CodegenContext *ctx, LLVMValueRef size, ...) {
    if (ctx->current_region.active) {
        return LLVMBuildCall2(ctx->builder, ...,
            ctx->arena_alloc_fn,
            [ctx->current_region.arena_ptr, size], 2, "arena.p");
    }
    // memcheck / malloc 原路径
}
```

#### `cg_emit_free` 改造

```c
static void cg_emit_free(CodegenContext *ctx, LLVMValueRef ptr, ...) {
    if (ctx->current_region.active) {
        // 生成: if (__ls_arena_contains(arena, ptr)) { /* skip */ } else { free(ptr) }
        // 采用分支：大部分 arena 内 ptr → skip，小部分外部 ptr → free
        return;
    }
    // memcheck / free 原路径
}
```

#### 新增 `cg_emit_realloc`

```c
LLVMValueRef cg_emit_realloc(CodegenContext *ctx, LLVMValueRef ptr,
                              LLVMValueRef old_size, LLVMValueRef new_size, ...) {
    if (ctx->current_region.active) {
        // new_ptr = arena_alloc(arena, new_size)
        // if (old_size > 0) memcpy(new_ptr, ptr, min(old_size, new_size))
        // return new_ptr
        // 不释放旧 ptr（arena 整体释放）
    }
    // realloc 原路径
}
```

#### 逃逸拷贝

在 region 体代码生成后、`arena_destroy` 前发射：

```c
for each escape_var in node->as.region.escape_vars:
    if (escape_var.type 是 has_drop / string / vec / map) {
        // 调用 emit_*_clone_val 深拷贝出 arena
        LLVMValueRef cloned = emit_clone_value(ctx, arena_val, escape_var.type);
        store_to_outer_scope(cloned, escape_var.var_name);
    } else {
        // POD 类型直接拷贝值
        store_to_outer_scope(arena_val, escape_var.var_name);
    }
```

#### 出口 (`codegen_region_exit`)

```llvm
call @__ls_arena_destroy(%arena)
```

```c
ctx->current_region.active = false;
ctx->current_region.arena_ptr = NULL;
```

---

## 6. Phase 0：前置重构（可独立先行）

在实现 region 语法前，先将所有绕过 `cg_emit_alloc` / `cg_emit_free` 的直接 `malloc`/`realloc`/`free` 改为走中心函数。

### 6.1 改造目标

| codegen.c 行号 | 当前操作 | 目标 |
|:---------------|:---------|:-----|
| 13677 | `malloc` for `new Struct` | `cg_emit_alloc` |
| 7197 | `malloc` for vec 字面量初始化 | `cg_emit_alloc` |
| 8425~8437 | `realloc` for vec.extend grow | `cg_emit_realloc` |
| 7477~7482 | `realloc` for vec.push grow | `cg_emit_realloc` |
| 8792+ | `malloc` for vec.resize | `cg_emit_alloc` |
| 9361 | `malloc` for vec.resize(n) | `cg_emit_alloc` |
| 9449+ | `realloc` for vec.shrink_to_fit | `cg_emit_realloc` |
| 9764 | `malloc` for vec map/filter | `cg_emit_alloc` |
| 10101 | `malloc` for vec.slice | `cg_emit_alloc` |
| 19989 | `calloc` for map buckets | `cg_emit_calloc` |
| 18427 | `malloc` for string.replace | `cg_emit_alloc` |
| 18582 | `malloc` for string.split | `cg_emit_alloc` |
| 18771 | `malloc` for string.join | `cg_emit_alloc` |
| 18934 | `malloc` for string.lines | `cg_emit_alloc` |
| 7423 | `free` for vec.scope_drop | `cg_emit_free` |

### 6.2 新增 `cg_emit_calloc`

```c
LLVMValueRef cg_emit_calloc(CodegenContext *ctx, LLVMValueRef num,
                              LLVMValueRef size, const char *kind, int line, int col) {
    // 内部调用 cg_emit_alloc(num * size) + memset(ptr, 0, num * size)
}
```

### 6.3 Phase 0 的独立价值

- Memcheck 覆盖率提升（之前 direct malloc 不经过 `ls_mc_alloc` → 不被追踪）
- 分配/释放统一路由出口，region 功能只需在 `cg_emit_alloc`/`cg_emit_free`/`cg_emit_realloc` 三处加 if 分支
- 代码更一致，便于后续维护

---

## 7. 与现有功能的交互矩阵

| 功能 | 交互 | 处理 |
|------|------|------|
| **string** | `data` 走 arena | arena_alloc；逃逸时 `emit_string_clone_val` |
| **string 字面量 (cap=0)** | 不走 heap | 不受影响 |
| **vec(T)** | `data` 缓冲区走 arena | arena_alloc；grow 用 `cg_emit_realloc(arena)` |
| **map(K,V)** | buckets + nodes 走 arena | 同上 |
| **struct(has_drop)** | 内部字段递归走 arena | `emit_struct_clone_val` 逃逸时触发 |
| **enum(has_drop)** | box payload 走 arena | `emit_enum_clone_val` 逃逸时触发 |
| **Block(闭包)** | env 走 arena | **编译期错误**（禁止逃逸） |
| **new StructName** | heap struct 走 arena | `cg_emit_alloc` → arena 分配 |
| **move-elision** | 不影响 | region 内 move 语义不变 |
| **memcheck** | arena 整体跟踪 | arena create/destroy 作为两条记录；内部不逐条追踪 |
| **借用参数 (&T)** | 传入指针可能指向 arena | 用户保证生命周期（同现有 by-ref 约定） |
| **模块函数** | region 内可自由调用 | 函数参数不走 arena |
| **for/while/if/match** | 内部嵌套 region | 正常运行 |
| **try 早返** | 从 region 内 try 返回 | 触发逃逸拷贝 |
| **cg_push_temp_drop** | temp 分配走 arena | 通过 `cg_emit_alloc` 自动路由 |
| **闭包 env drop** | region 内无闭包 | 不涉及 |

---

## 8. 实现阶段与工作量

| Phase | 内容 | 文件 | LOC | 验证 |
|-------|------|------|-----|------|
| 0 | 重构 direct malloc/realloc/free → cg_emit_alloc/free/realloc | codegen.c | ~200 | memcheck 覆盖率提升；现有测试全绿 |
| 1 | Runtime arena 分配器 | runtime/builtins.c | ~80 | 独立 C 单元测试 |
| 2 | Scanner + Parser + AST | token.h, scanner.c, parser.c, ast.h | ~50 | scanner 测试 + parse 测试 |
| 3 | Checker 逃逸检测 | checker.c | ~120 | checker 测试 |
| 4 | Codegen arena 路由 + 逃逸拷贝 | codegen.h, codegen.c | ~350 | JIT + AOT + memcheck 三重验证 |
| 5 | 测试 | tests/samples/region_*.ls | ~250 | ctest 全绿 |
| **总计** | | **14 文件** | **~1050** | |

### 8.1 测试清单

| 测试文件 | 验证点 |
|---------|--------|
| `region_basic.ls` | region 内 vec 分配 + push + 读，0 leak |
| `region_escape_return.ls` | `return v` 触发逃逸拷贝，外部的值是 arena 的独立副本 |
| `region_escape_assign.ls` | `outer = inner_var` 逃逸拷贝 |
| `region_string.ls` | string arena 分配 + 逃逸拷贝正确 |
| `region_struct.ls` | has_drop struct 递归逃逸拷贝 |
| `region_vec_of_struct.ls` | vec(struct) 在 arena 内 + 整体逃逸拷贝 |
| `region_map.ls` | map string→int 在 arena 内 + 逃逸拷贝 |
| `region_no_escape.ls` | 不逃逸的值在 arena 内修改 + arena 退出后内存安全 |
| `region_block_forbidden.ls` | `\|x\| body` 在 region 内 → 编译错误 |
| `region_large_vec.ls` | arena 自动增长（>64KB），正确性 + memcheck |
| `region_nested_error.ls` | 嵌套 region → 编译错误 |
| `region_for_loop.ls` | for 循环内使用 region（每个迭代独立 arena） |
| `region_try_escape.ls` | try 从 region 内早返 |

---

## 9. 未来演进方向

| 版本 | 改进 | 动机 |
|------|------|------|
| V1 | 单层 region + 逃逸拷贝 | 本设计 |
| V2 | 嵌套 region | 子 region 值可向上流动（逃逸拷贝到父 arena） |
| V3 | region 作为函数参数 | `fn f(r: region)` 允许调用者传入 arena |
| V4 | 编译期逃逸静态检查 | 减少运行时拷贝，证明某些值零成本不逃逸 |
| V5 | region 内的 Block 支持 | 需解决 env 生命周期问题 |

---

## 10. 附录：设计决策记录

| 日期 | 决策 | 理由 |
|------|------|------|
| 2026-06-06 | region + 逃逸拷贝（非纯线性 region） | LS 非线性类型系统，逃逸拷贝安全且实用 |
| 2026-06-06 | 具名 region | 调试信息、报错清晰 |
| 2026-06-06 | 第一版不支持嵌套 | 减少交互复杂度 |
| 2026-06-06 | 闭包禁止逃逸出 region | 避免 env 指针悬垂，用户明确要求 |
| 2026-06-06 | Arena 自动增长（mmap/VirtualAlloc） | 保持 bump 连续性，用户明确要求 |
| 2026-06-06 | 逃逸触发仅限 return 和外部赋值 | 函数实参不触发（调用者同步使用，安全） |
