# 设计：`for x in v` 迭代协议（`Iterator(T)` trait + 手写迭代器）

> ✅ **已实现（基础设施）2026-06-07**：`for x in v`（v 为纯 LS `Vec(T)`）走迭代协议脱糖。
> - **检测**（checker `AST_FOR`）：iter 表达式类型为 `TYPE_STRUCT` 且其 impl 含 `iter()`（取迭代器）
>   或 `next()`（本身即迭代器）→ 进脱糖路径。
> - **脱糖**（checker `build_foreach_desugar`，AST 层）：合成 §6 的
>   `{ [Iter __src=expr] Iter __it=src.iter(); while true { match __it.next(){ Some(x)=>{BODY} None=>break } } }`
>   子树，存于 `for_stmt.desugared`，对其 `check_stmt`；lvalue 源（裸 IDENT）借址迭代，
>   非 IDENT 源物化到 `__src` 局部活过循环（§6.1/§6.2）。BODY/iter 经 `ast_clone_deep` 拷入，
>   原 `for_stmt.body` 不动（供泛型模板再克隆）。配套：checker 支持 `var_type==NULL` 的类型推断局部
>   （仅脱糖器合成 `__it`/`__src` 用）。
> - **codegen**：`AST_FOR` 见 `desugared != NULL` 即 `codegen_stmt(desugared)` 后 `break`，
>   完全复用既有 while/match/方法调用机器；内建 `int`/`array`/`vec(T)` 三段专属路径不变（§8.4 待退役）。
> - **std/vec.ls**：新增 `struct VecIter(T){ *T data; int len; int i }`（非 has_drop，持裸指针不拥有 buffer）
>   + `impl(T) VecIter(T){ fn next(&!self)->Option(T) }`（clone-on-read → `Some(e)`）+ `Vec.iter(&self)->VecIter(T)`。
> - **测试** `tests/samples/iter_protocol_test.ls`（`test_iter_protocol`，JIT+AOT+memcheck 0/0/0）：
>   Vec(int/string/Person)、break/continue、空容器、rvalue 源、嵌套双迭代器。ctest 161/161。
> - **未做**（本期范围外，见 §9）：`Iterator(T)` trait 形式化（当前按方法名检测，未做 trait 一致性校验）；
>   Map/MapIter（待 std.map）；按借用产出 `for &x in v`；惰性适配器；for-in 解构。
>   `next()` 返回类型未深校验 Option（错则由合成 match 自然报错）。

> 状态：定稿 2026-06-07（分支 `feat/rawvec`）。**本版取代早期"`len()+__index` 索引脱糖"草案**
> （那只是应急、且踩 VR-LIM-008，见 §3 否决说明）。
> 来源：[plan_vec_replacement.md](plan_vec_replacement.md) §6.1 VR-LIM-001。
> 规模：语言特性（trait dispatch + AST 层脱糖），中等偏上 → 建议 ④ Codex+GPT5.5 或 ① 自做，
> **不**适合中段 agent。

---

## 1. 决策摘要
- **核心是外部迭代器 trait** `Iterator(T) { fn next(&!self) -> Option(T) }`。
- **`for x in EXPR` 脱糖**为 `while true { match it.next() { Some(x) => {BODY} None => break } }`
  （纯 AST 层脱糖，复用既有 trait / `Option` / `match` / `while`/`break`）。
- **具体迭代器（`VecIter` / `MapIter` …）一律手写**（在标准库），**编译器不自动生成**任何
  迭代器（见 §5 决策与理由）。
- 集合通过约定方法 `iter(&self) -> XxxIter` 暴露迭代器；`for x in coll` 脱糖时若 `coll`
  本身已是 `Iterator(T)` 则直接驱动，否则先 `coll.iter()`。

---

## 2. 现状与问题
`for x in v` 目前只对内建可迭代对象有**专属 codegen**：整数 `n`、`array(T,N)`、内建 `vec(T)`
（`codegen.c:15460-15534`）。纯 LS `Vec(T)` 是 has_drop struct，不命中任何分支 → 迁移后
`for x in v` 不可用（§6.1 绕行：改写为索引循环）。需要一个**面向用户容器的通用迭代协议**。

---

## 3. 为什么用 `next()` 模型，而非"索引脱糖"
早期草案把 `for x in v` 脱糖成 `for(i){ T x = v[i] }`（依赖 `len()+__index`）。**否决**，三条：
1. **只服务随机访问容器**：map / 链表 / 树 / 惰性·无限序列都表达不了（map 没有"第 i 个条目"
   的 O(1) 概念，见 §7 MapIter）。索引脱糖是迭代故事的死胡同。
2. **踩 VR-LIM-008**：`v[i]` 走 `Vec.__index` 的"按值读出 + field 消费"未成熟路径，每轮可能
   漏 drop。
3. **`next()` 反而落在成熟机器上**：元素由 `Some(x)` 绑定 → 走 **L-012 已修的"match 拥有的
   rvalue enum 主体会析构"**路径，**天然规避 008**（见 §6 drop 追踪）。

---

## 4. `Iterator(T)` trait
```ls
trait Iterator(T) {
    fn next(&!self) -> Option(T)
}
```
**与 Rust 的差异（有意为之）**：
- Rust 用**关联类型** `type Item`；LS 无关联类型 → 用**泛型参数** `Iterator(T)`。代价：一个
  类型可同时 `impl Iterator(int)` 与 `Iterator(string)`（Rust 关联类型禁止）——对 LS 无害，
  且 LS 的泛型 trait + 单态化（G1）原生支持。
- Rust 的 `Iter<'a,T>` 持生命期受检借用、产 `&T`；LS 无生命期系统 → 迭代器持**裸指针**、
  **按值产出**（has_drop 元素 clone-on-read）。按借用产出留作今后（§9）。
- `IntoIterator` 暂不形式化（§9），先用约定的 `iter()` 方法桥接。

---

## 5. 具体迭代器一律手写（编译器不自动生成）

**决策：标准库手写每个容器的迭代器结构体 + `Iterator(T)` impl + `iter()`；编译器不做任何
自动派生。**

理由：
- **无编译器魔法**：自动从 `len()+__index` 合成迭代器会引入一类隐式生成的类型/impl，增加
  编译器复杂度与调试难度；与 LS"显式、可读"的取向相悖。
- **反正非索引容器必须手写**：map/链表/树/惰性序列的 `next()` 无法从索引协议推导（§7），
  本就要手写。与其"索引容器自动、其余手写"两套规则，不如**统一手写**一套规则，简单一致。
- **手写成本可控**：随机访问容器的迭代器极小（裸指针 + 游标 + 几行 `next()`，见 §6）；
  标准库写一次，用户容器作者按同一范式照抄。

> 即：迭代器是普通的、显式定义的标准库类型；`Vec`/`Map` 的 `iter()` 与 `XxxIter` 都写在
> `std/vec.ls`/`std/map.ls` 里，和其他方法一样被单态化、被 RAII 管理。

### VecIter（随机访问容器示范）
```ls
struct VecIter(T) { *T data; int len; int i }      // 裸指针 backing，非 has_drop（不拥有 buffer）

impl(T) Iterator(T) for VecIter(T) {
    fn next(&!self) -> Option(T) {
        if self.i >= self.len { return None }
        T e = self.data[self.i]                     // has_drop T 在此 clone-on-read，绑定到具名局部
        self.i = self.i + 1
        return Some(e)                              // move 进 Option
    }
}

impl(T) Vec(T) {
    fn iter(&self) -> VecIter(T) {                  // &self 只读借用 v；v 之后仍可用
        return VecIter(T){ data: self.data, len: self.len, i: 0 }
    }
}
```
> 注：`next()` 里 `T e = self.data[i]` 与现有 `Vec.get`（`std/vec.ls:128`）同款 clone-on-read，
> 绑定到具名局部 `e` 后 `move` 进 `Some` —— 走 move + match 成熟路径，不碰 008 的 readout 洞。

---

## 6. `for x in v` 脱糖

### 6.1 源是具名变量（lvalue）—— 最常见
```ls
// 原始
for x in v { print(x) }

// 脱糖后
{
    VecIter(T) __it = v.iter()           // 借用 v 的 buffer；v 之后仍可用
    while true {
        match __it.next() {
            Some(x) => { print(x) }      // 原 BODY 原样进 Some 臂
            None    => { break }
        }
    }
    // __it 非 has_drop（裸指针不拥有 buffer）→ 无 drop
}
```

### 6.2 源是临时 rvalue（如 `for x in make_vec()`）—— 须先物化
`XxxIter` 持裸指针；源是临时时**必须先绑定到具名局部、活过整个循环**，否则 buffer 在循环里
就被释放 → 悬垂。
```ls
// 原始
for x in make_vec() { print(x) }

// 脱糖后
{
    Vec(T) __src = make_vec()            // 拥有该 vec，活到 block 末
    VecIter(T) __it = __src.iter()
    while true {
        match __it.next() { Some(x) => { print(x) }  None => { break } }
    }
    // block 退出：__src.__drop() 释放 buffer（迭代结束之后）
}
```
（与现有内建 vec 的 `for_iter_tmp` 机制同构：非 IDENT 源求值到临时、循环后整体 drop，
`codegen.c:15527`。）

### 6.3 break / continue
`Some` 臂里的 BODY 中，`break`/`continue` 穿过 `match`（match 不是循环）作用到外层 `while`：
`break` 退出循环、`continue` 回到 `__it.next()` 取下一个 —— 语义正确。

### 6.4 drop / 所有权追踪
1. `v.iter()`：`&self` 只读借用，不动 v 所有权；`__it` 是裸指针游标，无 drop 负担。
2. `__it.next()`：`&!self` 原地推进 `i`；有元素时 clone-on-read → `Some(e)`。
3. `match __it.next()`：主体是 `next()` 返回的 **owned rvalue `Option(T)` 临时** → 落
   **L-012 成熟路径**（match 拥有的 rvalue enum 主体会析构）。
   - `Some(x)`：`x` 绑定为臂作用域内拥有局部，BODY 用完后按 has_drop **正常 drop**。
   - `None`：空 payload，`break`。
4. lvalue 源：无额外清理；rvalue 源：`__src` scope 退出 drop buffer。

---

## 7. Map 示范：`MapIter`（非随机访问 → 必须手写 `next()`）
map 没有 O(1) 的"第 i 个条目"，**无法**索引脱糖——这正是 `next()` 模型不可替代之处。
LS 无元组 → 键值对用结构体 `Entry(K,V)`，迭代器 `impl Iterator(Entry(K,V))`。

```ls
struct Entry(K, V) { K key; V val }

// 假设纯 LS Map(K,V) 用开放寻址：struct Map(K,V){ *Slot(K,V) slots; int cap; int len }
//                                struct Slot(K,V){ bool used; K key; V val }
struct MapIter(K, V) { *Slot(K,V) slots; int cap; int i }

impl(K, V) Iterator(Entry(K, V)) for MapIter(K, V) {
    fn next(&!self) -> Option(Entry(K, V)) {
        while self.i < self.cap {
            int at = self.i
            self.i = self.i + 1
            if self.slots[at].used {                 // 跳过空槽
                K k = self.slots[at].key             // 仅占用槽 clone-on-read（K 与 V）
                V vv = self.slots[at].val
                return Some(Entry(K,V){ key: k, val: vv })
            }
        }
        return None
    }
}

impl(K, V) Map(K, V) {
    fn iter(&self) -> MapIter(K, V) { return MapIter(K,V){ slots: self.slots, cap: self.cap, i: 0 } }
    // 可选：keys()->KeysIter / values()->ValuesIter，各自手写 next()
}
```
- 链式哈希则 `MapIter{ *Bucket buckets; int cap; int bucket_i; *Node node }`，`next()` 先走当前
  链、链尽推进到下一非空桶——**接口 `next()->Option(Entry)` 不变**。
- `for e in m { e.key; e.val }` 脱糖形状与 §6 **完全一致**（仅 `T = Entry(K,V)`）。这正是 trait
  统一接口的价值：脱糖器只认 `next()->Option(T)`，不关心底层是连续 buffer 还是哈希桶。

---

## 8. 实现计划
### checker
1. for-in 的 iter 表达式类型解析：
   - 若其类型 `impl Iterator(T)`（有 `next(&!self)->Option(T)`）→ 直接驱动，循环变量类型 = `T`。
   - 否则若有 `iter(&self)->I` 且 `I impl Iterator(T)` → 先 `.iter()`，循环变量类型 = `T`。
   - 都不满足 → 报错 "type 'S' is not iterable (expected `Iterator(T)` or an `iter()` method)"。
2. 标注 for-in 节点为"协议迭代 + 元素类型 T + iter/next 解析结果"，供脱糖用。

### 脱糖（优先 AST 层，尽量不写专属 codegen）
3. 把 `for x in EXPR { BODY }` 改写为 §6 的 `{ [src 物化] XxxIter __it = ...; while true {
   match __it.next() { Some(x)=>{BODY} None=>{break} } } }` AST 子树，交由既有 while/match/
   方法调用 codegen 处理。lvalue 源借用、rvalue 源物化到 `__src` 两种形态按 §6.1/§6.2 区分。
4. 长远：内建 `int`/`array`/内建 `vec` 的三段专属 for-in codegen（`codegen.c:15460+`）可在
   Phase 3 后统一退役（届时只剩协议脱糖一条路径）。

---

## 9. 今后改进空间（非本期）
- **按借用产出（最重要）**：`next(&!self) -> Option(&T)`，配 `for &x in v`（只读）/
  `for &!x in v`（可写）。消除 has_drop 元素的 clone-on-read 开销（map 尤甚，省 K+V 双 clone）。
  **前置**：借用作返回类型 / struct 字段（生命期系统，CLAUDE.md §6 待实现）——迭代器届时可
  持**受检借用** `&Vec(T)` 而非裸指针，安全性从"脱糖保证"升级为"类型系统保证"。
- **`IntoIterator(T)` 形式化**：区分 `iter()`（借用迭代）/ `into_iter()`（消费迭代，move 出
  元素、零 clone）/ `iter_mut()`（可写迭代）。让 `for x in v`（消费）与 `for x in &v`（借用）
  语义分流，对齐 Rust 三态。
- **惰性适配器**：`map`/`filter`/`take`/`enumerate`/`zip` 作泛型 struct `impl Iterator`，
  组成 `v.iter().map(...).filter(...).take(5)` 惰性管道——**零中间分配**，性能与表达力双赢
  （需 `where I: Iterator(T)` 约束，G1 已支持）。
- **for-in 结构解构**：`for Entry{key, val} in m`（或 `for (k, v) in m` 若将来引入元组），
  免去 `e.key`/`e.val` 样板。属 for-in binder 的语法糖，不影响核心协议。
- **零成本验证**：`next()`+`Option`+`match` 每轮开销靠 O2 内联摊平成紧凑循环（Rust 迭代器
  同款）。需用 alloc/iter bench 实测确认内联生效——这是该模型唯一的性能风险点。

---

## 10. 测试
`tests/samples/iter_protocol_test.ls`（JIT+AOT+memcheck 0/0/0）：
- `Vec(int)` / `Vec(string)` / `Vec(Person)` 的 `for x in v`（含读字段）。
- rvalue 源 `for x in make_vec()`（临时迭代后 drop）。
- `break` / `continue` / 空容器（0 次）。
- `Map(K,V)` 的 `for e in m`（`e.key`/`e.val`）一旦 std.map 就绪。
- 嵌套 `for x in v { for y in v { ... } }`（两个独立迭代器，互不干扰——印证迭代器与集合分离）。

---

## 11. 在 vec 替换中的位次
- **取代** VR-LIM-001 的索引脱糖绕行。原"依赖 VR-LIM-008"的注记**作废**——本设计走 match
  payload 成熟路径，**不依赖 008**，故可与 008 解耦并行（甚至先行）。
- 排程：作为独立语言特性推进；`Vec.iter()/VecIter` 落在 `std/vec.ls`，`Map`/`MapIter` 待
  map 也纯 LS 化（std.map）后补。
- 归属：trait dispatch + 脱糖，**④ Codex+GPT5.5 或 ① Opus**；非中段 agent。
