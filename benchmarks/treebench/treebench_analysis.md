# treebench — 递归数据结构（树/链表）遍历基准分析

## 目的

测自递归 enum（树、链表、图）的遍历性能。这是 LS 内建的递归数据结构惯用法
（`enum Tree { Leaf; Node(Tree, Tree) }`，payload 自动 box）。对标 C++/Rust/Python。

二叉树 depth=16（131071 节点），sum 所有叶子。

## 结果（depth=16，每次完整 sum 的耗时）

| 语言 | 每次 sum | vs Rust |
|------|---------|---------|
| 🥇 Rust（`&Tree`） | **288 us** | 1.0× |
| **LS（`&Tree` 借用，Phase 9）** | **374 us** | **1.3×** ✅ |
| C++（`const Tree*`） | 485 us | 1.7× |
| Python（tuple，引用） | 15,694 us | 54× |
| ~~LS（`Tree` 按值）~~ | ~~130,899 us~~ | ~~454×~~ |

checksum 全部一致（6442418176）。

> ✅ **Phase 9 修复后 LS 以 ~1.3× 追平 Rust**（374 μs vs 288 μs）。
> 从之前 454× 差距降至 1.3×，**提速 ~350×**。

## 修复历程（Phase 9，2026-06-05）

**根因（已修）**：enum 之前不支持借用 → 遍历必然深拷贝整棵子树。

`&Tree` 现在可用：
```ls
fn sum_tree(&Tree t) -> i64 {
    match t {
        Leaf(v)    => { return v }
        Node(l, r) => { return sum_tree(l) + sum_tree(r) }
    }
}
```

**零拷贝实现**：
- `&Tree` 参数用 pointer ABI（不拷贝进函数）
- match 借用主体直接 GEP 原始指针，不做 alloca+store 拷贝
- box payload binder（`l`, `r`）直接存 box_ptr 作为 sym->value，auto-borrow 传调用时直传指针
- 递归调用零拷贝传递

## 影响范围

LS 现在支持借用的类型：✅ `vec` / `map` / `struct` / **`enum`（Phase 9）**。

Phase B（待做）：`&JsonValue` 等 owned payload（string/vec/struct）的借用遍历 → 进一步
加速 std.json stringify、std.md render、std.html render 等 stdlib 递归操作。
