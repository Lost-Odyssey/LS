# LS 已知限制与未来改进方向

记录当前实现中已知的限制、权衡取舍，以及将来可以改进的方向。  
每条记录注明 **原因**（为什么现在这样做）和 **改进路径**（将来怎么改）。

---

## L-001 · vec.sort_by 内联插入排序（O(n²)）

**现状**  
`vec.sort_by(|a, b| a - b)` 使用编译器直接 emit 的插入排序循环（O(n²)），而非 `qsort`。

**原因**  
`qsort` 的比较函数签名固定为 `int (*)(const void*, const void*)`，无法携带闭包的 `env_ptr`（Block 是 `{fn_ptr, env_ptr}` 16 字节胖指针）。线程局部全局中转（trampoline）虽然可行，但不可重入（嵌套 `sort_by` 会破坏全局状态）。插入排序可以直接在循环体内调用 Block，env_ptr 自然传递。

**限制**  
- 数组较大时（n > 几百）性能明显劣于 `qsort` 的 O(n log n)
- 无捕获的 `sort_by(plain_fn)` 仍走 `qsort` + 函数指针（不受影响）

**改进路径**  
1. **短期**：emit 归并排序 IR（O(n log n)，需要额外 O(n) 临时 buffer）
2. **长期**：等用户自定义泛型落地后，用纯 LS 实现通用排序函数，替换 codegen 内联版本

---

## L-002 · 不允许同名 trait 方法（无方法重载/无歧义消解）

**现状**  
两个 trait 定义了同名方法（即使签名完全相同），struct 同时实现两者时编译报错：  
`conflicting method 'greet': already defined for struct 'Person'`

**原因**  
LS 没有方法重载（overloading），也没有 trait 限定调用语法（如 Rust 的 `Greet::greet(&p)`）。  
如果允许同名共存，`obj.method()` 无法确定调用哪个 trait 的实现——返回类型可能不同，语义完全取决于注册顺序，是静默 bug。

当前策略：**严格拒绝**。`register_method()` 在发现 impl_registry 中已有同名方法时立即报错，不比较签名。

**限制**  
- 两个 trait 恰好定义了完全相同签名的方法时，仍会被拒绝（但这是极罕见的 corner case，且可通过在 trait 中使用不同名字规避）
- 未来如果引入 trait 限定调用语法，可以放宽此限制

**改进路径**  
1. **中期**：增加 `Trait::method(obj, args)` 调用语法，parser + checker + codegen 三端支持
2. **之后**：将 `register_method` 的限制放宽为"允许同名但必须在调用点显式消歧（`obj.method()` 歧义时报错，`Trait::method(&obj)` 允许）"

---

<!-- 后续新增限制条目请沿用 L-NNN · 标题 格式 -->
