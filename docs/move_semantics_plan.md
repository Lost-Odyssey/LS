# LS Move 语义系统 — 完整实现计划

> 本文档是独立的实现指南，可在新 session 中直接参考执行。
> 核心设计决策已同步固化于 `CLAUDE.md §8`。

---

## 一、背景与核心决策回顾

### 1.1 LsString 内存三态

```c
// cap == 0  → Static：data 指向 .rodata，永不 free，永不 move
// cap > 0   → Owned：malloc 分配，当前变量持有所有权
// cap == -1 → Moved：所有权已转移，变量不可再用
```

### 1.2 MAYBE_MOVED = MOVED = 死亡状态（不可更改）

变量一旦存在任意执行路径导致 move，该变量即进入死亡状态，无论后续走哪条路径。
这使得状态转移单调（LIVE → MAYBE_MOVED → MOVED），2-pass 循环分析足以完整处理控制流。

### 1.3 Move 触发操作

- `vec.push(s)`（cap > 0 时）
- `map.set(s, v)` / `map.set(k, s)`（cap > 0 时）
- `string t = s`（直接变量赋值，cap > 0 时）
- 函数调用参数传递（Phase B 后实现）

Static string（cap == 0）**永不** move。

---

## 二、当前实现状态（Codegen 层）

以下功能已在 `codegen.c` 中实现，是 **运行时** 保护，不是编译期检查：

| 功能 | 状态 | 位置 |
|------|------|------|
| `mark_string_moved(ctx, alloca, reason)` — cap>0 才标记 | ✅ 已实现 | codegen.c ~line 414 |
| `vec.push` — static string 不标记 moved | ✅ 已实现 | codegen.c |
| `map.set` — moved key 运行时 warning+nop | ✅ 已实现 | codegen.c ~line 10553 |
| `MAP_EMIT_COPY_KEY` — static string 不深拷贝 | ✅ 已实现 | codegen.c ~line 10296 |
| `emit_string_free` — cap>0 才 free | ✅ 已实现 | codegen.c |
| `emit_scope_cleanup` — 跳过 moved (cap<0) | ✅ 已实现 | codegen.c |

**尚未实现**：`checker.c` 中的编译期 move 检查（Phase A / Phase B）。

---

## 三、Phase A：线性 Move 检查（checker.c）

### 3.1 目标

在顺序语句序列中检查 move 违规，不处理控制流分支。覆盖以下场景：

```ls
string s = "hello".upper()
v.push(s)           // s: MOVED
print(s)            // ❌ 编译错误：use after move

string t = "world".upper()
t = "new".upper()   // ❌ 编译错误：re-assign after move（t 已被赋值 = 已 owned，但如果 t 已 moved 则报错）
```

### 3.2 数据结构

在 `checker.c` 中新增（或在 `checker.h` 中新增如果需要跨文件）：

```c
/* checker.c 内部，不需要对外暴露 */

typedef enum {
    MOVE_STATE_LIVE,         /* 变量可用 */
    MOVE_STATE_MAYBE_MOVED,  /* 存在路径导致 move（控制流分析用）*/
    MOVE_STATE_MOVED,        /* 确定已 move，不可再用 */
} MoveState;

typedef struct {
    const char *name;      /* 变量名（指向 symbol 的 name，不拥有） */
    MoveState   state;
    int         move_line; /* 发生 move 的行号，用于错误提示 */
} MoveEntry;

typedef struct {
    MoveEntry *entries;
    int        count;
    int        capacity;
} MoveTable;

/* 全局（函数级别）move table，每进入一个函数体重置 */
static MoveTable g_move_table;
```

### 3.3 辅助函数

```c
/* 初始化/重置 move table（每个函数体开始时调用） */
static void move_table_reset(MoveTable *mt);

/* 注册一个新变量为 LIVE（var_decl 时调用） */
static void move_table_register(MoveTable *mt, const char *name);

/* 将变量标记为 MOVED，记录行号 */
static void move_table_mark_moved(MoveTable *mt, const char *name, int line);

/* 将变量标记为 MAYBE_MOVED（Phase B 用） */
static void move_table_mark_maybe(MoveTable *mt, const char *name, int line);

/* 查询变量状态 */
static MoveState move_table_query(MoveTable *mt, const char *name);

/* 检查某次使用是否合法，不合法则输出错误 */
static bool checker_check_movable_use(Checker *c, const char *name, int line, int col);
```

### 3.4 需要修改的 checker.c 函数

#### 3.4.1 `check_fn_decl`（函数体入口）

```c
// 在函数体检查开始前：
move_table_reset(&g_move_table);
// 注册所有参数为 LIVE
for (int i = 0; i < node->as.fn_decl.param_count; i++) {
    // 只注册 string 类型参数
    if (type_is_movable(param_types[i])) {
        move_table_register(&g_move_table, node->as.fn_decl.param_names[i]);
    }
}
```

#### 3.4.2 `check_var_decl`（变量声明）

```c
// 声明新变量后，注册到 move table
if (type_is_movable(resolved_type)) {
    move_table_register(&g_move_table, node->as.var_decl.name);
}

// 如果右侧是一个标识符（直接赋值 string t = s），则触发 move
if (node->as.var_decl.init && node->as.var_decl.init->kind == AST_IDENT) {
    const char *src = node->as.var_decl.init->as.ident.name;
    if (move_table_query(&g_move_table, src) == MOVE_STATE_LIVE) {
        // 只对 owned string 做 move（static 不做，但 checker 阶段无法知道 cap 值）
        // 保守策略：对任何 string 变量赋值都标记 move
        move_table_mark_moved(&g_move_table, src, node->line);
    }
}
```

> **注**：checker 阶段无法知道 cap 是否为 0（那是运行时值）。
> 保守策略：所有 string 变量赋值都视为 move。
> 字符串字面量（AST_STRING_LIT）不触发 move（它们没有变量身份）。

#### 3.4.3 `check_call`（函数调用）

检测以下已知 move 触发调用：

```c
// vec.push(s) — 检查参数 s 是否是 string 变量
if (is_vec_push_call(node)) {
    AstNode *arg = node->as.call.args[0];
    if (arg->kind == AST_IDENT) {
        const char *name = arg->as.ident.name;
        checker_check_movable_use(c, name, arg->line, arg->column);  // 先检查
        if (type_is_movable(arg->resolved_type)) {
            move_table_mark_moved(&g_move_table, name, arg->line);   // 再标记
        }
    }
}

// map.set(k, v) — 检查 key 和 value
if (is_map_set_call(node)) {
    // 对 string key/value 做同样处理
}
```

#### 3.4.4 `check_ident`（变量使用）

```c
// 在返回标识符的解析类型之前，检查 move 状态
MoveState ms = move_table_query(&g_move_table, node->as.ident.name);
if (ms == MOVE_STATE_MOVED || ms == MOVE_STATE_MAYBE_MOVED) {
    checker_error(c, node->line, node->column,
        "use of moved variable '%s' (moved at line %d)",
        node->as.ident.name, moved_at_line);
    // 继续（不 panic），使用 LIVE 状态假设后续检查
}
```

#### 3.4.5 `check_assign`（赋值语句）

```c
// s = expr — 对已 moved 变量的 re-assign 也报错
if (target->kind == AST_IDENT && type_is_movable(target->resolved_type)) {
    MoveState ms = move_table_query(&g_move_table, target->as.ident.name);
    if (ms == MOVE_STATE_MOVED || ms == MOVE_STATE_MAYBE_MOVED) {
        checker_error(c, node->line, node->column,
            "re-assignment to moved variable '%s' is not allowed",
            target->as.ident.name);
    }
    // 如果右侧是另一个 string 变量，也触发 move（同 var_decl）
    if (rhs->kind == AST_IDENT && type_is_movable(rhs->resolved_type)) {
        checker_check_movable_use(c, rhs->as.ident.name, rhs->line, rhs->column);
        move_table_mark_moved(&g_move_table, rhs->as.ident.name, rhs->line);
    }
}
```

### 3.5 `type_is_movable` 辅助函数

```c
static bool type_is_movable(Type *t) {
    if (!t) return false;
    return t->kind == TYPE_STRING
        || t->kind == TYPE_VECTOR
        || t->kind == TYPE_MAP
        || (t->kind == TYPE_STRUCT && struct_has_drop(t));
}
```

### 3.6 Phase A 预估工作量

- 新增约 150 行数据结构 + 辅助函数
- 修改约 5 个检查函数，每个约 20-40 行
- 总计约 **300-350 行** 新增/修改
- 新增测试用例约 10 个（`test_types.c`）
- 新增样例文件：`samples/move_test.ls`

---

## 四、Phase B：控制流 Move 检查（checker.c）

### 4.1 目标

在 Phase A 基础上处理控制流分支，包括：
- `if/else` 分支：两个分支的 move 状态需要合并
- `while` / `for` 循环：循环体内的 move 需要 2-pass 分析

### 4.2 MoveTable 快照与合并

```c
/* 保存当前 move table 的快照 */
static MoveTable move_table_snapshot(const MoveTable *mt);

/* 将两个快照合并，取"更坏"状态（MOVED > MAYBE_MOVED > LIVE） */
static void move_table_merge(MoveTable *dst, const MoveTable *a, const MoveTable *b);

/* 将 src 中比 dst 更差的状态升级到 MAYBE_MOVED */
static void move_table_elevate_to_maybe(MoveTable *dst, const MoveTable *src);
```

### 4.3 if/else 分支处理

```c
// check_if_stmt 中：

MoveTable before = move_table_snapshot(&g_move_table);

// 检查 then 分支
MoveTable state_then = move_table_snapshot(&g_move_table);
// 切换到 then 分支状态，检查 then_block
check_node(c, node->as.if_stmt.then_block);
MoveTable after_then = move_table_snapshot(&g_move_table);

if (node->as.if_stmt.else_block) {
    // 恢复到 before，检查 else 分支
    g_move_table = before;
    check_node(c, node->as.if_stmt.else_block);
    MoveTable after_else = move_table_snapshot(&g_move_table);
    
    // 合并两个分支的状态
    // 规则：MOVED 在两个分支 → MOVED；只有一个分支 → MAYBE_MOVED
    move_table_merge(&g_move_table, &after_then, &after_else);
} else {
    // 没有 else 分支：then 中 MOVED 的变量变为 MAYBE_MOVED
    move_table_elevate_to_maybe(&g_move_table, &after_then);
}
```

**合并规则详解**：

| then 分支 | else 分支 | 合并结果      |
|-----------|-----------|---------------|
| LIVE      | LIVE      | LIVE          |
| MOVED     | MOVED     | MOVED         |
| MOVED     | LIVE      | MAYBE_MOVED   |
| LIVE      | MOVED     | MAYBE_MOVED   |
| MAYBE_MOVED | 任意    | MAYBE_MOVED   |

### 4.4 循环的 2-Pass 分析

#### 4.4.1 原理

循环体可能执行 0 次或多次，且变量在循环体末尾可能被重新赋值（但按 MAYBE_MOVED=MOVED 规则，这已是错误）。

2-pass 算法：
- **Pass 1（发现）**：扫描循环体，找出所有被 move 的变量，不报错
- **Pass 2（检查）**：将 Pass 1 发现的所有 moved 变量预先标记为 MAYBE_MOVED，然后重新检查循环体，此时这些变量在使用时就会报错

#### 4.4.2 实现

```c
// check_while_stmt / check_for_c_stmt / check_for_stmt 通用逻辑：

MoveTable before_loop = move_table_snapshot(&g_move_table);

// === Pass 1：发现模式（静默，只收集 moved 变量）===
bool old_silent = c->silent_move_errors;
c->silent_move_errors = true;  // 新增 flag，suppresses move error reporting

check_node(c, node->body);  // 扫描循环体
MoveTable after_pass1 = move_table_snapshot(&g_move_table);

c->silent_move_errors = false;

// === 准备 Pass 2：从 before_loop 恢复，但把 Pass 1 中新增的 MOVED 状态改为 MAYBE_MOVED ===
g_move_table = before_loop;
for each entry in after_pass1 where state == MOVED and before_loop[entry.name] == LIVE:
    move_table_mark_maybe(&g_move_table, entry.name, entry.move_line);

// === Pass 2：正式检查（报错）===
check_node(c, node->body);
MoveTable after_pass2 = move_table_snapshot(&g_move_table);

// 循环后状态：原本 LIVE 且在循环中可能被 move 的变量，变为 MAYBE_MOVED
move_table_elevate_to_maybe(&g_move_table, &after_pass2);
```

**示例验证**：

```ls
string s = get()     // s: LIVE

while cond {
    // Pass 1: 发现 s 在 if cond2 分支中被 move
    // Pass 2: s 预标记为 MAYBE_MOVED
    if cond2 {
        v.push(s)    // Pass 2: checker_check_movable_use → 不报错（s 在这条路径上是 LIVE，
                     //         但 MAYBE_MOVED 会触发错误... 等等
```

> **修正**：按照 MAYBE_MOVED = 死亡规则，Pass 2 中 `v.push(s)` 也会报错，因为 s 已被预标记为 MAYBE_MOVED。
> 这正是设计意图：一旦循环存在移动路径，整个变量在循环中都不可用。
> 
> 用户必须在循环外先拷贝或使用完：
> ```ls
> string s = get()
> // 方案1：循环外处理
> if cond2 { v.push(s) }  // s: MOVED，不再进入循环
> // 方案2：使用借用（未来功能）
> while cond { print(s) } // &s 借用，不 move
> v.push(s)               // 循环后 move
> ```

### 4.5 `silent_move_errors` flag

在 `Checker` 结构体中新增：

```c
typedef struct Checker {
    // ... 现有字段 ...
    bool silent_move_errors;  /* Pass 1 中不报告 move 错误，只收集状态 */
} Checker;
```

`checker_error` 中：
```c
if (c->silent_move_errors && is_move_error) return; // 静默跳过
```

### 4.6 Phase B 预估工作量

- 新增快照/合并/提升辅助函数：约 80 行
- 修改 `check_if_stmt`：约 40 行
- 修改 3 种循环检查函数（`check_while_stmt`, `check_for_c_stmt`, `check_for_stmt`）：各约 50 行
- `Checker` 结构体新增 `silent_move_errors` 字段：5 行
- 总计约 **350-400 行** 新增/修改
- 新增测试用例约 15 个（`test_types.c`）
- 扩充 `samples/move_test.ls`

---

## 五、Struct 的 Move 语义（Phase A + B）

### 5.1 适用范围

以下 struct 需要 move 跟踪：

```c
static bool struct_has_drop(Type *t) {
    // 1. 含 string 字段（编译器自动生成 __drop）
    for each field in t->as.strukt.fields:
        if (field.type->kind == TYPE_STRING) return true;
    // 2. 含嵌套的需要 drop 的 struct
    for each field in t->as.strukt.fields:
        if (field.type->kind == TYPE_STRUCT && struct_has_drop(field.type)) return true;
    // 3. 用户定义了 __drop 方法
    if (struct_has_user_drop_method(t)) return true;
    return false;
}
```

### 5.2 Struct Move 触发条件

```ls
struct Person { string name; int age; }

Person p
p.name = "Alice"

Person q = p    // p: MOVED（struct 赋值转移所有权）
print(p.name)   // ❌ use after move

// vec.push(p) — 同样触发 move
vec(Person) people
people.push(p)  // p: MOVED
```

### 5.3 字段赋值不触发 move

```ls
Person p
p.name = "Alice"  // 不触发 p 的 move，只是字段赋值
p.name = "Bob"    // 旧 name 自动 free（已有 codegen 实现），不触发 p 的 move
print(p.name)     // ✅ OK
```

### 5.4 Struct Phase A 实现要点

`type_is_movable` 已包含 `TYPE_STRUCT && struct_has_drop`，因此 Phase A 对 struct 的支持与 string 完全相同，**不需要额外代码**，只需：

1. `check_var_decl`：对 struct 类型变量也注册到 move table
2. `check_ident`：同样检查 move 状态
3. `check_assign`（target 是 struct 变量）：同样检查并标记

但有一个区别：struct 的**字段赋值**（`p.name = ...`，target 是 `AST_FIELD`）**不**触发 `p` 的 move。
需要确保 `check_assign` 中只对 `target->kind == AST_IDENT` 的情况处理 struct move。

### 5.5 Struct Phase B

与 string Phase B 完全相同的 2-pass 循环算法，无需额外处理。

### 5.6 Struct 预估工作量

如果 string Phase A/B 已实现，struct 支持几乎是免费的（因 `type_is_movable` 已统一处理）。
只需确认 `struct_has_drop` 逻辑正确，预估约 **50-80 行** 新增代码。

---

## 六、`__move` 内置函数实现

### 6.1 Scanner/Parser 层

`__move` 作为内置标识符（不是关键字），在 checker 层特殊处理：

```c
// checker.c check_call 中：
if (callee_name == "__move" && arg_count == 1) {
    AstNode *arg = node->as.call.args[0];
    if (arg->kind != AST_IDENT) {
        checker_error(c, arg->line, arg->column,
            "__move() requires a variable, not an expression");
        return type_void();
    }
    // 检查变量状态
    checker_check_movable_use(c, arg->as.ident.name, arg->line, arg->column);
    // 标记为 MOVED
    move_table_mark_moved(&g_move_table, arg->as.ident.name, node->line);
    // __move 的类型与参数类型相同（透明传递）
    return arg->resolved_type;
}
```

### 6.2 Codegen 层

```c
// codegen.c AST_CALL 中：
if (strcmp(fn_name, "__move") == 0) {
    // 完全 no-op：直接返回参数的 codegen 结果
    // 不生成任何 IR，不调用 mark_string_moved（checker 已处理语义）
    return codegen_node(ctx, node->as.call.args[0]);
}
```

### 6.3 使用示例

```ls
fn transfer(vec(string) &v, string s) {   // 未来借用语法
    v.push(__move(s))   // 显式标记 s 被移入 vec
}

string name = "Alice".upper()
transfer(v, __move(name))  // 显式传递所有权
// name 现在 MOVED，后续使用会被 checker 报错
```

---

## 七、借用语义评估

### 7.1 前提条件

借用语义必须在 **Phase B 完整实现之后** 才考虑，因为借用检查依赖精确的 move 状态跟踪。

### 7.2 设计方案（简化版，无 lifetime 标注）

```ls
// 只读借用：&T 参数，调用方变量保持 LIVE
fn print_name(string &s) {
    print(s)    // 只读，不能 move s，不能 s = ...
}

string name = "Alice".upper()
print_name(name)   // name 仍然 LIVE
v.push(name)       // 现在才 MOVED
```

**Checker 规则**：
- `&T` 参数：调用时不标记实参为 MOVED
- 函数体内：`&T` 参数不能被 move（`vec.push(s)` 等操作被拒绝）
- 函数体内：`&T` 参数不能被 re-assign

**Codegen 实现**：
- `&string` 参数在 LLVM 层传递 `LsString*`（指针）而非 `LsString`（值）
- 被调函数通过指针读取字段（GEP），不拥有内存，不负责 free

### 7.3 可变借用（暂不实现）

可变借用（`&mut T`）引入别名问题（两个可变引用指向同一内存），复杂度大幅增加。
LS 的设计哲学是简洁，因此暂不实现可变借用，用户可以通过指针（`*T`）或返回新值来处理。

### 7.4 借用预估工作量

- Parser：`&` 类型语法，约 30 行
- Checker：借用参数状态标记，约 100 行
- Codegen：指针传递适配，约 80 行
- 总计约 **200-250 行**，复杂度中等

---

## 八、实现顺序与检查清单

### 阶段一：String Phase A（建议首先实现）

- [ ] 在 `checker.c` 中新增 `MoveState` 枚举和 `MoveTable` 结构体
- [ ] 实现 `move_table_reset / register / mark_moved / query` 函数
- [ ] 实现 `checker_check_movable_use` 函数
- [ ] 实现 `type_is_movable` 函数
- [ ] 修改 `check_fn_decl`：函数入口重置 move table
- [ ] 修改 `check_var_decl`：注册新变量，检测 string-to-string 赋值 move
- [ ] 修改 `check_call`：检测 vec.push / map.set 触发的 move
- [ ] 修改 `check_ident`：检查 move 状态
- [ ] 修改 `check_assign`：检查 re-assign 到 moved 变量，检测 rhs move
- [ ] 新增测试：`test_types.c` 中 10 个用例
- [ ] 新增样例：`samples/move_test.ls`

### 阶段二：String Phase B

- [ ] 新增 `move_table_snapshot / merge / elevate_to_maybe` 函数
- [ ] 在 `Checker` 结构体中新增 `silent_move_errors` 字段
- [ ] 修改 `check_if_stmt`：快照 + 检查 + 合并
- [ ] 修改 `check_while_stmt`：2-pass 分析
- [ ] 修改 `check_for_c_stmt`：2-pass 分析
- [ ] 修改 `check_for_stmt`（foreach）：2-pass 分析
- [ ] 新增测试：`test_types.c` 中 15 个用例
- [ ] 扩充 `samples/move_test.ls`

### 阶段三：Struct Phase A/B

- [ ] 实现 `struct_has_drop` 函数（或确认已存在）
- [ ] 确认 `type_is_movable` 正确处理 struct
- [ ] 确认 `check_assign` 对 `AST_FIELD` target 不误触发 struct move
- [ ] 新增测试：`test_types.c` 中 8 个用例
- [ ] 新增样例：扩充 `samples/move_test.ls`

### 阶段四：`__move` 内置函数

- [ ] `checker.c check_call` 中识别 `__move` 调用
- [ ] `codegen.c AST_CALL` 中 `__move` no-op 处理
- [ ] 新增测试：3-5 个用例

### 阶段五：借用语义（Phase B 完成后）

- [x] Parser：解析 `&T` 类型（仅函数参数位置）
- [x] Checker：借用参数状态标记，禁止在借用参数上 move / __move / 再赋值
- [x] Codegen：`&string` 复用 string 的 pass-by-value ABI（cap 字段承担借用标记）
- [x] 新增测试（见下文 §十一）

---

## 十一、Phase 5：`&string` 只读借用 — 实现记录（2026-04）

### 11.1 已完成

**类型系统**：
- 新增 `TYPE_REFERENCE` TypeKind（types.h / types.c），包含 `type_clone / type_free / type_equals / type_name`（打印 `&T`）。
- 新增 `TYPE_NODE_REFERENCE` AST 节点（ast.h / ast.c）。
- 符号表新增 `Symbol.is_borrow` 标志（symtable.h / symtable.c）。

**Parser**：
- `parse_type()` 识别前缀 `&`，构造 `TYPE_NODE_REFERENCE`。仅在参数类型上下文中有效——变量声明位置使用 `&string` 会被解析器当作表达式解析并报 "expected expression"（当前可接受的限制，因为借用只在参数位置有意义）。

**Checker**：
- `resolve_type_node` 处理 `TYPE_NODE_REFERENCE`，**仅接受 `&string`**（pointee 必须是 TYPE_STRING），其它 pointee 报错 "only &string is implemented"。
- `type_assignable` 扩展：
  - `&T ← T`：实参 T 可自动"降级"为借用（调用方无需写 `&`）
  - `T ← &T`：借用可无缝透传给需要 `&T` 的内层函数（re-borrow）
- `checker_try_mark_moved`：若 symbol 有 `is_borrow`，直接 early-return（借用不传递所有权，不能被 move）。
- 新增 `checker_reject_borrow_move(c, arg, what)` 辅助函数，在下列 move 位点报错：
  - `vec.push` / `vec.insert`
  - `map.set` key / value
  - `__move(borrow)` 专门分支（错误信息："cannot __move(): variable 'x' is a read-only borrow"）
- `AST_ASSIGN`：target IDENT 若有 `is_borrow` 立即报错（借用不可被再赋值）。
- 函数参数解析 3 个位点（`check_fn_decl` / `check_pass` / `check_impl_decl`）统一做 `&T → T` 的 unwrap：符号表里登记裸 T 类型，但打上 `is_borrow = true`，以便后续所有形参使用都能正确做类型匹配并被 move 检查跳过。

**Codegen**：
- `type_to_llvm(TYPE_REFERENCE)` 直接 forward 到 pointee 的 LLVM 类型（即 LsString by-value struct）——借用与 owned 共用同一 ABI。
- `codegen_fn_decl` 在 alloca / 注册 scope 时对 `TYPE_REFERENCE` 形参 unwrap，保证下游访问成员（`.length` 等）不报 "field access on non-struct type"。

### 11.2 ABI 设计要点

当前 `&string` **不是** 指针 ABI：实参以 LsString by-value（3 字段：data/len/cap）传入被调函数，而调用方自身的 alloca 不被 move。这是可行的，因为：

1. LS 既有的 string 参数传递**本来**就走 by-value（即使未来要优化为指针传递也只影响 codegen，不影响语义）。
2. Callee 收到一份"cap 字段被视为 0"（borrow 不拥有所有权）的副本；callee 内部不允许任何 move/修改。
3. Caller 的 alloca 永远不被 move（checker 静态保证）。

因此 Phase 5 对 codegen 的侵入极小——借用在语义上是**编译期注解**，不改变运行时数据表示。

### 11.3 测试矩阵（tests/samples/borrow_*.ls）

| 文件 | 场景 | 预期结果 |
|------|------|----------|
| `borrow_basic_test.ls` | 静态串 + owned 串作 `&string` 实参，调用后原变量仍可用 | ✅ 运行成功 |
| `borrow_caller_live_test.ls` | 多次借用后，caller 变量仍 LIVE，之后真正 move 到 vec | ✅ 运行成功 |
| `borrow_chain_test.ls` | 借用再传给另一个 `&string` 函数（re-borrow） | ✅ 运行成功 |
| `borrow_methods_test.ls` | 在借用上调用 `.upper() / .contains()` 等方法，返回新 owned | ✅ 运行成功 |
| `borrow_neg_move.ls` | 试图 `vec.push(borrow)` | ✅ 报错："cannot move into vec: variable 's' is a read-only borrow" |
| `borrow_neg_assign.ls` | 试图对借用形参再赋值 | ✅ 报错："cannot assign to read-only borrow 's'" |
| `borrow_neg_move_explicit.ls` | 试图 `__move(borrow)` | ✅ 报错："cannot __move(): variable 's' is a read-only borrow" |
| `borrow_neg_var_decl.ls` | 试图 `&string x = owned` 作变量声明 | ✅ 解析阶段即拒绝（expected expression） |

### 11.4 未完成 / 暂不支持

- **`&mut T`**：可变借用未实现。当前所有 `&T` 都是只读（等价 Rust `&T`，不是 `&mut`）。
- **`&T` 仅限 string**：`resolve_type_node` 主动拒绝 `&vec(T)` / `&map(K,V)` / `&struct` 等非 string 借用。
- **`&T` 仅限函数参数位置**：变量声明、struct 字段、返回类型、`impl` 方法返回值不支持 `&T`。
- **auto-borrow 与 owned 重载**：同名函数不能同时存在 `(string)` 与 `(&string)` 两个版本——LS 尚无重载；实参会按 `type_assignable` 规则自动匹配第一个兼容签名。
- **借用的"借出范围"运行时跟踪**：因为借用不改变内存布局，也没有可变别名（只读），当前无需实现借用生命期 / NLL 风格的静态检查。

### 11.5 下一步建议

**优先级 1 — 将借用扩展到其它类型**（按成本递增）：

1. **`&struct`**：pass-by-pointer ABI，callee 内 field read 走 GEP；禁止在 callee 内对字段赋值 / 调用含 move 行为的方法。应在实现前先决定"借用受体的成员 move"语义（当前设计是：借用者本身死亡 = 所有字段不可 move）。
2. **`&vec(T)` / `&map(K,V)`**：天然是 pointer ABI（vec/map 头就是指针结构），callee 可用但 push/pop/clear 全部禁止。
3. **`&string` 升级为 pointer ABI**：当函数签名里含 `&string` 时，以 `LsString*` 传递可避免 3 字段拷贝；需要同步改 `codegen_fn_decl` 的 param 落盘与 `AST_IDENT` 的 load 路径。非必需，先度量收益。

**优先级 2 — 放宽位置限制**：

4. 允许 `&T` 作为函数返回类型——但必须跟"借用生命期"挂钩，需要引入类似 Rust 的生命期参数，或先用保守规则"`&T` 返回值必须来自形参借用"。工作量较大，可延后。

**优先级 3 — `&mut T`**（谨慎）：
仅在确实需要"可变但不转移所有权"时引入。需要：(a) 别名检查（同一 owner 同时只能有一个 `&mut`）；(b) callee 内允许部分 mutation 但仍禁止 move 整值。这是真正的 Rust 借用检查器领域，实现复杂度 >> 只读借用。建议推迟到有真实需求再做。

### 11.6 注意事项 / 陷阱

- **auto-borrow（`&T ← T`）是隐式的**：用户调用 `fn f(&string s)` 时写 `f(my_owned)` 不需要 `&my_owned`。这让语法轻量，但也意味着用户可能意识不到自己在借用。诊断信息里 move 错误应明确标出"变量 x 是借用"。
- **借用不能延长出函数作用域**：callee 不能把借用存进 vec/map 或返回它（当前被全面禁止；未来若允许返回借用需要生命期系统）。
- **字面量实参**：`f("literal")` 仍然走 auto-borrow 路径正常，因为 `"literal"` 本来就是 static（cap=0）string。
- **借用方法调用返回值是 owned**：`(&s).upper()` 返回新 owned 串，不继承借用性。这与 Rust 一致，也是最合理的设计。
- **struct 方法的 `self` 与借用**：当前 `self` 不是借用（`impl` 方法里 `self` 仍是 owned 语义）。将来扩展 `&self` 时需要重新审视 `check_impl_decl`。
- **Parser 限制副作用**：由于 `&` 在变量声明位置会被解析为"一元表达式"并失败，用户的典型写法"类型前置 + 标识符"对 `&string x = ...` 自动屏蔽，这是当前的"意外保护"，不要被未来的语法扩展破坏。

---

## 十二、Phase 5.5：`&!string` 可写借用 — 实现记录（2026-04）

在 Phase 5 只读借用之上，引入 **不需要 `mut` 关键字** 的可写借用 `&!T`。核心不变式：**可写借用允许变更内容（`=` / `+=` / `.append`），但禁止转移所有权**。

### 12.1 设计三元组（语义锁定）

| 操作 | `string`（owned） | `&!string`（writable borrow） | `&string`（read-only borrow） |
|---|---|---|---|
| 读 / `print` | ✅ | ✅ | ✅ |
| `=` 重新赋值 | ✅ | ✅（写穿到 caller） | ❌ |
| `+=` / `.append(...)` | ✅ | ✅（写穿到 caller） | ❌ |
| `vec.push(s)` / `map.set(..., s)` | ✅（move） | ❌ | ❌ |
| `__move(s)` | ✅ | ❌ | ❌ |
| `string t = s`（复制出借用内容） | ✅（move 原变量） | ❌ | ✅ 正常读 |
| 调用点 `&!s` 显式取可写借用 | ✅ | ✅（转发） | ❌ |
| 调用点 `&s` / auto `s` 取只读借用 | ✅ auto-borrow | ✅ downgrade | ✅ |
| 同一 call 内同名变量再借用 | — | ❌ aliasing 报错 | ❌ aliasing 报错 |

**关键决策**：
- **没有 `mut` 关键字**：用 `&!T` 单个符号承载"可写借用"语义；用户若不写 `!` 默认是只读借用，心智负担低。
- **作用域**：当前仅对 `&!string` 形参支持；不支持 `&!T` 作为返回类型、变量声明或 struct 字段。
- **显式性**：`&!x` 必须显式写出，不像只读借用有 auto-borrow（`&T ← T`）。这避免"悄悄"把 owned 变量借成可写的。
- **ABI 差异**：只读借用走 by-value（cap=0 标记），可写借用走 **pointer**（LsString*）——mutation 必须对 caller 的槽位可见。

### 12.2 实现分解（Step 1-7）

| Step | 内容 | 主要改动 |
|---|---|---|
| 1 | 类型/AST/解析 | `Type.is_mut`, `type_mut_reference()`, `AST_MUT_BORROW`, `TypeNode.is_mut`; parser 中 `&!` 前缀（类型 + 表达式两处） |
| 2 | checker 规则 | `Symbol.is_mut_borrow`；`type_assignable` 加 `&T ← &!T` downgrade；fn 入口 unwrap 并设标志；`AST_MUT_BORROW` 校验 operand（IDENT / string / 非 borrow / 非 moved / 非 static）；`checker_reject_mut_borrow_copy_source` 拒绝 `string t = mut_borrow` 和赋值 RHS |
| 3 | (合并到 4) | — |
| 4 | 调用点别名 | `AST_CALL` 后对所有实参做 pair-scan：同变量在同一 call 中既作 `&!` 又作其它借用 → 报错 |
| 5 | codegen ABI | `type_to_llvm(&!T) → LLVMPointerType`；`codegen_fn_decl` 对 `&!T` 形参跳过 alloca，直接用 param 值（LsString*）并设 `is_mut_borrow + is_borrowed`；`AST_MUT_BORROW` 直接返回 `sym->value`（caller alloca 地址）。现有 `AST_IDENT` / `AST_ASSIGN` / `+=` / `.append` 天然沿指针穿透，无需改动 |
| 6 | 方法层拦截 | checker `check_string_method` 在 `.append` 分支拒绝 `is_borrow` 接收者（`+=` 早已被通用赋值拦截覆盖） |
| 7 | 测试矩阵补漏 | forward mut borrow / mut+read distinct / `.append(int)` / loop 内 mutation / `&!int` 非 string 拒绝 |

### 12.3 ABI 设计要点

**可写借用必须是 pointer ABI**，否则 callee 的 `+=` / 重新赋值对 caller 不可见。实现上：
- Callee 形参类型在 LLVM 层是 `ptr`；`sym->value` 就是传入的指针。
- Callee 内 `AST_IDENT` 的 `LLVMBuildLoad2(LsString, sym->value)` 自然穿透指针读 caller 槽位。
- Callee 内 `AST_ASSIGN` / `+=` / `.append` 原本对 "alloca-as-LsString*" 操作，现在对 "param-as-LsString*" 操作——**同一份代码**。
- `emit_scope_cleanup` 因 `is_borrowed=true` 跳过该槽，caller 保留所有权。

`&!x` 实参侧：checker 保证 `x` 是 owned local，codegen 直接返回 `sym->value`（它本身就是 caller 的 alloca 地址，即 LsString*）。

### 12.4 测试矩阵（tests/samples/mutref_*.ls + borrow_*.ls）

**总计 35 条：18 正例 + 17 反例，全部通过。**

| 阶段 | 正例 | 反例 | 覆盖要点 |
|---|---|---|---|
| Phase 5（只读借用） | 4 | 4 | `borrow_basic / caller_live / chain / methods`；`neg_move / neg_assign / neg_move_explicit / neg_var_decl` |
| Step 1（解析） | 2 | 0 | 类型位置 / 表达式位置 |
| Step 2（checker 规则） | 1 | 7 | `pos_readonly_downgrade`；`neg_move / move_explicit / copy_out / implicit / static / literal / readonly_upgrade` |
| Step 4（别名） | 1 | 3 | `pos_distinct`；`neg_alias_mut_mut / mut_read / three` |
| Step 5（codegen） | 5 | 0 | `pos_read / append / reassign / downgrade / multi` |
| Step 6（方法层） | 1 | 2 | `pos_append_method`；`neg_append_readonly / neg_pluseq_readonly` |
| Step 7（补漏） | 4 | 1 | `pos_forward / mut_read_distinct / append_int / in_loop`；`neg_nonstring` |

### 12.5 代码改动位点一览

| 文件 | 新增/修改 | 说明 |
|---|---|---|
| `src/types.h` | `Type.is_mut` 字段；`type_mut_reference()` | TYPE_REFERENCE 子变种，不改 TypeKind 枚举 |
| `src/types.c` | `type_clone / equals / name / mut_reference`；所有 PRIM_* 初始化补 `false` | MSVC 对 POD 零值初始化器敏感 |
| `src/ast.h / ast.c` | `AST_MUT_BORROW`；`TypeNode.is_mut`；`mut_borrow.operand` | operand 必须是 AST_IDENT |
| `src/parser.c` | `parse_type` 识别 `&!`；`prefix_addr` 识别 `&!` | 两处前缀 |
| `src/symtable.h / .c` | `Symbol.is_mut_borrow` | 与 `is_borrow` 互斥 |
| `src/checker.c` | fn 入口 unwrap（3 处）；`type_assignable` 加 downgrade；`AST_MUT_BORROW` 校验；`reject_mut_borrow_copy_source` 助手；`AST_CALL` pair-scan aliasing；`check_string_method(append)` 拒绝只读借用接收者；`resolve_type_node` 分派到 mut 版本并拒绝非 string | 核心 |
| `src/codegen.h / .c` | `CgSymbol.is_mut_borrow`；`type_to_llvm(&!T)→ptr`；`codegen_fn_decl` 跳过 alloca；`AST_MUT_BORROW` 返回 `sym->value` | pointer ABI 落地 |

### 12.6 未完成 / 暂不支持

- **`&!vec(T)` / `&!map(K,V)` / `&!struct`**：类型解析阶段主动拒绝非 string。扩展策略同只读借用（见 §11.5 优先级 1）。
- **`&!T` 作为返回类型 / 变量声明 / struct 字段**：同只读借用，需要生命期系统才能放宽。
- **同一变量在不同 statements 中的"借用窗口"跟踪**：当前只检查单次 call 内的 aliasing。跨语句的 `&!x` 紧接其它借用是允许的，这与只读借用模型一致——真正的借用生命期系统（NLL 风格）暂不需要，因为没有可变别名问题（同一 call 内 aliasing 已禁止）。
- **`&!self`**：impl 方法的 `self` 当前是 owned 语义；引入 `&!self` 属于独立议题。

### 12.7 注意事项 / 陷阱

- **没有 auto-mut-borrow**：`fn f(&!string s)` 调用必须写 `f(&!x)`，不能省略 `&!`。这个不对称（只读可省、可写必写）是故意的——显式声明"我要修改它"降低隐式修改的意外。
- **aliasing 仅在单次 call 内检查**：`f(&!x); g(&!x); h(&!x, x);` 中前两句合法（连续 call），第三句不合法（同 call 内 `&!x` + `x` auto-borrow）。
- **writable borrow 链式转发**：`outer(&!x) → inner(&!s)` 可行，`s` 本身类型被 unwrap 到 TYPE_STRING 但 `sym->value` 仍是指针，`&!s` 走 AST_MUT_BORROW 返回该指针——天然穿透多层。
- **不能从可写借用拷贝出新变量**：`string t = s`（s 是 `&!string`）被拒绝。设计理由：可写借用的"当前内容"语义上随时可能被外部（通过同一借用）继续修改，拷贝瞬间的快照很容易被误用。如需快照，显式 `.upper()` / `.substr()` 等方法返回的 owned 新串是 OK 的。
- **`.append(int)` 语义**：int 实参被 trunc 到 i8 当作单字节追加（与现有 owned string 一致）。通过可写借用调用不改变这个语义。

---

## 十三、Phase 5.6：`&vec(T)` / `&!vec(T)`

在 Phase 5 / 5.5 之上，扩展借用到 `vec(T)`。**和 string 不对称：vec 借用走 pointer ABI**（无论只读/可写）。

### 13.1 语义三元组

| 操作 | `vec(T)`（owned） | `&!vec(T)` | `&vec(T)` |
|---|---|---|---|
| 读（`v[i]` / `.length` / `.get` / `.is_empty` / `.first` / `.last` / `.contains` / `.index_of` / `.slice` / `.copy`） | ✅ | ✅ | ✅ |
| mutating 方法（`push` / `pop` / `clear` / `set` / `reserve` / `remove` / `truncate` / `swap` / `reverse` / `extend` / `insert` / `resize` / `sort` / `sort_by` / `shrink_to_fit`） | ✅ | ✅（写穿 caller） | ❌ |
| `v[i] = x` | ✅ | ✅（写穿） | ❌ |
| `v = new_vec` | ✅（move 旧 vec） | ✅（释放旧、装新） | ❌ |
| `vec(T) t = v` copy-out | ✅（move） | ❌ | ❌ |
| 调用点 `&!v` | ✅ | ✅（转发） | ❌（不能升级） |
| 调用点 `&v` / auto `v` | ✅ auto-borrow | ✅ downgrade | ✅ |
| 同一 call 内同名变量再借用 | — | ❌ aliasing | ❌ aliasing |

### 13.2 ABI：为什么 pointer

| 维度 | by-value LsVec | pointer LsVec\* |
|---|---|---|
| 和 `&!vec` 统一 | ❌ | ✅ |
| `len/cap` 写穿 | ❌（需要 checker 禁 mutation 掩盖） | ✅ |
| 转发链（`outer(&!v) → inner(&!v)`） | 需再取地址 | 指针原样传递 |
| 和 owned vec param codegen 共享路径 | ✅ | ✅ |
| 降级 `&!vec → &vec` 成本 | load+store | 指针直传 |

**结论**：`&vec(T)` / `&!vec(T)` 均用 pointer。`&string` 的 by-value 特化保留（16 字节 POD 优化 + cap=0 标记机制已成熟），**string 是唯一特例**。

### 13.3 实现分解

| 文件 | 改动 | 说明 |
|---|---|---|
| `src/checker.c` | `resolve_type_node` 允许 `TYPE_VECTOR` pointee；`AST_MUT_BORROW` 允许 vec；`vec_method_is_mutating` + `checker_reject_vec_mut_on_readonly_borrow`（在 `check_vector_method` 入口处卡点）；`AST_ASSIGN` 对 `v[i] = x` 当 base 是 `is_borrow` vec 时拒绝；`checker_reject_vec_borrow_copy_source` 在 `var_decl` 和 `assign` 的 RHS 处拒绝 `vec(T) t = borrowed_vec`（对 `&vec` 和 `&!vec` 均拒） | 编译期静态分析 |
| `src/codegen.c` | `type_to_llvm(&vec(T)) → ptr`（新增分支，和 `&!T` 同）；`codegen_fn_decl` 为 `&vec(T)` 只读分支：`LLVMGetParam` → `sym->value = ptr`、`is_borrowed=true`、跳过 alloca；call-site 在 string fixup 前加 vec arg fixup：形参 LLVM 类型是 pointer 且实参 `resolved_type` 是 `TYPE_VECTOR` 时，用 IDENT 的 `sym->value` 替换 arg_val（fallback：alloca+store 临时 LsVec） | pointer ABI 落地 |
| `src/codegen.h` | 无（复用 `is_borrowed`；无需 `is_borrow` 标志，mutation 全部由 checker 拦截） | — |
| `src/types.h / .c / parser.c / ast.*` | 无改动 | TYPE_REFERENCE/AST_MUT_BORROW 本身已泛型 |

### 13.4 关键代码点

- **AST_MUT_BORROW codegen**（`codegen.c` ~L6672）：已是泛型 `return sym->value`，无需修改——对 vec IDENT 天然返回 LsVec\*（caller 的 alloca 地址）。
- **`codegen_vec_method` 的 vec_alloca 解析**（`codegen.c` ~L4562）：`obj_node->kind == AST_IDENT` 时 `vec_alloca = sym->value`。owned vec param 的 `sym->value` 是 alloca（本就是 LsVec\*），`&vec`/`&!vec` 的 `sym->value` 是 caller 指针——**同一份代码路径**，天然穿透。
- **AST_IDENT 读取 vec**：`LLVMBuildLoad2(LsVec, sym->value)` 从指针处载入 struct 值——用于 `v.length` / iteration 等需要值的场合。mutation 不走这条路径（走 vec_alloca 路径直接对指针操作）。

### 13.5 测试矩阵（`tests/samples/vecref_*.ls`）

| 类别 | 文件 | 覆盖点 |
|---|---|---|
| 正例 | `pos_read` | `&vec` 读 + auto-borrow |
| 正例 | `pos_push` | `&!vec` push 写穿 caller |
| 正例 | `pos_write_elem` | `&!vec` `v[i] = x` 写穿 |
| 正例 | `pos_downgrade` | `&!vec → &vec` 降级 + push 后读 |
| 正例 | `pos_forward` | `outer(&!v) → inner(&!v)` 链式转发 |
| 正例 | `pos_string_elem` | `&!vec(string)` push owned string |
| 正例 | `pos_methods` | `&!vec.pop/.reverse` + `&vec.is_empty/.length` |
| 反例 | `neg_push_readonly` | `&vec` 调 push 被拒 |
| 反例 | `neg_elem_assign_readonly` | `&vec` `v[i] = x` 被拒 |
| 反例 | `neg_copy_out_readonly` | `vec(int) t = &vec` 被拒 |
| 反例 | `neg_copy_out_mut` | `vec(int) t = &!vec` 被拒（两条错误都触发） |
| 反例 | `neg_implicit_mut` | `&!vec` 未显式 `&!` 被拒 |
| 反例 | `neg_readonly_upgrade` | `&vec → &!vec` 升级被拒 |
| 反例 | `neg_alias` | `f(&!v, v)` aliasing 被拒 |

**共 14 条：7 正 + 7 反，全部通过。**

### 13.6 未完成 / 暂不支持

- `&struct` / `&!struct`：需同时处理字段级 move（struct Phase A/B 已有基础）与 `has_drop` 的 moved_flag 交互。
- `&!self` for impl methods：独立议题，当前 `self` 是 owned 语义。

> `&map(K,V)` / `&!map(K,V)` 已在 Phase 5.7 完成，见 §14。

### 13.7 注意事项

- **`&vec(T)` 也禁止 copy-out**（不同于 `&string` 的只读借用）：vec 没有 cap=0 那种 "harmless alias" 的机制，复制 `{data,len,cap}` 出去会在作用域结束时 double-free。
- **`v = new_vec` 对 `&!vec` 合法**：和 `&!string` 的重赋值同策略——释放旧内容、装入新内容，写穿到 caller 槽位。
- **aliasing 检查沿用 Phase 5.5 的 pair-scan**，vec 自动纳入（基于 IDENT 名字比较，与类型无关）。

---

## 九、错误信息规范

所有 move 相关错误使用统一格式：

```
move error [line:col]: use of moved variable 'name' (moved at line N)
move error [line:col]: re-assignment to moved variable 'name' is not allowed
move error [line:col]: use of maybe-moved variable 'name' (possibly moved at line N)
move error [line:col]: __move() requires a variable, not an expression
```

错误类型前缀：`move error`（区别于 `type error` / `syntax error`）。

---

## 十、与 Codegen 层的协作

Checker Phase A/B 实现后，Codegen 层的运行时保护（mark_string_moved 等）**依然保留**，作为双重保障：

- **Checker**：编译期静态分析，报错阻止非法程序编译
- **Codegen 运行时**：即使 checker 有漏网之鱼（如未来新增的操作未更新 checker），运行时仍正确标记 cap=-1 并在 cleanup 时跳过，避免 double-free

两层互为补充，不冲突。

---

## 十四、Phase 5.7：`&map(K,V)` / `&!map(K,V)` 借用

在 Phase 5.6 之上，把借用扩展到 `map(K,V)`。**和 vec 同策略：pointer ABI**（无论只读/可写）。

### 14.1 语义三元组

| 操作 | `map(K,V)`（owned） | `&!map(K,V)` | `&map(K,V)` |
|---|---|---|---|
| 读（`.length` / `.get` / `.contains_key` / `.keys` / `.values`） | ✅ | ✅ | ✅ |
| mutating 方法（`set` / `remove` / `clear`） | ✅ | ✅（写穿 caller） | ❌ |
| `m = new_map` | ✅（move 旧 map） | ✅（释放旧、装新） | ❌ |
| `map(K,V) t = m` copy-out | ✅（move） | ❌ | ❌ |
| 调用点 `&!m` | ✅ | ✅（转发） | ❌（不能升级） |
| 调用点 `&m` / auto `m` | ✅ auto-borrow | ✅ downgrade | ✅ |
| 同一 call 内同名变量再借用 | — | ❌ aliasing | ❌ aliasing |

### 14.2 ABI

完全沿用 vec 的 pointer 策略。`LsMap*` 直接传递；callee 中 `sym->value` 即 caller 的 LsMap*，与 owned map param 的 alloca-of-LsMap 同型，所有 map 方法的 codegen 路径透明复用。

### 14.3 实现分解

| 文件 | 改动 | 说明 |
|---|---|---|
| `src/checker.c` | `resolve_type_node` 放宽 pointee 至 `TYPE_MAP`；`AST_MUT_BORROW` 允许 map；`map_method_is_mutating`（set / remove / clear）+ `checker_reject_map_mut_on_readonly_borrow`（在 `check_map_method` 入口处卡点）；`checker_reject_map_borrow_copy_source` 在 `var_decl` / `assign` RHS 处拒绝 `map(K,V) t = borrowed_map`（对 `&map` 和 `&!map` 均拒） | 编译期静态分析 |
| `src/codegen.c` | `type_to_llvm(&map(K,V)) → ptr`（vec 同分支放宽）；`codegen_fn_decl` 新增 `&map(K,V)` 只读分支（与 vec 对称）；call-site fixup 把 string fixup 之前的 vec 分支放宽到 vec\|map | pointer ABI 落地 |

### 14.4 测试矩阵（`tests/samples/mapref_*.ls`）

| 类别 | 文件 | 覆盖点 |
|---|---|---|
| 正例 | `pos_read` | `&map` 读 + auto-borrow（`.length` / `.get` / `.contains_key`） |
| 正例 | `pos_set` | `&!map` set 写穿 caller |
| 正例 | `pos_downgrade` | `&!map → &map` 降级 |
| 正例 | `pos_remove_forward` | `outer(&!m) → inner(&!m)` 链式转发 + remove |
| 反例 | `neg_set_readonly` | `&map.set()` 被拒 |
| 反例 | `neg_remove_readonly` | `&map.remove()` 被拒 |
| 反例 | `neg_clear_readonly` | `&map.clear()` 被拒 |
| 反例 | `neg_copy_out_readonly` | `map(K,V) t = &map` 被拒 |
| 反例 | `neg_copy_out_mut` | `map(K,V) t = &!map` 被拒 |
| 反例 | `neg_implicit_mut` | `&!map` 未显式 `&!` 被拒 |
| 反例 | `neg_readonly_upgrade` | `&map → &!map` 升级被拒 |
| 反例 | `neg_alias` | `f(&!m, m)` aliasing 被拒 |

**共 12 条：4 正 + 8 反，全部通过。**

### 14.5 注意事项

- **`&map`/`&!map` 均禁止 copy-out**：和 vec 同理，`{buckets, len, cap}` 复制出去会在作用域结束时 double-free 整张哈希表（含所有 string keys/values 的深拷贝）。
- **mutating 集只有 3 个**：`set` / `remove` / `clear`。`get` / `keys` / `values` 返回新拷贝（或 vec），不影响源 map 状态，归入只读集。
- **runtime 的 `MAP_EMIT_COPY_KEY` cap=0 跳过深拷贝**机制在借用上下文不变——借用 map 中 keys/values 的所有权依然属于 map 本身，只读借用调 `.get` 时返回的 string 也按原 owned 路径处理。

---

## 十五、Phase 5.8：`&struct` / `&!struct` 借用（first slice：仅 POD）

在 Phase 5.7 之上，把借用扩展到 **POD struct**（无 `string` / `vec` / `map` / 自定义 `__drop` 字段，即 `has_drop == false`）。pointer ABI，与 vec/map 同策略。

### 15.1 切片范围

| 维度 | 范围 |
|---|---|
| 支持 | POD struct 的字段读（`p.x`）、字段写（`p.x = e`）、auto-borrow `&`、显式 `&!`、`&!→&` 降级 |
| 不支持 | 含 drop 字段的 struct、实例方法调用、借用作返回类型/变量声明/struct 字段 |

含 drop 字段的 struct 借用涉及字段级 move 跟踪 + clone-on-read 策略 + `__drop` 联动，规模与 Phase 5.6 相当，留待独立 phase。`&!self` 是另一独立议题，需要 `impl` 块层面的语法/语义改造。

### 15.2 语义三元组

| 操作 | `Struct`（owned） | `&!Struct` | `&Struct` |
|---|---|---|---|
| `p.field` 读 | ✅ | ✅ | ✅ |
| `p.field = x` | ✅ | ✅（写穿） | ❌ |
| 实例方法 `p.m()` | ✅ | ❌（暂不支持） | ❌（暂不支持） |
| 静态方法 `Struct.m()` | ✅ | n/a | n/a |
| `Struct t = p` copy-out | ✅ | ❌ | ❌ |
| 调用点 `&!p` | ✅ | ✅（转发） | ❌（不能升级） |
| 调用点 `&p` / auto `p` | ✅ auto-borrow | ✅ downgrade | ✅ |
| 同一 call 内同名变量再借用 | — | ❌ aliasing | ❌ aliasing |

### 15.3 ABI

pointer ABI（与 vec/map 同）。LLVM 层面：
- 形参类型 `Struct*`
- callee 中 `sym->value = LLVMGetParam(...)` 直接保存 caller 指针，不 alloca、不 copy
- 字段读：`codegen_expr` 走原 `AST_FIELD` 路径，`obj_node` 是 IDENT 时用 `sym->value` 作为 `struct_ptr` 给 `LLVMBuildStructGEP2`——owned struct 的 alloca 与 borrow 的 caller 指针在 LLVM 层是同型 `ptr`，**透明复用**
- 字段写：`codegen_lvalue_ptr` 同理，`AST_IDENT` 直接返回 `sym->value`
- `is_borrowed = true` 防止 scope cleanup 释放 caller 内存（POD struct 本身没 drop，标志主要保留语义对称性）

### 15.4 实现分解

| 文件 | 改动 | 说明 |
|---|---|---|
| `src/checker.c` | `resolve_type_node` 放宽 pointee 至 `TYPE_STRUCT`（且 `!has_drop`）；`AST_MUT_BORROW` 同放宽；`checker_reject_struct_borrow_copy_source` + 接入 `var_decl` / `assign` RHS；`AST_ASSIGN` 在 `target->kind == AST_FIELD` 时拒绝 base 为 `is_borrow` struct；`AST_CALL` 实例方法分支：`obj_node` IDENT 是 borrow 时报 `[move error]` | 编译期静态分析 |
| `src/codegen.c` | `type_to_llvm(&struct) → ptr`（vec/map 分支放宽）；`codegen_fn_decl` 只读 struct 分支与 vec/map 合并；call-site fixup 把 vec/map 分支放宽到 vec\|map\|struct，且 fallback alloca 用 `type_to_llvm(arg_type)` 推导 LLVM 类型 | pointer ABI 落地 |
| AST_FIELD 读 / `codegen_lvalue_ptr` | 无改动 | IDENT 走 `sym->value`，owned alloca 与 borrow 指针同型 |
| AST_MUT_BORROW codegen | 无改动 | 已是 `return sym->value` 泛型 |

### 15.5 测试矩阵（`tests/samples/structref_*.ls`）

| 类别 | 文件 | 覆盖点 |
|---|---|---|
| 正例 | `pos_read` | `&Point` 字段读 + auto-borrow |
| 正例 | `pos_write` | `&!Point.x = ...` 写穿 caller |
| 正例 | `pos_downgrade` | `&!Box → &Box` 降级 |
| 正例 | `pos_forward` | `bump_twice(&!c) → bump(&!c)` 链式转发 |
| 反例 | `neg_field_assign_readonly` | `&Point` 写字段被拒 |
| 反例 | `neg_copy_out_readonly` | `Point t = &Point` 被拒 |
| 反例 | `neg_copy_out_mut` | `Point t = &!Point` 被拒 |
| 反例 | `neg_implicit_mut` | `&!Point` 未显式 `&!` 被拒 |
| 反例 | `neg_readonly_upgrade` | `&Point → &!Point` 升级被拒 |
| 反例 | `neg_alias` | `f(&!p, p)` aliasing 被拒 |
| 反例 | `neg_method_call` | `p.show()` on borrow 被拒 |
| 反例 | `neg_drop_struct` | `&Person { string name; ... }` 被拒（has_drop） |

**共 12 条：4 正 + 8 反，全部通过。**

### 15.6 已知 trade-off

- **POD-only 限制是有意的**：含 drop 字段的 struct 借用需要决定字段读返回 owned 还是借用（当前 owned struct 字段读会 `emit_string_clone_val` / `emit_struct_clone_val`，借用上下文也得保持同样行为否则 caller 会 double-free）。这个决策与 `&!self` 一并设计更经济。
- **实例方法暂禁**：`self` 当前是 owned 语义；要在 borrow 上调方法，得引入 `fn show(&self)` / `fn mut(&!self)` 语法标注。这是 `&!self` phase 的核心。
- **结构体字段是借用类型**（如 `struct Wrapper { &!Point inner }`）需引入生命期系统，远期议题。

## 十六、Phase A1：`&self` / `&!self` 显式 self 借用方法（2026-04）

### 16.1 目标

为 `impl Struct { ... }` 中的实例方法引入显式 self 借用语法，解锁 Phase 5.8 留下的 "borrow 对象上调方法" 限制。

```ls
impl Point {
    fn show(&self)            { print(self.x) }       // 只读 self
    fn shift(&!self, f64 dx)  { self.x = self.x + dx } // 可写 self
    fn distance(Point other)  { ... }                  // 旧式 (legacy)
}
```

### 16.2 三态语义

| 写法 | self 在 body 中的类型 | 调用合法性 |
|---|---|---|
| `fn m()`（旧式，self_borrow_kind = 0） | `*Struct` 伪指针 | 仅 owned；borrow 上调用被拒（建议迁移到 `&self`/`&!self`） |
| `fn m(&self)`（kind = 1） | `Struct`，`is_borrow = true` | owned / `&Struct` / `&!Struct`（自动降级） |
| `fn m(&!self)`（kind = 2） | `Struct`，`is_mut_borrow = true` | owned / `&!Struct`（不接受 `&Struct`） |

`&self` 方法体内 `self.field = ...` 被 checker 拒绝；`&!self` 允许。

### 16.3 ABI 与 Codegen

**LLVM 函数签名不变**：self 永远是首参数 `Struct*`（指针 ABI）。差别只在 callee body 内的符号绑定：

- `kind == 0`：alloca 一份 self，store 入参，sym->value 是 alloca（兼容旧字段读/写代码）
- `kind != 0`：sym->value 直接是入参指针（caller 的 struct ptr），无 alloca、无拷贝；置 `is_borrowed`/`is_mut_borrow`

call-site 走和 `&Struct` / `&!Struct` 相同的传参路径（已在 Phase 5.8 实现），无需新增 fixup。

### 16.4 限制（暂行）

- 仅 POD struct（无 has_drop 字段）。drop struct 的 `&self`/`&!self` 与字段读 clone 策略一并留给后续 phase
- 顶层 fn 与 `static fn` 不允许 `&self`/`&!self`
- 同一 impl 内可混用 legacy / `&self` / `&!self`，互不影响

### 16.5 测试矩阵（`tests/samples/selfref_*.ls`）

| 类型 | 文件 | 验证 |
|---|---|---|
| 正例 | `pos_mut_self_owned` | owned struct 上调用 `&!self` 方法 |
| 正例 | `pos_mut_self_borrow` | `&!Struct` 上调用 `&!self` 方法 |
| 正例 | `pos_readonly_self` | `&self` 方法在 owned/`&Struct`/`&!Struct` 三种调用 |
| 反例 | `neg_readonly_writes_field` | `&self` 内 `self.x = ...` 被拒 |
| 反例 | `neg_mut_self_via_readonly` | `&Struct` 调用 `&!self` 方法被拒 |
| 反例 | `neg_top_level_self` | 顶层 fn 用 `&!self` 被拒 |
| 反例 | `neg_static_self` | `static fn` 用 `&!self` 被拒 |
| 反例 | `neg_drop_struct` | drop struct 上 `&!self` 被拒（POD-only） |

**共 8 条：3 正 + 5 反，全部通过；regression 全绿。**

### 16.6 后续切片

- A2 (内联 A1)：`&self` 已与 `&!self` 同期实现，无独立 phase
- B：drop struct 的 `&self`/`&!self`（需决定字段读 clone vs borrow 返回）
- C：`&self`/`&!self` 与 `__drop` 协议的交互（borrow 不可触发 drop）

## 十七、Phase B：drop struct 借用与 `&self`/`&!self` 解锁（2026-04）

### 17.1 目标

解除 Phase 5.8 / Phase A1 的 POD-only 限制，使含 `string`/`vec`/`map` 字段的 struct（`has_drop = true`）也能：

- 作为 `&Struct` / `&!Struct` 形参传递
- 实例方法声明 `fn m(&self)` / `fn m(&!self)`

### 17.2 修改点

1. **checker `resolve_type_node` TYPE_REFERENCE**：删除 has_drop 拦截
2. **checker `AST_MUT_BORROW`**：删除 has_drop 拦截
3. **checker `check_impl_decl`**：删除 self-borrow 上的 has_drop 拦截
4. **codegen `AST_CALL` 参数处理**：当 callee 形参是 pointer ABI（`&Struct`/`&!Struct`）时，跳过 `emit_struct_clone_val` 防止 heap 泄漏

### 17.3 字段读 clone 策略

owned struct 上 `s.name`（string 字段）已经走 `emit_string_clone_val` 返回 owned 副本。借用 struct 上字段读完全复用同一路径（codegen 的 AST_FIELD 不区分 holder 来源），保持 caller 端一致语义：调用者拿到独立的 owned string，方便后续 print / 赋值 / move。

注意：这是保守策略，性能上每次字段读都 alloc+memcpy。后续可加"读后立即 print/比较"的窥孔优化或显式 borrow 返回类型（远期生命期系统）。

### 17.4 测试

- `tests/samples/structref_drop_pos_basic.ls`：drop struct 上的 `&Person` / `&!Person` 字段读写
- `tests/samples/structref_drop_pos_self_method.ls`：drop struct 上的 `&self` / `&!self` 方法（含 owned / 借用 / 借用降级三种调用路径）
- 删除：`structref_neg_drop_struct.ls` / `selfref_neg_drop_struct.ls`（已不再被拒）
- regression：8/8 ctest + 全部 borrow samples 绿

### 17.5 已知 trade-off

- **字段写不释放旧值的边界场景需进一步验证**：`p.name = new_str` 应该 free 旧 owned name（`emit_string_clone_val` 路径已有），但当 new_str 是 static 时是否仍正确 clone 待审计
- **借用作为返回类型**仍未支持（远期需引入生命期系统）
- **借用作 struct 字段**同上
