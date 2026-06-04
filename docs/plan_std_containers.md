# 计划：泛型标准容器库（std 容器）

> 状态：起草于 2026-06-03。前置条件「用户自定义泛型」已完成并通过测试
> （`ctest -R "generics|trait"` → 11/11，JIT+AOT）：泛型 struct/函数/`impl(T,U)`
> 方法、trait + trait bound（`fn f(T: A + B)(...)`、`struct W(T: Bound)`）均可用。
> 本文档规划在**纯 LS 语言层**实现一套泛型容器。

---

## 0. 设计前提与约束

| 维度 | 决策 |
|------|------|
| 实现层 | 纯 LS（`std/*.ls`），底层复用内建 `vec(T)` / `map(K,V)` / `enum` |
| 泛型实例化 | **显式类型实参，无推导**：`Stack(int)`、`new_stack(int)()` |
| 所有权 | 元素 `T` 一律值语义；容器 has_drop 时自动 drop 每个元素 |
| 比较/哈希 | 通过 trait bound 提供：先定义 `trait Ord` / `trait Eq`，约束型容器用 `(T: Ord)` |
| 内存验证 | 每个容器必须 JIT + AOT + `--memcheck` 三重过；memcheck SUMMARY 必须 0/0/0 |

### 0.0 ✅ Step 0 已完成（2026-06-04）：跨模块泛型实例化

**spike 成功，已落地并测试**。`import std.stack` + 在导入方调用点 `Stack(int)` /
`Stack(string)` / `new_stack(int)()` 端到端跑通（JIT + AOT + memcheck 0/0/0）。

**实际改动（约 70 行，符合评估区间）**：
- `src/checker.c` import 收集循环（~L8437）：泛型 struct decl → `register_struct_template`
  + 扫模块 AST 挂同名泛型 `impl`；泛型 fn decl → `register_fn_template`；均以
  `find_*_template_idx < 0` 预判**幂等去重**（不动同文件双声明的合法报错）。
- `src/codegen.c` struct 方法调用路径（~L11142）：补 free-fn 路径早有的**按需 forward-
  declare** 兜底——模块函数体在 pending-gm 块之前发射时引用泛型 struct 方法
  （`Stack(int).push`）不再报 `undefined method`。**这是 spike 中实测发现的真 bug**
  （由"模块内部使用导入容器"的传递场景触发），评估时未预见。

**测试**：`test_stack`（直接 import）+ `test_stack_xmod`（传递：helper 模块内部用
`Stack(int)`，main 直接用 `Stack(string)`，验证幂等注册 + 方法兜底）。全量 ctest 133/133。

**②（限定名）+ ①（同名冲突检测）已补（2026-06-04）**：
- ✅ **限定泛型类型** `mod.Stack(int)`（var/param/field/return 位置，单属模块）：parser
  var-decl 启发式支持 `mod.Type(args) var`（含护栏：变量名后须 `=`/`;`/EOF，避免把
  `r.append(x) r.append(y)` 同行多语句误判为声明）；checker 验证限定符确实指向拥有该泛型
  的模块。
- ✅ **跨模块同名泛型检测**：struct 模板带 `module_name` 归属；两模块导出同名泛型 →
  标记歧义；**裸用或限定用都给清晰错误**（替代原"静默取第一个"的隐患），对齐 B-4 非泛型行为。
- 测试：`test_stack_qual`（限定类型 JIT+AOT+memcheck）、`test_generic_ambig`（负向歧义）。

**仍推迟（确认未做）**：
- ⛔ **两模块同名泛型容器"同时可用"**：这是 B-full 命名空间级大特性，需 ① 表达式位限定泛型
  解析（`a.Stack(int){...}` 字面量 + `a.new_stack(int)()` 调用，现解析层仅认裸 IDENT
  callee）+ ② codegen 实例名/方法符号按模块前缀（否则两实例都 mangle 成 `Stack(int)` →
  缓存命中第一个 → 静默错值）。当前以清晰错误挡住，避免静默损坏。
- 跨模块 trait-bound 容器（heap 之前需补"跨模块 trait/impl 注册"小档）。
- 跨模块泛型体引用模块私有 helper（作用域解析失败，限定只用参数/内建/已导出符号）。

---

<details><summary>（历史）Step 0 阻塞分析与评估（2026-06-03，已被上方完成项取代）</summary>

#### ⛔ 前置阻塞（2026-06-03 实测发现）：跨模块泛型实例化未支持

把容器做成 `std/*.ls` 模块、再 `import` 使用，**当前不可行**。实测：`import std.stack`
后 `Stack(int)` 报 `unknown generic type 'Stack(int)'`。根因（src/checker.c）：

- 泛型 struct 的 `check_struct_decl`（~L6946）`tpc>0` 时提前 return，**不设
  `d->resolved_type`**，模板只注册在模块自身 checker（随后丢弃）。
- import 收集循环（~L8437）只导出 `AST_STRUCT_DECL && d->resolved_type` 的非泛型
  struct；泛型 impl 收集（~L8506）只处理 `type_param_count==0`。
- 现有跨模块泛型机制（A1，~L8379）只搬运**模块内部已实例化**的泛型函数实例
  （`l0091_modgen`：`mod_a.use_int()` 内部调 `box(int)`）；**导入方在自己的调用点
  实例化导入模块的泛型（函数或 struct）从不支持**。

→ **容器逻辑本身已验证可用**（见下），但要把它们作为可 import 的 std 模块，必须先补一个
语言特性：**跨模块泛型模板导出 + 导入方实例化**（checker 导出 struct/fn 模板 +
导入方 `find_struct_template_idx` 命中 + codegen 在导入方按模块前缀单态化 struct/impl/
drop/clone）。这是独立工作项，列为容器库 **Step 0**，先于一切容器模块。

在 Step 0 落地前，容器可先以**同文件定义**形式验证逻辑与 memcheck（`std/stack.ls` 已写好，
逻辑经 in-file 等价测试 + memcheck 0/0/0 验证通过）。

#### Step 0 实施评估（2026-06-04 摸排 checker.c + codegen.c）

**核心结论**：缺口比预期小。整条单态化机器与"模板来自哪个模块"**无关**且已验证：
- `checker_instantiate_struct`（建实例类型 + trait bound 检查 + 字段代换）
- `instantiate_impl_method_types`（克隆方法体、注册 impl 表、入队 `pending_generic_methods`）
- 泛型 fn 调用点实例化（checker.c ~L4369-4562，按 `module_name` 前缀符号 + 入队）
- codegen 发射（~L20815 forward-decl+body、~L11237 调用点按 mangled name 解析、Pass 2.5
  自动 drop）——**全部 origin-agnostic**，本地泛型已 memcheck-clean。

**唯一断点**：import 收集循环（checker.c ~L8429）只导出 `resolved_type != NULL` 的非泛型
声明；泛型 struct/impl/fn 模板从未注册进 importer 的 checker → `find_struct_template_idx`
落空。**把导入模块的泛型模板注册进 importer，剩余机器自动接管。**

**改动面（happy path，自包含容器）**：

| 改动 | 位置 | 规模 |
|------|------|------|
| import 循环：泛型 struct decl → `register_struct_template` + 扫同名泛型 impl 挂 `.impl_node` | checker.c ~L8437 | ~30 行 |
| import 循环：泛型 fn decl → `register_fn_template` | checker.c ~L8490 | ~10 行 |
| `register_struct_template`/`register_fn_template` 重复注册改**幂等去重**（不再报错） | checker.c L228 | 小但必须 |
| 注册放"export 收集循环"（每 importer 都跑），不放 `if(!mod->checked)` 内 | checker.c | 设计点 |

**风险点（排序）**：
1. 跨模块泛型方法/构造体在 **importer 作用域**内重新类型检查 → 体内引用模块私有 helper
   会解析失败。限定：跨模块泛型体只可引用其参数 / 内建 / 已导出符号。std.stack 自包含，OK。
2. 单态化符号 `Stack(int).push` 由实例化所在 checker 的 `module_name` 前缀；单模块唯一安全，
   **两模块同名泛型容器**会撞 → v1 不支持。
3. trait-bound 容器（heap）需 importer 注册 trait+impl → 额外一档，stack 不触发。
4. 泛型 + 限定名 `mod.Type(args)` 不支持（B-4 仅 arg_count==0）；v1 用裸名 `Stack(int)`。

**推迟**：同名泛型容器跨多模块、`mod.Type(args)` 限定名、跨模块 trait-bound 容器。

**测试**：`tests/samples/stack_test.ls`（`import std.stack`）三段式（JIT+AOT+memcheck 0/0/0，
复用 test_move_elision.cmake 风格）+ 双 import 去重回归 + 全量 `--repeat until-pass:2`。

**工作量**：核心 happy path 估 checker **50–80 行** + 调试；把握较高（下游全现成、断点单一）；
纯增量，不动已绿的本地泛型路径，回归面小。顺利约半天，踩风险点 1/命名细节可能翻倍。

</details>

### 0.1 关键未知数（必须按容器逐一验证）

泛型 G1 测试只覆盖了泛型 struct 的 **string** 字段 drop。以下路径**尚无 memcheck 覆盖**，
是本计划每一步的真实风险点，必须用测试逐格验证：

1. 泛型 struct 含 **`vec(T)` 字段**时，单态化后的自动 drop / clone 是否正确 → **std.stack 首先验证**。
2. 泛型 struct 含 **`map`** 字段 → std.set / std.counter 验证。
3. 泛型 **递归 enum**（`enum Node(T) { Cons(T, box Node(T)), Nil }`）的 drop → std.list 验证。
4. 泛型 **impl + `&!self`** 可变方法（g1 只测了只读隐式 self）→ std.stack 顺带验证。
5. trait bound 容器里对 `T` 调 trait 方法（比较）在单态化下的正确性 → std.heap 验证。

> 原则：**每个容器先当成一次"未测泛型路径"的探针**，跑通 memcheck 再加功能。

---

## 1. 容器路线图（按风险递增 / 价值落地排序）

### 第一梯队：纯结构型（零约束，只搬运 T）

| 模块 | 类型 | 底层 | 探针目标 |
|------|------|------|----------|
| **std.stack** | `Stack(T)` | `vec(T)` | 泛型 struct + vec(T) 字段 drop/clone；泛型 `&!self` |
| **std.deque** | `Deque(T)` | `vec(T)` + 双指针/环形 | 进阶索引；Queue/Stack 退化复用 |
| **std.list** | `List(T)` | 递归 `enum Node(T)` + box | 泛型递归 enum drop（最深的 RAII 验证） |
| **std.ring** | `Ring(T)` | 定容 `vec(T)` | 覆盖写语义 |

### 第二梯队：约束型（需 trait bound + 先立标准 trait）

先在 `std/cmp.ls`（或内建约定）定义：

```
trait Eq  { fn eq(&self, other: &Self) -> bool }
trait Ord { fn lt(&self, other: &Self) -> bool }   // 仅需 lt，其余可派生
```

| 模块 | 类型 | 底层 | 探针目标 |
|------|------|------|----------|
| **std.heap** | `BinaryHeap(T: Ord)` | `vec(T)` | trait bound 容器；优先队列 |
| **std.set** | `Set(T: Eq)` | `vec(T)` 或 `map` | 自定义 key 类型集合（超出内建 map） |
| **std.sortedmap** | `SortedMap(K: Ord, V)` | `vec` of pair | 有序映射，补 map 无序空缺 |

### 第三梯队：组合型

- **std.graph**：`map(int, vec(int))` + BFS/DFS/拓扑/Dijkstra（复用 heap）
- **std.lru**：`map` + 双向链表
- **std.counter**：`map(K, int)` 多重集 / 词频

---

## 2. 通用 API 约定

为保持各容器一致、可组合，统一动词约定：

| 操作 | 命名 | 备注 |
|------|------|------|
| 构造空容器 | `new_<name>(T...)()` | 自由函数，返回拥有所有权的空容器 |
| 加入元素 | `push` / `add` / `insert` | 移动 T 进容器 |
| 取出元素 | `pop` / `remove` | 移动 T 出容器（无 clone） |
| 只读查看 | `peek` / `top` / `get` | 返回 **clone**（has_drop T 深拷贝） |
| 大小 | `len(&self) -> int` | O(1) |
| 空判断 | `is_empty(&self) -> bool` | |
| 清空 | `clear(&!self)` | drop 所有元素 |

**所有权要点**：`pop` 走 move-out（无 clone），`peek/get` 走 clone（底层 `vec.last/get`
已对元素 `emit_clone_value`），调用方拿到独立 owned 值。

---

## 3. 落地顺序与验收

1. **std.stack（本批）**：`Stack(T)`，验证第一梯队 + 未知数 1/4。测试 `test_stack`：
   - Stack(int)：push/pop/peek/len/is_empty/clear 值正确（JIT+AOT）。
   - Stack(string)：has_drop 元素 push/peek(clone)/pop(move)；**残留元素随容器析构**。
   - memcheck SUMMARY 0 leak / 0 double-free / 0 invalid free。
2. std.list（递归 enum 探针）。
3. 立 `trait Ord` + std.heap（trait bound 探针）。
4. 其余按梯队推进；组合型最后。

每步若 memcheck 报泄漏/双释放 → 先停在该步，定位单态化 drop/clone 缺陷并修复，再继续。

---

## 4. 已知限制（继承自语言层）

- 实例化无类型推导：调用点需显式写类型实参。
- by-ref 捕获的 vec/map 闭包不能 outlive 外层变量（见 closures_plan.md）——容器内若存
  Block(T) 需注意。
- REPL L-010：跨 snippet 反复 drop imported has_drop 类型可能段错误；容器以 `ls run`
  文件方式验收，不依赖 REPL。
