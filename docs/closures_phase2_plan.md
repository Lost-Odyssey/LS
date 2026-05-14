# LS 闭包系统 — 第二阶段实现计划（Phase F）

> 本文档承接 [docs/closures_plan.md](closures_plan.md) 与 CLAUDE.md §8。
> 第一阶段（Phase A–E.4）已完成基础闭包 + 捕获策略；第二阶段聚焦：
> 1. `[move v]` 显式所有权转移语法
> 2. Block 作为一等值（赋值、struct 字段、容器元素）
> 3. Block 完整生命周期 + drop 模型
> 4. CG_DEBUG 全链路所有权可视化

---

## 零、阶段目标

### 必做（按依赖排序）

- [F.1] `[move v]` 语法：用户可显式让 vec/map 走 by-move（解决工厂模式悬垂）
- [F.2] Block 类型作为变量赋值的一等公民（含 `g = h` 转移）
- [F.3] Block 类型作为 struct 字段（含 has_drop 字段链路）
- [F.4] vec(Block) / map(K, Block) —— Block 作为容器元素
- [F.5] enum capture（含 has_drop enum），完整内存模型 + drop 实现
- [F.6] CG_DEBUG 全链路注入：每一处 capture / move / borrow / drop 打印结构化日志
- [F.7] Memcheck 大体检 + stress 测试（每阶段 0 leak / 0 dfree）

### 可做（取决于 F.1–F.7 进度）

- [F.8] 嵌套闭包字面量（closure body 内再写 `\|y\| ...`）
  - **若 F.8 不实施**：parser/checker 必须给出明确编译错误（见 §F.8 节）

### 不做（留给 Phase G/H）

- ❌ 逃逸分析（escape analysis）—— 需要独立设计
- ❌ 生命期标注（lifetime annotations）—— 大特性
- ❌ 用户级泛型 Block（`Block<T>`）

---

## 一、`[move v]` 语法设计

### 1.1 语法形式

```ls
// 单变量 move
Adder f = [move nums] || { return nums[0] }

// 多变量 move
Combiner g = [move v1, v2] |x| { return v1[x] + v2[x] }

// 与 trailing closure 兼容
io.with_file(p) [move buffer] { |line|
    buffer.append(line)
}

// 不写 [move] = 维持当前默认（vec/map by-ref，string/struct by-move）
Plain h = |x| { return nums[x] }   // nums by-ref（现状）
```

### 1.2 Parser 改动

新增前缀 handler 处理 `[`：

```c
// parser.c 新增
static AstNode *prefix_capture_spec(Parser *p) {
    // expect: [ move ident (, ident)* ]
    expect(p, TOKEN_LBRACKET);
    expect_keyword(p, "move");

    // 收集 move 列表
    char **move_names = ...;
    int move_count = 0;
    do {
        expect(p, TOKEN_IDENT);
        move_names[move_count++] = strdup(p->prev.lexeme);
    } while (match(p, TOKEN_COMMA));
    expect(p, TOKEN_RBRACKET);

    // 紧接着必须是 closure 字面量
    AstNode *cls = parse_closure_literal(p);  // 现有 prefix_ruby_closure
    cls->as.closure.move_names = move_names;
    cls->as.closure.move_count = move_count;
    return cls;
}
```

`[` 在表达式开头时：
- 后跟 `move` 关键字 → capture spec
- 否则 → 数组字面量（已有逻辑）

### 1.3 AST 改动

```c
// ast.h - AstClosureNode 扩展
typedef struct {
    // ... 既有字段 ...
    char **move_names;       // [move v1, v2] 的标识符列表（仅 parse 阶段填充）
    int    move_count;
    // checker 阶段把 move_names 解析到 captures[i].is_explicit_move
} AstClosureNode;

// ast.h - AstCapture 扩展
typedef struct {
    char *name;
    Type *type;
    bool  is_explicit_move;  // 用户显式 [move] 标记
    // codegen 用此字段决定走 by-move 还是默认策略
} AstCapture;
```

### 1.4 Checker 行为

```c
// checker.c - capture_walk 完成后
for each capture c:
    if c.name in node.move_names:
        c.is_explicit_move = true
        // 移除 move_names 里的对应项（剩余的报"move 了未捕获的变量"）

if move_names 还有剩余:
    error("[move ...] 列表中的 'X' 未被闭包 body 引用")

// 应用 move 语义检查
for each c with is_explicit_move:
    if outer Symbol is borrow:  error
    if outer Symbol already moved: error
    mark outer as moved (复用现有 move 检查器)
```

---

## 二、按类型 × `[move]` 的语义矩阵

| 类型 | 默认 | `[move v]` 后 | env 字段类型 | env_drop 行为 | outer 标记 |
|------|------|---------------|--------------|---------------|------------|
| POD (int/f64/bool/...) | by-copy | by-copy（warn 冗余） | T | skip | live |
| array(POD, N) | by-copy | by-copy（warn 冗余） | [N x T] | skip | live |
| string | by-move | by-move（warn 冗余） | LsString | free data if cap > 0 | MOVED (cap=-1) |
| struct(has_drop) | by-move | by-move（warn 冗余） | StructTy | call `Struct.__drop` | MOVED (moved_flag=1) |
| vec(T) | **by-ref** | **by-move** ✅ | LsVec | call `__ls_vec_<T>_drop` | MOVED (moved_flag=1) |
| map(K,V) | **by-ref** | **by-move** ✅ | LsMap | call `__ls_map_<K,V>_drop` | MOVED (moved_flag=1) |
| enum(has_drop) | not impl | by-move（F.8） | EnumTy | call `Enum.__drop` | MOVED |
| Block | not impl (F.x) | by-move | LsBlock | call `__env_drop_N` + free env | MOVED |

**核心约束**：`[move]` 对 vec/map 是真正改变行为的；对其他类型是 no-op（emit warning 提示用户冗余）。

### 2.1 vec/map 的 by-move 实现细节

env 字段从 `ptr_t`（by-ref）切换到值类型：

```c
// codegen 区分两种情况
if (capture.is_explicit_move && (ct->kind == TYPE_VECTOR || ct->kind == TYPE_MAP)) {
    // by-move 路径
    env_field_type[i+1] = type_to_llvm(ctx, ct);  // {ptr, i32, i32}
    cap_outer_vals[i] = LLVMBuildLoad2(ctx->builder, ct_llvm, sym->value, "cap.move.load");
    // outer 标 moved_flag = true（与 struct by-move 同款）
} else if (capture_type_is_by_ref_cg(ct)) {
    // 现状 by-ref 路径不变
    env_field_type[i+1] = ptr_t;
    cap_outer_vals[i] = sym->value;  // alloca 地址
}
```

env_drop 合成：

```c
// codegen synth_env_drop
for each capture c with is_explicit_move and (vec || map):
    // 调用对应的 drop helper
    field_ptr = GEP env, c.field_index
    call __ls_<vec|map>_<elem>_drop(field_ptr)
```

---

## 三、Block 作为值的完整生命周期

### 3.1 Block 是什么

```c
// 内存表示（已实装）
typedef struct {
    void *fn_ptr;   // 8 字节
    void *env_ptr;  // 8 字节，可能 NULL（无捕获闭包）
} LsBlock;          // 16 字节 POD
```

env_ptr 指向堆分配的 env struct，env 第 0 字段是 `__env_drop_N` 函数指针。

### 3.2 Block 所有权状态机

类比 LsString 三态：

| env_ptr 值 | 状态 | 说明 | 释放规则 |
|------------|------|------|----------|
| `NULL` | **Empty** | 无捕获闭包 / 已 moved Block | 永不释放 |
| `非 NULL` | **Owned** | 拥有 env 堆内存 | 退出作用域 → call drop_fn → free env |

**关键操作**（Move 触发点）：
- `Block g = h`：env_ptr 转移，`h.env_ptr := NULL`
- `f(h)`：默认 borrow（callee 不释放）；显式 `__move(h)` 转移（沿用 string 语义）
- `return h`：转移给 caller
- `vec.push(h)` / `map.set(k, h)`：转移给容器
- `s.field = h`（struct 字段赋值）：转移给 struct
- 闭包捕获 `[move h]`：转移给外层闭包的 env

### 3.3 Block 赋值（F.2）

```ls
type F = Block(int) -> int

fn main() {
    F g = |x| x + 1
    F h = g            // ← Block 赋值，env 转移；g.env_ptr 置 NULL
    print(h(10))       // 11
    // print(g(10))    // ❌ checker 报"g has been moved"
}
```

实现：
- checker 加 Block 类型的 move 检查（接入现有 `is_moved` 状态机）
- codegen 在 Block-typed `LLVMBuildStore` 后，把源 alloca 的 env_ptr 字段置 NULL

### 3.4 Block 作为 struct 字段（F.3）

```ls
type Handler = Block(int) -> int

struct Pipeline {
    string name
    Handler step1
    Handler step2
}

fn main() {
    Pipeline p = Pipeline {
        name: "demo",
        step1: |x| x * 2,
        step2: |x| x + 10,
    }
    print(p.step1(5))   // 10
    print(p.step2(5))   // 15
}   // ← p 退出作用域，Pipeline.__drop 必须释放 step1/step2 的 env
```

**checker 改动**：
- struct 含 Block 字段 → struct.has_drop = true
- struct field 类型支持 Block（之前只有 type 别名能在 field 出现，需要确认/放宽）

**codegen 改动**：
- `emit_struct_drop`（synthesize Struct.__drop）遍历字段时识别 TYPE_BLOCK：
  ```c
  if field.type == TYPE_BLOCK:
      load env_ptr = struct.field.env_ptr
      if env_ptr != NULL:
          drop_fn = env_ptr[0]
          if drop_fn != NULL: call drop_fn(env_ptr)
          call __ls_mc_free(env_ptr, "closure.env", line, col)
  ```
- struct literal 构造 Block 字段：从 rvalue 闭包字面量转移所有权（移出 temp_block_envs 队列，与 var_decl 一致）

### 3.5 Block 作为容器元素（F.7）

```ls
vec(Handler) handlers = []
handlers.push(|x| x + 1)
handlers.push(|x| x * 2)

for h in handlers {
    print(h(10))   // 11, 20
}
```

**实现**：
- `vec(Block)` 的 elem 是 16 字节 POD struct，push/pop 直接走 memcpy（与现有 `vec(struct)` 路径一致）
- vec drop 时，遍历元素，对每个 Block 调用 env drop_fn + free env
- 这个 walker 复用 emit_struct_drop 的 Block 字段逻辑

### 3.6 Block 形参 / 返回值（已部分实现）

| 位置 | 当前 | 是否需改 |
|------|------|----------|
| 函数形参 `fn f(Block b)` | ✅ borrow（is_borrowed=true） | 无变 |
| 函数返回值 `fn() -> Block` | ✅ caller 接管 env | 无变 |
| 闭包捕获 `[move h]` Block | ❌ 未实现 | F.4 实装（嵌套闭包能力） |

---

## 四、CG_DEBUG 全链路可视化（F.5）

### 4.1 输出格式约定

统一前缀 `[cg]`，操作类型 + 关键标识：

```
[cg] cap.copy   name='count'   type='int'           kind='by-copy'
[cg] cap.move   name='nums'    type='vec(int)'      kind='by-move' (explicit [move])
[cg] cap.borrow name='items'   type='vec(int)'      kind='by-ref'
[cg] cap.move   name='s'       type='string'        kind='by-move' (auto)
[cg] outer.mark name='nums'    state='MOVED'        marker='moved_flag=1'
[cg] outer.mark name='s'       state='MOVED'        marker='cap=-1'
[cg] env.alloc  closure_id=3   size=48              ptr=0x...
[cg] env.drop   closure_id=3   ptr=0x...            drop_fn=__env_drop_3
[cg] env.free   closure_id=3   ptr=0x...
[cg] block.assign target='h'   source_kind='ident'  src_env=0x... → tgt_env=0x...
[cg] block.move  from='temp'   to='var=h'           env=0x...
[cg] block.drop  var='h'       env=0x...
[cg] field.move  struct='Pipeline' field='step1'    env=0x...
[cg] vec.elem.drop  vec='handlers' index=0  block.env=0x...
```

### 4.2 注入点清单

| 点位 | codegen 函数 | 触发条件 |
|------|--------------|----------|
| 闭包捕获决策 | codegen_closure_literal step 0 | 每个 capture 一行 |
| 外层 mark moved | codegen_closure_literal step 9b | by-move 后立即 emit |
| env 分配 | cg_emit_alloc 内 | kind=="closure.env" 时额外打 closure_id |
| env_drop 调用 | emit_scope_cleanup / emit_cleanup_to | TYPE_BLOCK 分支 |
| Block 赋值 | codegen_assign | LHS/RHS 都是 TYPE_BLOCK 时 |
| Block move 转移 | var_decl / return / call arg | TYPE_BLOCK + 是 rvalue |
| Block 字段 drop | emit_struct_drop | 字段类型 == TYPE_BLOCK |
| vec/map 元素 drop | emit_vec_drop / emit_map_drop | elem 类型 == TYPE_BLOCK |

### 4.3 实现策略

复用现有 `cg_emit_debug_printf`（CG_DEBUG=0 时编译期为空）。新增辅助：

```c
// codegen.c (CG_DEBUG=1 时活跃)
static void cg_dbg_capture(CodegenContext *ctx, const char *name,
                           Type *t, const char *kind, const char *qualifier);
static void cg_dbg_outer_mark(CodegenContext *ctx, const char *name,
                              const char *marker);
static void cg_dbg_env(CodegenContext *ctx, const char *op,
                       int closure_id, LLVMValueRef env_ptr);
static void cg_dbg_block_op(CodegenContext *ctx, const char *op,
                            const char *target, LLVMValueRef env_ptr);
```

每个函数内部用 `#if CG_DEBUG` guard，内部展开为 `cg_emit_debug_printf` 调用。

---

## 五、Memcheck 集成（F.6）

### 5.1 每个子阶段的 memcheck checklist

| 阶段 | 用例必含 | 期待 memcheck 结果 |
|------|----------|--------------------|
| F.1 [move] vec | 工厂模式 + 多次调用 + outer 不再用 | 0 leak / 0 dfree |
| F.2 Block 赋值 | g = h 后 print(h)，g 与 h 都正确 drop | 0 leak / 0 dfree |
| F.3 Block 字段 | struct 含 1+ Block 字段，struct 退场触发 | 0 leak / 0 dfree |
| F.4 vec(Block) | push 多个，遍历调用，vec 退场 | 0 leak / 0 dfree |
| F.5 enum capture | Option(string) / Result / 自递归 Tree 全部覆盖 | 0 leak / 0 dfree |
| F.7 stress | 1000 次循环创建销毁，无累积泄漏 | 0 leak / 0 dfree |

### 5.2 新增 memcheck site kind

```
"closure.env"            // 现有
"closure.move.outer"     // [move] 后 outer 标记的 alloca
"block.assign.tmp"       // Block 赋值过程中的临时
"struct.field.block"     // struct 字段 drop 路径
"vec.elem.block"         // vec 元素 drop 路径
"map.value.block"        // map value 类型为 Block 时的 drop 路径
"enum.capture.payload"   // enum capture 字段中 payload 的 alloc/drop 跟踪
"enum.capture.box"       // 自递归 enum 在 env 中的 box 跟踪
```

每个 alloc 用 `cg_emit_alloc(ctx, size, kind, line, col)` 标注，泄漏时报告精确 LS 源码位置。

### 5.3 调用栈集成

Phase D.1 已实现 `ls_mc_enter` / `ls_mc_leave`，Block 路径自动获益——闭包 body 内部的泄漏会显示完整调用栈，包括是哪个闭包字面量在哪一行被实例化。

---

## 六、实施阶段（按依赖排序）

### Phase F.1：`[move v]` 语法 + vec/map by-move（3 天）

**任务**：
1. Parser：`[move ident, ...]` 前缀 → AST_CLOSURE.move_names
2. AST：AstCapture 加 `is_explicit_move`
3. Checker：解析 move_names → captures[i].is_explicit_move；剩余 move 名报错；接入现有 move 检查器（mark outer moved）
4. Codegen：闭包字面量的 by-ref 分支拆成两路（普通 by-ref vs explicit by-move）；env 字段类型 + cap_outer_vals 取值 + outer marker 全部分支
5. env_drop：is_explicit_move 的 vec/map 字段调对应 drop helper
6. CG_DEBUG：cap.move/borrow/copy 三种 kind 的注入

**测试**（`tests/samples/closure_f1_test.ls`）：
```ls
fn make_summer() -> Summer {
    vec(int) nums = [1, 2, 3]
    return [move nums] || {
        int s = 0; int i = 0
        while i < nums.length { s = s + nums[i]; i = i + 1 }
        return s
    }
}

fn main() {
    Summer f = make_summer()
    int x = pollute()           // 栈污染测试
    print(f())                  // 6（不再是 0）
}
```

**memcheck**：必须 0 leak / 0 dfree（pollute 栈污染场景下也成立）。

---

### Phase F.2：Block 赋值 + 移动语义（2 天）

**任务**：
1. Checker：Block 类型变量在 RHS 出现时按 move 处理（与 string 共享 is_moved 状态机）
2. Codegen：`g = h` 时把 source.env_ptr 写 NULL；emit_cleanup 检查 env_ptr != NULL 才走 drop 路径
3. 已有的 `temp_block_envs` 队列：var_decl 接管时 pop 已实装；增加 assign / argument move 路径
4. CG_DEBUG：block.assign / block.move 注入

**测试**：
```ls
fn main() {
    F g = |x| x + 1
    F h = g
    print(h(5))     // 6
    // g(5)         // ❌ 'g' has been moved
}   // 只有 h 走 drop
```

**memcheck**：必须 0 leak / 0 dfree（验证 g 的 env_ptr 被置 NULL 后 g 不再触发 drop）。

---

### Phase F.3：Block 作为 struct 字段（3 天）

**任务**：
1. Checker：struct field 类型放宽接受 TYPE_BLOCK（必须经 type 别名）；struct 含 Block 字段 → has_drop = true
2. Struct literal 构造：Block 字段从 rvalue temp 接管 env（pop temp_block_envs）
3. emit_struct_drop：遍历字段，TYPE_BLOCK 分支 emit env drop_fn + free
4. struct field 读 Block：返回 Block 值，env_ptr 仍归 struct 持有（不能赋出去，否则 drop 时双释放）—— 暂禁 `let g = p.step1`，只允许 `p.step1(args)` 直接调用
5. CG_DEBUG：field.move / struct.drop.block 注入

**测试**：
```ls
struct Pipe {
    Handler step1
    Handler step2
}

fn main() {
    Pipe p = Pipe { step1: |x| x*2, step2: |x| x+10 }
    print(p.step1(5))   // 10
    print(p.step2(5))   // 15
}   // p.__drop 释放两个 env
```

**额外测试 — struct 含 Block + string + vec 三种 has_drop 字段**：验证 emit_struct_drop 顺序无误，每种字段都被释放。

---

### Phase F.4：vec(Block) / map(K, Block)（2 天）

**任务**：
1. **类型侧**：vec/map elem 类型放宽接受 TYPE_BLOCK（必须经 type 别名）
2. **vec drop walker**：合成或扩展 `__ls_vec_<Block>_drop`：
   ```c
   for i in 0..vec.length:
       Block *b = &vec.data[i];
       if b->env_ptr != NULL:
           drop_fn = b->env_ptr[0];
           if drop_fn != NULL: drop_fn(b->env_ptr);
           ls_mc_free(b->env_ptr, "vec.elem.block", ...);
   free(vec.data);
   ```
3. **map drop walker**：同理处理 value 类型为 Block 的 map（`map(K, Block)`）
4. **push/set 接管 env**：`vec.push(|x| ...)` 时从 temp_block_envs pop（与 var_decl 相同语义）
5. CG_DEBUG：vec.elem.block / map.value.block 注入

**测试**（`tests/samples/closure_f4_vec_block_test.ls`）：
```ls
type H = Block(int) -> int

fn main() {
    vec(H) handlers = []
    handlers.push(|x| x + 1)
    handlers.push(|x| x * 2)
    handlers.push(|x| x - 3)

    int i = 0
    while i < handlers.length {
        print(handlers[i](10))    // 11, 20, 7
        i = i + 1
    }
}   // handlers 退场，3 个 env 全部释放
```

**memcheck**：必须 0 leak / 0 dfree。包含 stress 子测试：1000 次 push + 退场，验证无累积泄漏。

---

### Phase F.5：enum capture + 完整 drop 模型（4 天）

**这是 Phase F 中最复杂的子阶段。** enum 的内存布局是 tagged union，payload 可能含 string / vec / struct / 自递归 box，drop 必须按 disc 分派。本节给出完整的内存模型分析。

#### F.5.1 enum 内存模型回顾

```
enum Shape {
    Circle(f64 r)
    Rect(f64 w, f64 h)
    Named(string name, f64 area)   // ← payload 含 has_drop 字段
}

LLVM 布局：
%Shape = type { i32 disc, [N x i8] payload }   // N = max(payload sizeof)

Circle: disc=0, payload[0..7] = r (f64)
Rect:   disc=1, payload[0..7] = w, payload[8..15] = h
Named:  disc=2, payload[0..15] = LsString name, payload[16..23] = area
```

自递归 enum：

```
enum Tree {
    Leaf
    Node(int v, Tree left, Tree right)   // 自递归 → 编译器自动 box
}

布局：
%Tree = type { i32 disc, [N x i8] payload }
Node payload: { i32 v, ptr boxed_left, ptr boxed_right }   // box = malloc 出的 Tree 副本
```

#### F.5.2 capture 的内存策略

**enum capture 默认 by-move（不允许 by-ref，因为悬垂语义和 string 同款危险，且 enum 经常出现在工厂模式）。** `[move]` 是 no-op + warning（提示冗余）。

```c
capture_type_is_by_move(TYPE_ENUM with has_drop) → true
capture_type_is_by_move(TYPE_ENUM without has_drop) → false (POD-like, by-copy 即可)
```

#### F.5.3 capture 阶段的内存所有权转移

```
Step 0 (codegen_closure_literal):
    cap_outer_vals[i] = LLVMBuildLoad2(EnumTy, outer_alloca, "cap.enum.load")
    // 注意：load 出的是整个 enum 值（disc + payload bytes）
    // payload 内的堆指针（string.data / boxed children）共享同一份堆内存

Step 9b (写入 env 字段):
    LLVMBuildStore(cap_outer_vals[i], env_field_gep)
    // 现在 env 的 enum 字段持有同一份堆指针

Step 后续 (mark outer moved):
    moved_flag[i] = true
    // outer 退出作用域时，scope cleanup 检查 moved_flag → 跳过 enum.__drop
    // 单一 owner = env，避免 double-free
```

**关键不变量**：
1. enum capture 是**浅 move**（拷贝 disc + payload bytes，不递归 clone payload 内的 heap）
2. outer 必须标 moved，否则 scope cleanup 会调 enum.__drop 释放 payload heap → env 变悬垂
3. env_drop 必须调 enum.__drop（按 disc 分派 payload drop）

#### F.5.4 env_drop 实现

```c
// codegen synth_env_drop（已有框架，需扩展 enum 分支）
for each capture c:
    field_ptr = GEP env, c.field_index
    if c.type->kind == TYPE_ENUM and c.type->as.enom.has_drop:
        // 复用现有的 EnumName.__drop
        Function *enum_drop = lookup_or_emit_enum_drop(c.type)
        call enum_drop(field_ptr)    // 内部 switch on disc + drop payload
```

`lookup_or_emit_enum_drop` 已经为普通 enum 变量实装（emit_auto_drop_fn）。env_drop 直接复用，**不需要新写 drop 逻辑**——这是设计正确性的保证。

#### F.5.5 自递归 enum（Tree）的特殊处理

```ls
enum Tree { Leaf  Node(int v, Tree l, Tree r) }

Tree t = Node(1, Leaf, Leaf)
TreeOp op = || { return tree_size(t) }
```

- t 的 payload 含 `boxed_l` / `boxed_r` 两个 heap 指针（每个指向 malloc 出的 Tree）
- capture 时浅 move：env 接管 disc + boxed_l + boxed_r 指针
- env_drop 调 Tree.__drop → switch disc → Node 分支：
  - 递归 Tree.__drop(*boxed_l) + free(boxed_l)
  - 递归 Tree.__drop(*boxed_r) + free(boxed_r)
- outer t 标 moved → scope cleanup 跳过 Tree.__drop

**潜在风险**：自递归 enum 的递归 drop 可能遇到深度过大栈溢出。沿用现有 enum drop 的限制（默认深度，未来可改成迭代式）。

#### F.5.6 内置 Option / Result 模板的覆盖

`Option(string)` / `Result(int, string)` 等是按需单态化的 enum 模板。捕获它们时：
- `Option(string)`：has_drop=true（Some 变体含 string payload）→ env_drop 调 `Option_string.__drop`
- `Result(int, string)`：has_drop=true（Err 变体含 string）→ 同上
- `Option(int)`：has_drop=false → 走 by-copy 路径（capture_type_is_pod 不接受，需新加 `capture_type_is_pod_enum` 辅助）

#### F.5.7 has_drop_n 计数与 drop_fn slot

`codegen_closure_literal` 的 `has_drop_n` 计数现在已经覆盖 `capture_type_is_by_move_cg` 的全部类型。enum has_drop 加入后无需额外改动——只要 `capture_type_is_by_move_cg(TYPE_ENUM)` 在 has_drop 时返回 true 即可。

#### F.5.8 内存泄漏与 double-free 审计清单

| 风险点 | 检查 |
|--------|------|
| outer 未标 moved → 双释放 payload heap | mark moved 必须在 store 之后 emit |
| env_drop 漏调 enum.__drop → payload heap 泄漏 | has_drop_n 计数必须包含 enum |
| 自递归 enum box 泄漏 | enum.__drop 的递归路径已被 Phase 8 / memcheck B 验证 |
| Option(int) 走错路径（误当 has_drop） | 检查 `c.type->as.enom.has_drop` 而非 enum 类型本身 |
| match 内 binder 的 ownership：closure body 内 `match captured_enum { Some(v) => ... }` | binder v 必须 is_borrowed=true（payload 仍归 env），同 Phase 8 + memcheck B 的修复 |
| temp enum 作为捕获源（`\|\| { ... build_tree() ... }`）—— 当前 capture 必须是 IDENT，不接受 rvalue | checker 已限制 capture 必须可解析为 outer Symbol |

#### F.5.9 任务清单

1. checker：`capture_type_is_by_move` 加 `TYPE_ENUM with has_drop`；不带 has_drop 的 enum 加 `capture_type_is_pod_enum` 走 by-copy
2. codegen：`capture_type_is_by_move_cg` 同步；env_struct_t 字段类型用 `type_to_llvm(EnumTy)`
3. codegen：env_drop 合成时遇 enum 分支调 `lookup_or_emit_enum_drop(t)`
4. checker：闭包 body 内对 captured enum 的 match binder 标 is_borrowed=true（沿用 §memcheck B Bug 5 的修复路径）
5. CG_DEBUG：cap.move kind='enum'、env.drop.enum dispatching=variant_name 注入
6. 测试用例覆盖：

**测试矩阵**（`tests/samples/closure_f5_enum_test.ls`）：
```ls
enum Color { Red  Green  Blue  RGB(int r, int g, int b) }    // 无 has_drop（payload 全是 POD）
enum Tag   { Plain(string s)  Empty }                         // has_drop（string payload）
enum Tree  { Leaf  Node(int v, Tree l, Tree r) }              // has_drop + 自递归

fn main() {
    // 1. POD enum capture（走 by-copy）
    Color c = RGB(255, 128, 0)
    SomeFn f1 = || { match c { RGB(r,g,b) => print(r+g+b); _ => print(0) } }
    f1()    // 383

    // 2. has_drop enum capture（走 by-move + env_drop 调 Tag.__drop）
    Tag t = Plain("hello".upper())   // owned string in payload
    SomeFn f2 = || { match t { Plain(s) => print(s); Empty => print("e") } }
    f2()    // "HELLO"
    // print(t)  // ❌ t has been moved

    // 3. 内建 Option(string) 捕获
    Option(string) opt = Some("world".upper())
    SomeFn f3 = || {
        match opt {
            Some(s) => print(s)
            None    => print("none")
        }
    }
    f3()    // "WORLD"

    // 4. 自递归 Tree 捕获
    Tree tr = Node(1, Node(2, Leaf, Leaf), Leaf)
    SomeFn f4 = || { print(tree_sum(tr)) }   // tree_sum 借用计算
    f4()    // 3
}   // f1/f2/f3/f4 全部退场，所有 enum payload heap 全部释放
```

**memcheck 强约束**：每个测试单独跑 `--memcheck`，全部 `0 leak / 0 dfree`。重点验证 Tag 的 string payload、Tree 的 box 指针、Option(string) 的 Some payload 都正确释放，不漏不重。

---

### Phase F.6：CG_DEBUG 全面铺开（1 天）

**任务**：F.1–F.5 实施过程中已逐步注入；本阶段做完整覆盖审查，补漏 + 统一格式。

新增审查表：
- 每种 capture kind（POD/array/string/struct/vec-ref/vec-move/map-ref/map-move/enum/Block）都有 cap.* 输出
- 每种 outer marker（cap=-1/moved_flag/none）都有 outer.mark 输出
- 每种 drop 路径（scope cleanup / struct field / vec elem / map value / enum payload / env_drop）都有对应输出

提供环境变量过滤：`LS_CG_DEBUG_FILTER=cap,move,env`（默认全开）。

---

### Phase F.7：Memcheck 大体检 + stress（1 天）

**任务**：
1. 跑全部 closure_f1–f5 测试 + 现有 24 测试，所有 memcheck 必须 ✓ clean
2. 编写 stress 测试：1000 次循环创建/销毁 Block + struct(Block) + vec(Block) + 含 enum capture 的闭包
3. 验证内存稳态：第 1 次 vs 第 1000 次的 RSS 差应为常数（无累积）
4. AOT 路径同步验证

---

### Phase F.8（可选）：嵌套闭包字面量（[move] 透传）

**若实施**（3 天）：
1. Checker：闭包 body 内允许出现新的闭包字面量；内层 capture 解析能看到外层捕获的变量
2. Codegen：内层闭包字面量在外层 body 中 emit 时，capture 解析能看到外层捕获的变量（通过 outer scope chain 链回外层 env）
3. 嵌套深度：v1 仅支持 1 层嵌套（深度 2），更深报错

**若不实施（默认）**：

**Parser/Checker 必须明确报错**，不允许静默接受或产生未定义行为。

实施位置：`checker.c` 的 `capture_walk`（外层闭包扫描时遇到 AST_CLOSURE 子节点）：

```c
case AST_CLOSURE:
    checker_error(s->c, node->line, node->column,
                  "nested closure literals are not yet supported "
                  "(Phase F.8 of the closure plan). Workaround: "
                  "extract the inner closure into a top-level fn and "
                  "pass it explicitly.");
    s->had_error = true;
    return;  // 不继续递归，避免触发更深的 capture 解析错乱
```

**测试**（`tests/samples/closure_nested_reject.ls` 期望编译错）：
```ls
type Outer = Block() -> Block(int) -> int

fn main() {
    Outer make = || |n| { return n + 1 }
    //                  ^^ 内层闭包，必须被拒
}
```

CMake 期望 `compile FAILED` + stderr 含 `nested closure literals are not yet supported`。

---

## 七、生命期分析的预研（不实施）

### 7.1 现状的安全缺口

不写 `[move]` 时，by-ref 闭包逃逸 = 悬垂指针，编译器无检查。`[move]` 让用户能修复这类 bug，但**不能强制**用户写它。

### 7.2 真正解决需要什么

**最小可行的逃逸分析**（intra-procedural escape analysis）：

```
对每个闭包字面量 lit:
  分析 lit 在当前函数内的命运：
    1. 直接调用后丢弃（不存）        → 不逃逸 → by-ref 安全
    2. 赋给局部 var 后只在本函数用    → 不逃逸
    3. 作为 return 表达式            → 逃逸 ← 必须 by-move
    4. 传给 fn 形参（callee Block 形参） → 不逃逸（callee 不持久化时）
    5. 存入 vec/map/struct field    → 逃逸（容器 outlive 当前 scope）
    6. 赋给 outer scope 的 var      → 逃逸

if 闭包逃逸 AND 含 by-ref capture (无 [move]):
  error: "by-ref capture 'X' cannot escape; add [move X] or change strategy"
```

### 7.3 对编译器复杂度的影响估算

| 维度 | 复杂度 | 说明 |
|------|--------|------|
| 数据流框架 | **中等** | 需 per-function dataflow（可复用现有 Move checker 的 fixed-point） |
| AST 标注 | 低 | 给每个 AST_CLOSURE 加 `escapes: bool` 字段 |
| Checker 工作量 | 中 | 8–12 个传播规则（return / store / push / set / 字段写 / 跨 scope 赋值 / call arg / ...） |
| 跨函数分析（procedure summary） | **高** | 真正完美需要 CG-level callee 分析；可省略（保守认为所有 callee Block 形参 = 逃逸）|
| 误报率 | 中 | 保守分析会拒绝一些实际安全的代码；提供 `[move]` 出口让用户绕过 |
| 改动范围 | 中 | checker.c +500–800 行；codegen 几乎不动 |

**推荐路径**：
- **Phase G**（独立大任务）：实装 intra-procedural escape analysis，保守分析 + `[move]` 出口
- **Phase H**（更大）：跨函数 procedure summary，减少误报；或引入显式生命期标注

### 7.4 与 `[move]` 的关系

| 阶段 | `[move]` 角色 |
|------|---------------|
| Phase F（本计划） | 用户手动选 by-move；无安全保证（user discipline） |
| Phase G（逃逸分析） | 编译器检测到逃逸 by-ref 时**强制要求** `[move]`；变成 hard error |
| Phase H（生命期） | `[move]` 仍是显式出口；多数情况下编译器自动选最优策略 |

`[move]` 语法本身是**前向兼容**的——Phase F 加进来的代码到 Phase G 仍然合法。

---

## 八、风险与降级方案

| 风险 | 概率 | 应对 |
|------|------|------|
| F.3 struct 字段 Block 与现有 has_drop 链路冲突 | 中 | 先在小测试里验证 has_drop 传播；若 emit_struct_drop 变得过复杂，可推迟到 F.4 之后 |
| F.4 vec(Block) push 时 rvalue 闭包 env 接管漏 pop temp_block_envs | 中 | 沿用 var_decl 的 pop 模式，重点测试 push 多个 + 退场 |
| F.5 自递归 enum 捕获深度过大栈溢出 | 低 | 沿用现有 enum drop 限制；测试用例不构造极端深度树 |
| F.5 Option(int) 走错路径（误当 has_drop） | 中 | 检查 `c.type->as.enom.has_drop` 而非 enum 本身；测试覆盖 has_drop / no-drop 两版 |
| F.5 闭包内 match captured_enum 的 binder 双释放 | 中 | binder 标 is_borrowed=true，沿用 memcheck B Bug 5 修复模式 |
| CG_DEBUG 输出过载（10+ 行/语句） | 低 | 环境变量 `LS_CG_DEBUG_FILTER=cap,move,env` 选择性开启 |
| Block 赋值后源 alloca 错误重用 | 中 | Move 检查器接管；codegen 写 NULL 后 LLVM 优化自动消除冗余加载 |
| memcheck stress 发现循环引用 leak | 低 | Block 不能存自引用（`let g = \|...\| g(...)` 已被 move 检查器拒），不会发生 |
| F.8 嵌套闭包未拒，静默生成错码 | **高** | 必做：在 capture_walk 中拦截 AST_CLOSURE 子节点，给出明确编译错误；写 `tests/samples/closure_nested_reject.ls` 用例确认 |

---

## 九、与 CLAUDE.md 同步

Phase F 完成后，CLAUDE.md 更新点：

1. §1.2 已实现特性：加"闭包 `[move v]` 语法"、"Block 一等公民（赋值 / struct 字段）"
2. §4 实现阶段表：加 Phase 10 (closure F.1) – F.6 行
3. §6 已完成（近期）：每个 F.x 完成后加 dated 条目（与 E.x 同样格式）
4. §8 闭包捕获策略：8.2 节悬垂例子改为"工厂模式应使用 `[move]`"，移除"已知不安全约束"措辞
5. §7 所有权与 Move 语义：7.5 表加 Block 行（move 跟踪：✅ Phase F.2）

---

## 十、测试矩阵汇总

| 测试名 | 阶段 | 覆盖 |
|--------|------|------|
| test_phase_f1_move_vec | F.1 | `[move vec]` 工厂模式 + 栈污染验证（pollute）|
| test_phase_f1_move_map | F.1 | `[move map]` 工厂模式 |
| test_phase_f1_move_warn | F.1 | `[move POD]` / `[move string]` 等冗余 move 警告输出 |
| test_phase_f2_block_assign | F.2 | `g = h` 后 g 死亡，h 正常 drop |
| test_phase_f2_block_arg_move | F.2 | `f(__move(g))` 显式转移 |
| test_phase_f3_struct_field | F.3 | `Pipe { step1, step2 }` 全路径 |
| test_phase_f3_struct_mixed | F.3 | struct 含 Block + string + vec 三种 has_drop 字段 |
| test_phase_f4_vec_block | F.4 | vec(Block) push/iter/drop |
| test_phase_f4_map_block | F.4 | map(string, Block) set/get/drop |
| test_phase_f5_enum_pod | F.5 | 无 has_drop enum（Color/RGB）capture |
| test_phase_f5_enum_string | F.5 | has_drop enum（Tag(string)）capture + env_drop |
| test_phase_f5_enum_option | F.5 | 内建 Option(string) capture |
| test_phase_f5_enum_tree | F.5 | 自递归 Tree capture + box 释放 |
| test_phase_f5_enum_match_binder | F.5 | 闭包内 match captured_enum 的 binder 借用语义 |
| test_phase_f7_stress | F.7 | 1000 次创建销毁组合（Block + struct + vec + enum），内存稳态 |
| test_phase_f8_nested_reject | F.8 | 嵌套闭包字面量必须给出明确编译错误（若 F.8 未实施） |
| test_phase_f8_nested_closure | F.8 | 1 层嵌套 + `[move]` 透传（若 F.8 实施） |

每个测试同样的三重断言：JIT + AOT + memcheck（0 leak / 0 dfree）。

---

## 十一、估算汇总

| 阶段 | 内容 | 工作量 | 累计 | 必/可 |
|------|------|--------|------|-------|
| F.1 | `[move v]` + vec/map by-move | 3d | 3d | 必做 |
| F.2 | Block 赋值 + 移动语义 | 2d | 5d | 必做 |
| F.3 | Block 作为 struct 字段 | 3d | 8d | 必做 |
| F.4 | vec(Block) / map(K, Block) | 2d | 10d | 必做 |
| F.5 | enum capture + 完整 drop 模型 | 4d | 14d | 必做 |
| F.6 | CG_DEBUG 全面铺开 | 1d | 15d | 必做 |
| F.7 | Memcheck 大体检 + stress | 1d | 16d | 必做 |
| F.8 | 嵌套闭包字面量（若实施 3d；不实施时仅需 0.5d 写明确报错 + reject 测试） | 0.5d / 3d | 16.5d / 19d | 可选 |

**必做合计 ≈ 16 工作日**（F.1–F.7）；**含可选实施 ≈ 19 工作日**；不实施 F.8 但补 reject 测试 ≈ 16.5 工作日（推荐路径）。

---

## 十二、后续路径（Phase G+）

```
Phase F   完成本计划                                  (~16.5d)
   ↓
Phase F.8 嵌套闭包（如有需求）                        (+3d)
   ↓
Phase G   intra-procedural escape analysis           (~10d)
            ↓ 让 [move] 从 "user discipline" 变 "compiler enforced"
Phase H   procedure summary / 跨函数逃逸分析         (~15d)
            ↓ 减少 escape analysis 的保守误报
Phase I   完整生命期标注（Rust 路线）                 (~30d+)
            ↓ 引入 'a 标注，借用作返回类型 / struct 字段成为可能
```

每一步都是独立特性，不强制连续推进。Phase F 本身已显著提升语言能力与可控性。
