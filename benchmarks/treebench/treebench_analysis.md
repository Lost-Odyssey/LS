# treebench — 递归数据结构（树/链表）遍历基准分析

## 目的

测自递归 enum（树、链表、图）的遍历性能。这是 LS 内建的递归数据结构惯用法
（`enum Tree { Leaf; Node(Tree, Tree) }`，payload 自动 box）。对标 C++/Rust/Python。

二叉树 depth=16（131071 节点），sum 所有叶子。

## 结果（depth=16，每次完整 sum 的耗时）

| 语言 | 每次 sum | vs Rust |
|------|---------|---------|
| 🥇 Rust（`&Tree`） | **288 us** | 1.0× |
| C++（`const Tree*`） | 485 us | 1.7× |
| Python（tuple，引用） | 15,694 us | 54× |
| 🔴 **LS（`Tree` 按值）** | **130,899 us** | **454×** |

checksum 全部一致（6442418176）。

> ⚠️ **LS 甚至比 Python 还慢 8.3×**（131ms vs 15.7ms）—— 这是目前所有 benchmark 里
> 差距最大的一个，也是唯一 LS 输给 Python 的纯计算场景。

## 根因：enum 不支持借用 → 遍历必然深拷贝整棵子树

```ls
fn sum_tree(Tree t) -> i64 {            // ← 只能按值传
    match t { Node(l, r) => sum_tree(l) + sum_tree(r) ... }
}
```

`&Tree` 被 checker 直接拒绝：
```
&Tree is not supported yet; only &string/&vec/&map/&struct are implemented
```

所以递归 enum 只能按值传。`Tree` 是 has_drop enum（Node payload 是 box 指针），按值传会
**深拷贝整个子树**。每次递归 `sum_tree(l)` 把左子树完整 clone 一遍：

- 实测超线性：节点 ×4（depth +2）时间 ×4.7
- ~1.2 µs/节点（纯加法遍历本应是 ns 级）

**move 无法绕过**：move 会销毁树（sum 完树就没了），不符合只读遍历语义。只读遍历的正解是
**借用**，而 enum 恰恰没有。C++/Rust 用指针/引用零拷贝 O(n)；LS 被迫 O(n·subtree) clone。

## 影响范围（不止 benchmark）

LS 支持借用的类型：✅ `vec` / `map` / `struct`；🔴 **递归 enum 无借用**。

实际波及：
- 任何自递归 enum 建的树/链表/AST/图，只读遍历都是性能灾难
- **stdlib**：`std.json` 的 `JsonValue`、`std.md` 的 `MdInline`、`std.html` 的 `HtmlNode`
  都是递归 enum → 任何递归 navigate/walk/stringify 都在 clone 子树（这也是 JSON
  stringify、md/html 递归渲染慢的一部分原因）

## 已知限制状态

**记录为已知限制（2026-06-05）**。修复方向是给 enum 加借用（`&Tree` / `&!Tree`），
实现说明书见 `docs/plan_enum_borrow.md`。**当前决定：暂不实现**（中等偏大工程，
排期待定）。修复后递归数据结构可从 454× 慢 → 追平 C++/Rust，并顺带加速整个 stdlib 的
递归访问。
