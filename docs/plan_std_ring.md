# 计划：std.ring — rte_ring 风格环形缓冲（单线程版 + 原子化预留）

> 状态：起草 2026-06-04。前置「跨模块泛型」已完成（见 plan_std_containers.md Step 0）。
> 目标：在**纯 LS 语言层**实现一个借鉴 DPDK `rte_ring` *数据结构设计*的定容环形缓冲，
> **当前单线程**，但 API 与内部布局**为将来的无锁 MP/MC 版本预留演进空间**。

---

## 0. 背景：rte_ring 是什么，我们取它的哪一半

DPDK `rte_ring` = **环形缓冲算法** + **无锁多生产者/多消费者并发**。两层在 LS 里命运不同：

| rte_ring 特征 | 本质 | LS 现状 | 本计划 |
|---|---|---|---|
| 2 的幂容量 + `idx & mask` 索引 | 整数运算 | ✅ | **采用** |
| 自由递增游标（free-running，不归零）| 索引语义 | ✅ | **采用**（原子化前提）|
| 定容、满策略（拒绝/覆盖）| 逻辑 | ✅ | 采用（v1 拒绝）|
| burst/bulk 批量 | 循环 | ✅(部分) | 部分（见 Q7）|
| 存 POD/指针、**不负责析构** | 数据布局 | ✅ | 扩展为**支持 has_drop T**（槽位模型）|
| **prod/cons head/tail 四游标 CAS 两阶段** | 原子 cmpxchg | ❌ 无原子 | **预留**，不实现 |
| **MP/MC 多线程** | 线程 + CAS 竞争 | ❌ 无线程 | **预留**，不实现 |
| cache-line 对齐防伪共享 | `alignas(64)` | ❌ 无对齐控制 | 预留 |

**实测确认（2026-06-04）**：LS 无任何原子/线程/对齐原语（codegen 无 atomic/cmpxchg/fence，
运行时无线程）。因此 v1 = rte_ring 的**算法骨架**（mask 索引 + 自由游标 + 定容），
并发那一半作为独立「并发底座」工作项预留。

---

## 1. v1 设计（单线程 SP/SC 形态）

### 1.1 类型与内部布局

```
struct Ring(T) {
    vec(Option(T)) buf    // 定长 size 个槽位，初始全 None
    int            cap    // 可用容量 = size（2 的幂）
    int            mask   // size - 1，用于 idx & mask 快速取模
    int            prod   // 写游标（自由递增）—— 对应 rte_ring 的 prod_tail
    int            cons   // 读游标（自由递增）—— 对应 rte_ring 的 cons_tail
}
```

- **元素所有权（关键）**：槽位用 `Option(T)`，不是裸 `T`。定容 vec 的所有 `size` 个槽
  都被自动 drop 覆盖，若用裸 `T` 则死槽/未初始化槽会被误析构。`Option(T)`：活元素是
  `Some(payload)`（drop 一次），空槽是 `None`（不 drop）。**已 spike 验证 memcheck 0/0/0**。
- **count = prod - cons**（自由递增游标相减）；`empty` ⇔ count==0；`full` ⇔ count==cap。
  自由游标使我们能用满 `size` 个槽（无需 rte_ring 经典的"保留一槽"技巧）。

### 1.2 公开 API（与未来 MP/MC 版**保持一致**）

| 方法 | 签名 | 语义 |
|---|---|---|
| 构造 | `new_ring(T)(int capacity) -> Ring(T)` | capacity 向上取 2 的幂；槽位填 None |
| 入队 | `enqueue(&!self, T x) -> bool` | 满则返回 false（x 未被消耗——见 Q7a）|
| 出队 | `dequeue(&!self) -> Option(T)` | 空则 None；否则 Some(payload)（move 出）|
| 批量出 | `dequeue_burst(&!self, int n) -> vec(T)` | 出队至多 n 个，返回 owned vec |
| 容量 | `capacity(&self) -> int` / `len(&self) -> int` | size / 当前元素数 |
| 判断 | `is_empty(&self) -> bool` / `is_full(&self) -> bool` | |
| 余量 | `free_space(&self) -> int` | cap - len |
| 清空 | `clear(&!self)` | 所有槽置 None（drop 活元素），prod=cons=0 |

实现要点：
- `enqueue`：满→false；`buf[prod & mask] = Some(x)`（x move 进 Some 进槽，旧 None 被 drop）；`prod++`。
- `dequeue`：空→None；`Option(T) v = buf.get(i)`（clone 出 Some）；`buf[i] = None`（drop 原件）；`cons++`；返回 v。
  - ⚠️ **每次 dequeue 一次深拷贝**：vec 无"移动取出"原语，只能 clone 后置 None。POD/指针 T
    时 clone 即 memcpy（≈免费，正是 rte_ring 典型用法）；has_drop T 时是深拷贝（见 Q6 优化项）。
- `dequeue_burst`：循环 dequeue→match Some(v)→`out.push(v)`，返回 out。

### 1.3 测试（v1 验收）
`test_ring`：JIT + AOT + memcheck 0/0/0：
- Ring(int)：环绕（绕过 size 边界仍 FIFO 正确）、满拒绝、空返回 None、burst、clear。
- Ring(string)：has_drop 元素入/出/残留析构 + clear 中途 drop 全干净。

---

## 2. 原子化 / 无锁 MP/MC 演进（预留，**不在 v1**）

**设计承诺：公开 API（§1.2）跨 SP/SC → MP/MC 不变**，只换内部游标推进方式。具体演进：

### 2.1 内部布局变化（v1 已为此设计）
- 游标从 2 个（`prod`/`cons` = *_tail）扩为 4 个：
  `prod_head` / `prod_tail` / `cons_head` / `cons_tail`。
- v1 的 `prod`/`cons` 即未来的 `prod_tail`/`cons_tail`，**自由递增 + mask** 的选择正是为此预留。
- enqueue 变两阶段：CAS 推进 `prod_head` 预定 n 个槽 → 写入 → 自旋等 `prod_tail` 追平后提交。
- `Option(T)` 槽位模型、`mask`、容量策略、**全部不变**。

### 2.2 需要的语言底座（关键路径，按依赖排序）
1. **原子 intrinsic**（中等，硬前提）：`atomic_load/store/cas/fetch_add/fence`，codegen 发射
   LLVM `load atomic`/`store atomic`/`cmpxchg`/`atomicrmw`/`fence`。无此无法无锁。
2. **线程**（中-大）：FFI Win32 `CreateThread` / pthreads，或 `std.thread` 内建。无线程则 MP/MC 无意义。
3. **内存对齐**（小）：struct/分配 `align(64)`，prod/cons 分置不同 cache line 防伪共享。
4. **备选快路**：FFI 直接封装 C 的真 rte_ring/无锁库（LS 已有 `load(dll)`+`extern fn`）——
   最快拿到"真无锁环"，但本质是包 C，非 LS 语言层实现。

> 这条线本质是给 LS 开「并发」这扇门，影响远超一个 ring，应作为独立大特性单独立项评估。

---

## 3. 详细计划问题（待定决策）

- **Q1 游标类型**：v1 用 `int`(i64) 单调自由递增（简单、实际不溢出）。原子版 rte_ring 用
  i32 自然回绕（CAS 宽度）。→ v1 取 i64；原子化时另评是否切 i32 回绕语义。
- **Q2 容量策略**：v1 向上取 2 的幂（易用）。是否改为"必须传 2 的幂否则报错"（更显式）？→ 倾向取整 + 暴露 `capacity()` 让用户看到实际值。
- **Q3 满策略**：v1 `enqueue` 满→拒绝（false）。是否再加 `enqueue_overwrite`（覆盖最旧，LRU/最新 N 条日志场景）？→ v1 只做拒绝，覆盖版作为 Q3 后续。
- **Q4 出队返回**：v1 `dequeue() -> Option(T)`（惯用）。vs `(bool, out)`？→ 取 Option(T)。
- **Q5 元素模型**：v1 `Option(T)` 槽位（支持 has_drop T，已验证）。是否再提供 POD/指针专用快路
  （无 Option、无 per-dequeue clone，最贴 rte_ring "纯搬运" 语义）？→ 通用版优先；POD 快路作为性能后续。
- **Q6 移动取出原语**：vec 缺"move-out"（取出并失效槽，不 clone/不 drop）。补上可同时消除
  `dequeue` 与 `Stack.pop` 的深拷贝。→ 独立语言改进项（受益面广）。
- **Q7 批量方向**：
  - Q7a `enqueue_burst(&!vec(T))`：需从 src vec 中段移出元素 → 被 Q6（move-out）阻塞 →
    v1 不做（用户循环 `enqueue`）。
  - Q7b `dequeue_burst(int) -> vec(T)`：方向友好（push 进新 vec）→ **v1 实现**。
- **Q8 并发底座优先级**：原子 intrinsic（§2.2 第 1 项）是否值得单独立项？何时？→ 待用户定。

---

## 3.9 v1 实现状态（2026-06-04 完成）

✅ `std/ring.ls` + `test_ring`（JIT+AOT+memcheck 0/0/0，Ring(int) 与 Ring(string) 并存，
含环绕/满拒绝/空 None/burst/clear/has_drop 析构）。全量 ctest 136/136。

**实现中发现并修复的 3 个真编译器 bug（均非 ring 专属，价值外溢）**：
1. **跨模块泛型体引用模块私有 helper**（已知限制，见 plan_std_containers §0.0）：`new_ring`
   原调用模块私有 `_ring_next_pow2`，在导入方作用域重新检查时不可见 → 报 undefined。
   **绕过**：把 pow2 内联进 `new_ring`。泛型体只可引用参数/内建/已导出符号。
2. **`s.vecfield[i] = x` / `s.mapfield[k] = v` 静默不写**（codegen，**危险静默正确性 bug**）：
   index-assign 的目标 vec/map 地址只处理 `obj->kind == AST_IDENT`（裸局部），对 struct
   字段 `vec_alloca` 保持 NULL 直接 return → 写入丢失。修复：改用 `codegen_lvalue_ptr(obj)`
   取真实地址（src/codegen.c ~L14292/14330）。影响所有"持有可索引容器字段的 struct"。
3. **多 enum 实例时裸变体名歧义**（checker）：`push(None)` / `self.buf[i] = Some(x)` 的
   RHS 变体名在 Option(int)+Option(string) 并存时歧义——`find_variant` 本可用 expected_type
   消歧，但 push（仅 Block 时 plumb）与 assign（根本没 plumb）没设 expected_type。修复：
   vec.push 对所有 elem 类型 plumb `expected_type=elem`；AST_ASSIGN 检查 RHS 前 plumb
   `expected_type=target`（src/checker.c）。

## 4. 落地顺序
1. ✅ **v1（已完成）**：`std/ring.ls`（§1）+ `test_ring`（JIT+AOT+memcheck）。
2. 视需要：覆盖策略（Q3）、POD 快路（Q5）、move-out 原语（Q6，跨容器收益）。
3. 独立大项：并发底座（原子 + 线程 + 对齐）→ 解锁真·无锁 MP/MC ring（§2）。
