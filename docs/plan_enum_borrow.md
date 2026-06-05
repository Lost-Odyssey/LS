# 实现说明书：enum 借用（`&Tree` / `&!Tree`）

> **状态：起草，暂不实现（2026-06-05）**。优先级待定。
> 动机见 `benchmarks/treebench/treebench_analysis.md`：递归 enum 只读遍历因无借用
> 必须深拷贝子树，比 C++/Rust 慢 ~450×，甚至慢于 Python。

## 0. 目标

让 `&Enum`（只读借用）和 `&!Enum`（可写借用）像 `&struct` 一样工作，使递归 enum
（树/链表/AST/图）能**零拷贝只读遍历**，并加速整个 stdlib 的递归访问
（`JsonValue` / `MdInline` / `HtmlNode`）。

目标用法：
```ls
fn sum_tree(&Tree t) -> i64 {
    match t {
        Leaf(v)    => { return v }          // v: 值绑定（payload 是 i64，copy）
        Node(l, r) => { return sum_tree(&l) + sum_tree(&r) }  // l,r: 借用绑定
    }
}
```

## 1. 现状（借用机制已就绪，enum 被排除在外）

借用对 string/vec/map/struct 已完整实现。ABI 要点（CLAUDE.md §7）：
- `&string` 特殊（by-value，cap=0 标记）；其余 `&T`/`&!T` **全部 pointer**
- 只读借用支持 auto-borrow；可写借用必须显式 `f(&!x)`
- 借用仅用于函数参数位置

enum 唯一缺的是：**被显式排除在白名单外**，且 match 对借用主体的解构逻辑未处理。

## 2. 改动清单

### 2.1 checker —— 放开白名单（`src/checker.c:1158`）
```c
bool ok_kind = (pointee->kind == TYPE_STRING ||
                pointee->kind == TYPE_VECTOR ||
                pointee->kind == TYPE_MAP ||
                pointee->kind == TYPE_STRUCT ||
                pointee->kind == TYPE_ENUM);   // ← 新增
```
更新错误信息文案。`&Enum` 走 pointer ABI（同 struct borrow），非 string 特例。

### 2.2 checker —— `&!Enum` 资格检查
仿 struct：`&!enum` 要求 IDENT 是 owned、非 moved、非借用的 enum 变量。
（搜 `&! requires` / `borrow_kind` 附近的 struct 分支，加 enum 同款。）

### 2.3 checker —— match 借用主体的 payload 绑定类型
**核心改动**。当 match 主体类型是 `&Enum`（借用）时，变体解构的 payload 绑定：
- 标量 payload（i64/f64/bool/...）→ 值绑定（copy out，不影响原值）
- **owned payload（string/vec/map/struct/嵌套 enum/box 子树）→ 借用绑定**
  （绑定类型 = `&payload_type`，指向 enum 内部 payload，**不 clone**）

当前 match 对 owned 主体会 clone payload binding（这正是 treebench 慢的根源）。
借用主体路径必须改为发借用（指针），不 clone、不 drop。

自递归 box payload（`Node(Tree left, ...)`）：binding `l` 的类型是 `&Tree`，
值 = 解 box 后的指针（payload 存的就是 `Tree*`，直接作为借用指针）。

### 2.4 codegen —— enum 参数 by-pointer
`type_to_llvm` 对 `TYPE_REFERENCE(enum)` / `TYPE_MUT_REFERENCE(enum)` 发 `ptr`
（同 struct borrow）。函数体内 self/param 是 enum 借用指针，不 alloca、不 copy。

### 2.5 codegen —— match 借用主体解构
match 主体是借用指针时：
- disc 读：`GEP enum_ptr, 0` 后 load tag（直接从借用指针读，不先 copy 整个 enum）
- payload binding：
  - 标量 → load 出值
  - owned → 取 payload 字段**地址**作为借用指针（不 deep-clone，不注册 drop）
  - box 子树 → load 出 box 指针（即子树借用指针）→ 可直接传给 `sum_tree(&l)`
- **借用主体的 match 不 drop 主体**（借用不拥有）——区别于现有"拥有 rvalue 临时 enum
  主体析构"路径（L-012）

### 2.6 codegen —— auto-borrow & 调用
`sum_tree(&l)`：l 是借用绑定（已是指针）→ 直接传指针。
只读 auto-borrow：`sum_tree(tr)` 若签名是 `&Tree` 且 tr 是 owned 变量 → 自动取址传入
（同 struct auto-borrow）。

## 3. 风险与边界

- **drop 交互**：借用绝不能 drop 主体或 payload。match 借用路径要完全跳过现有
  enum drop 注册。重点回归 has_drop enum（JsonValue 等）经借用 match 后**原值仍完整、
  作用域退出正常 drop 一次**（memcheck）。
- **嵌套**：`&Tree` 的 match 解构出 `&Tree` 子借用，递归借用传递不能升级为拥有。
- **可写借用 `&!Enum`**：可写 payload（如改 Node 的子节点）涉及 move 进出，
  先做只读 `&Enum`（覆盖 treebench/遍历场景），`&!Enum` 可作为 Phase 2。
- **模板 enum**：`Option(T)` / `Result(T,E)` 也是 enum，借用要兼容（`&Option(string)`）。
- **生命期**：和现有借用一样，编译器不做生命期检查，借用不能 outlive 主体（用户保证）。
- **借用绑定的"返回禁令"**：从 `&Enum` 的 match 取出的 payload 借用绑定**不能作为返回值返回**，
  否则一旦主体析构借用就悬垂。等价于现有 `&struct` 字段借用的"借用不能返回"限制，
  checker 需复用同套规则（搜 `borrow_kind` 相关的返回路径检查）。
- **`return` 在 match 借用主体的臂中**：现有"拥有 rvalue 临时 enum 主体"在臂 `return f(binding)`
  路径上有专门的 clone+drop 处理（L-012 / `test_cmatrix_t08`）。借用主体走借用路径——
  **完全跳过那条 clone+drop**（借用主体无 rvalue 临时、无堆所有权）。务必在 codegen
  里把"借用主体"判定**前置**于既有 L-012 路径，避免误触发。
- **借用主体 + match `_` 通配/未用绑定**：临时主体的 L-012 修复曾涉及裸 `_` 臂的析构；
  借用版本同样跳过析构（借用不拥有 → 没什么可 drop）。
- **借用绑定不可再 `&!` 升级**：从 `&Enum` match 出的借用是只读，禁止在臂内 `&!l`
  之类的升级（等价 `&struct` 的只读→可写禁令）。
- **auto-borrow 与 `is_lvalue`**：调用 `sum_tree(tr)` 自动取址要求 `tr` 是 lvalue 命名变量；
  调用 `sum_tree(build(...))` 传 rvalue enum 应**按值传**还是**拒绝**？建议：rvalue → 走原有
  按值路径（兼容现状），不强制借用。checker 在 auto-borrow 匹配时仅对 IDENT lvalue 取址。
- **借用主体作为 print/打印参数**：`print(t)` 现有 enum 打印路径按值；借用版本要么实现
  `print(&t)` 同等行为（解引用后走既有 codepath），要么 Phase A 暂不支持（match 自己写）。

## 4. 分阶段建议

| Phase | 内容 | 验收 |
|-------|------|------|
| A | 只读 `&Enum` + match 借用解构（标量+box payload） | treebench 追平 C++/Rust（~450×→1×） |
| B | owned payload（string/vec/struct）借用绑定 | `&JsonValue` 递归遍历零拷贝 + memcheck |
| C | `&!Enum` 可写借用 | 树原地修改 |

Phase A 即可解除 treebench 的 450× 差距（树/链表 payload 多是标量或 box 子树）。

## 4.5 目标 IR 形状（Phase A，sum_tree 例子）

按值版（当前）会发射类似（每次递归把整个 `%Tree` 拷进 alloca 再解构）：
```llvm
define i64 @sum_tree(%Tree %0) {
entry:
  %t = alloca %Tree, align 8       ; param copy
  store %Tree %0, ptr %t
  %disc.p = getelementptr ...      ; disc + payload load → clone box → recurse
  ...
}
```

借用版（Phase A 目标），main 入参直接是 ptr，无 alloca/copy，payload 借用 = 子树指针：
```llvm
define i64 @sum_tree(ptr %t) {
entry:
  %disc.p = getelementptr %Tree, ptr %t, i32 0, i32 0
  %disc = load i8, ptr %disc.p
  switch i8 %disc, label %case_node [ i8 0, label %case_leaf ]
case_leaf:
  %v.p = getelementptr ...
  %v = load i64, ptr %v.p
  ret i64 %v
case_node:
  %l.p = getelementptr %Tree, ptr %t, i32 0, i32 1, i32 0
  %l  = load ptr, ptr %l.p          ; box(Tree) 已是 ptr，直接传
  %lr = call i64 @sum_tree(ptr %l)
  %r.p = getelementptr %Tree, ptr %t, i32 0, i32 1, i32 8
  %r  = load ptr, ptr %r.p
  %rr = call i64 @sum_tree(ptr %r)
  %sum = add i64 %lr, %rr
  ret i64 %sum
}
```
零 alloca、零 clone、纯 ptr 追踪，应能直接被 LLVM SROA + 内联到接近 C++/Rust 水平。

## 5. 验收测试

- `treebench`：LS sum 从 131ms → 接近 Rust 0.29ms
- has_drop enum 借用 memcheck（JsonValue walk 不 clone、不泄漏、不双释放）
- 现有全部 enum/match/Option/Result/json/md/html 测试不回归
- 新增 `tests/test_enum_borrow.cmake`（JIT+AOT+memcheck）

## 6. 工作量评估

中等偏大。借用 ABI/auto-borrow 基础设施已就绪（struct 借用趟过），主要新工作在
**match 对借用主体的解构语义**（2.3 + 2.5）——这是 enum 特有、struct 没有的部分。
估计与当年"给 struct 加借用"相当。

## 7. 实施路径建议（落地顺序）

1. **checker 白名单 + auto-borrow**（2.1, 2.6）：先让 `&Tree` 类型签到，调用点能自动取址。
   无 match 解构改动时，函数体内借用还不可用——但编译通过、能调用既有按值实现。
2. **codegen 参数 by-ptr**（2.4）：`&Enum` 参数发 ptr，函数体内对 enum 借用做最简单的"解引用→
   按现状走"（先 deref 成值再 match，本阶段还会有一次大 copy，但循环已通）。
3. **match 借用主体路径**（2.3 + 2.5）：核心改动。新增 `match.subject_is_borrow` 分支，
   disc/payload 直接经 ptr GEP/load，跳过 clone+drop。treebench 应在此阶段拿到 200×+ 提速。
4. **L-012 边界**（3 中的 return 在借用臂）：仔细审 codegen 里 AST_RETURN 在 match 臂内的
   clone+drop 路径，确认借用主体走纯 ptr 版本。
5. **owned payload 借用绑定**（Phase B 入口）：string/vec/struct payload 改发借用绑定（取址），
   解锁 `&JsonValue` 等真实 stdlib 场景。需扩 checker 的"绑定→借用"规则。
6. `&!Enum`（Phase C）：留到 Phase A+B 稳定后。

## 8. 修订记录

| 日期 | 修订 |
|------|------|
| 2026-06-05 | 初稿（起草，暂不实施） |
| 2026-06-05 | 补充：返回禁令、L-012 边界、auto-borrow rvalue 决策、目标 IR 形状、实施路径 |
