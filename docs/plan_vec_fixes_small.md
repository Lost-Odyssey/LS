# 设计：Vec 迁移——小改动合集

> 状态：起草 2026-06-07（分支 `feat/rawvec`）。
> 来源：[plan_vec_replacement.md](plan_vec_replacement.md) §6.1 已知限制中**改动较小、局部、低风险**的条目。
> 较大条目各有独立文档：VR-LIM-001 → [plan_userdef_for_in.md](plan_userdef_for_in.md)；
> VR-LIM-007/008/009 → [plan_vec_ownership_drop.md](plan_vec_ownership_drop.md)；
> VR-LIM-010 → [plan_fn_value_as_block.md](plan_fn_value_as_block.md)；
> VR-LIM-011 → [plan_container_in_enum_payload.md](plan_container_in_enum_payload.md)。
>
> 本文每条：根因（带 file:line）→ 设计 → 实现步骤 → 工作量/风险 → 验收。

---

## VR-LIM-002 —— 全局 `Vec` 变量不自动 `__drop`

### 根因
全局变量在 `main` 返回前由 `__ls_global_cleanup` 清理（`codegen.c:21592+`）。该函数的清理宏
（`codegen.c` ~21642「Helper macro: emit cleanup for one global var decl」）只识别
`TYPE_STRING`（free if cap>0）与 `TYPE_VECTOR`（内建 vec 逐元素 drop + free buffer），**没有
`TYPE_STRUCT(has_drop)` 分支**。纯 LS `Vec(T)` 是 has_drop struct → 全局 `Vec` 退出时不调用
`Vec.__drop` → raw buffer 泄漏。局部 `Vec` 由 `emit_scope_cleanup`（`codegen.c:2926`）的
`TYPE_STRUCT && has_drop` 分支正常 drop，故只全局漏。

判定全局是否需清理的宏（`codegen.c` ~21600 `HAS_GLOBAL_CLEANUP` 判定）也只认
`TYPE_STRING || TYPE_VECTOR` → 即便加了 struct 分支，若判定宏不认 struct，cleanup 函数都不会
被生成/调用。两处都要改。

### 设计
让全局 cleanup 与"需要 cleanup"判定都纳入 `TYPE_STRUCT(has_drop)`、`TYPE_ENUM(has_drop)`、
`TYPE_MAP`（与局部 `emit_scope_cleanup` 的类型集合对齐），对 struct 调用其 `drop_fn`（与
局部 struct 清理同一入口 `emit_struct_drop`/`emit_struct_drop_cond`）。

### 实现步骤
1. `codegen.c` 全局 cleanup 判定宏：`TYPE_STRING || TYPE_VECTOR` → 增加
   `TYPE_STRUCT(has_drop) || TYPE_ENUM(has_drop) || TYPE_MAP`。
2. 清理宏体：新增 struct 分支 `emit_struct_drop(ctx, gv_ptr, type)`（drop_fn 此时已在
   Pass 2.5 生成；若为 NULL 按需 `emit_auto_drop_fn`，参照模块函数内 struct 局部的惰性生成
   处理，见 CLAUDE.md「模块函数内 struct 局部 drop 泄漏」修复）。enum/map 同理接既有 drop。
3. 全局变量的 module 前缀名沿用宏现有 `gname`（P1-3 已处理）。

### 工作量/风险
小（~30 行，复用既有 drop 入口）。风险低：只新增分支，不改既有 string/vec 路径。需注意
全局 struct 的 `moved_flag` 一般无（全局不参与 move），直接无条件 drop 即可。

### 验收
`tests/samples/vec_global_drop_test.ls`：全局 `Vec(string) g = {}` + 在函数里 push，
`run --memcheck` SUMMARY 0/0/0。覆盖全局 `Vec(int)` / `Vec(string)` / 含 struct 元素。

---

## VR-LIM-003 —— 越界容错 / 默认值 API 差异

### 根因
内建 `vec` 的 `get(99)`/`first()`/`last()`/`remove(99)`/`swap(0,99)` 越界时**打印警告并返回
默认值/静默容错**（codegen 内建实现的边界保护）。纯 LS `Vec`：`get(i)` 是 unchecked 裸读
（`std/vec.ls:128` 注释），`first`/`last`/`pop` 返回 `Option(T)`。语义不同。

### 设计（决策为主，改动极小）
**不保留内建的"越界返回默认值"容错**（那是隐藏 bug 的反模式）。最终对齐为：
- `get(i)` / `v[i]` / `get!(i)`：unchecked，越界即 UB（`get!` 名字已彰显）。
- 安全访问走 `first()`/`last()`/`pop()` 的 `Option(T)`。
- 未来（CLAUDE.md §6 待实现）`get(i)` 改返回 `Option(T)` 为安全默认，`get!` 保留 unchecked
  ——**本计划不动**，仅在此登记方向。

迁移侧：依赖旧容错的样本按 `Vec` 语义重写断言（已在 §6.1 绕行说明）。

### 实现步骤
无编译器改动。仅文档登记 + 迁移样本断言重写。可选：给 `Vec` 增加纯 LS 的
`get_or(i, default) -> T`（便利方法，纯 LS，零编译器改动）替代旧的"容错默认值"。

### 工作量/风险
极小（决策 + 可选便利方法）。无风险。

---

## VR-LIM-004 —— `resize` 必须显式 fill

### 根因
内建 `vec.resize(n)` 用类型默认值（0 / `""` / 空）填充新槽；纯 LS `Vec.resize(n, fill)`
（`std/vec.ls:212`）要求显式 `fill`（因为纯 LS 无法凭空造任意 `T` 的默认值）。

### 设计
LS 当前**无默认参数 / 无重载**，无法零参 `resize(n)`。两条路：
- **(A 推荐)** 保持显式 `resize(n, fill)`，迁移样本改为 `resize(n, 0)` / `resize(n, f"")`
  （已绕行）。语义更清晰（"用什么填"显式化）。
- **(B)** 给数值/字符串等"有零值"的 `T` 加 `resize_default(&!self, int n) where T: Default`
  ——需引入 `Default` trait（较大），**本计划不做**，仅登记。

### 实现步骤
A：无编译器改动。文档登记 + 迁移断言。

### 工作量/风险
极小。无风险。

---

## VR-LIM-005 —— 闭包写捕获外层 `Vec`（by-ref 缺失）

### 根因
内建 `vec` 闭包捕获是 **by-ref**（`capture_type_is_by_ref_cg`，`codegen.c:381`），闭包内
`acc.push(x)` 改的是外层 vec。纯 LS `Vec` 是 has_drop struct → **by-move** 捕获（已由 D1
决策确定），闭包拿到的是被移动的副本，外层失效。`nums.each(|x| { acc.push(x) })` 这类模式
对 `Vec` 不成立。

### 设计（迁移规约为主）
D1 已定 by-move，**不**给 `Vec` 恢复 by-ref 特例（违背"Vec 是普通 struct"）。改写模式：
- **闭包内累加 → 改为可写借用参数**：把累加容器作为 `&!Vec` 显式传给做累加的函数，而非
  闭包捕获。或用 `reduce`/`map`/`filter` 等**返回新 Vec** 的函数式 API 替代"闭包内 push"。
- 真有"闭包持有可变容器"需求的，归入桶 D 专项（plan_vec_replacement §4 D1 路线 B 改写）。

> 编译器层不改。若将来确需"闭包按可变借用捕获容器"，那是独立的大特性（闭包捕获 `&!T`），
> 另案，不在本轮。

### 实现步骤
无编译器改动。迁移样本按上述模式重写。

### 工作量/风险
小（迁移规约）。风险：改写后语义需逐个 memcheck 确认无悬垂。

---

## VR-LIM-006 —— 闭包直接返回 string 形参的脆弱边界

### 现象
长链 `reduce(string)(..., |acc, s| { if acc.length == 0 { return s } ... })` 曾出现**首元素
错值**（直接 `return s`，s 是 string 形参）。绕行：改为返回新串表达式 `f"" + s`。

### 根因（待确证的假设）
闭包体 `return s`（s 为 string 参数）时的所有权转移路径存疑：string 参数按 `&string`
by-value（cap=0 借用标记）进入闭包；直接 `return` 它，调用方期望获得**拥有**的 string，但
返回的是 cap=0 借用值 → 后续被当借用不 free / 或被覆盖 → 错值。即「借用 string 直接作为
拥有值返回」的转移缺失（类似 CLAUDE.md L-012 边界③ 在闭包语境的变体）。

### 设计
在闭包/函数 `return <string 参数 IDENT>` 路径：若被返回的 IDENT 是借用 string（cap=0 /
is_borrow），返回前 **clone 成拥有副本**（与 AST_RETURN 既有「返回全局 string 名 clone」
同源逻辑，`codegen.c:15281`）。owned 局部 string 仍走 move-transfer 不 clone。

### 实现步骤
1. 先写最小复现 `tests/samples/closure_return_str_param_test.ls`（不带绕行），确认 JIT 错值。
2. 定位闭包体 AST_RETURN codegen（复用函数返回路径）；对「返回值是借用 string 形参」插入
   `emit_string_clone_val`。
3. 对照 owned 局部 string 不受影响（不退化为多余 clone）。

### 工作量/风险
中（需先 root-cause 确认假设；改动局部但要分清 owned vs borrowed 返回）。**可能升级**：
若根因不止于 string 借用返回，则拆为独立文档。风险中：误 clone owned 串会引入多余分配
（memcheck 仍 clean 但性能微损）——用 owned/borrowed 判定区分。

---

## VR-LIM-012 —— struct 字段默认值里的 `[..]` 不走 `Vec.__from_list`

### 根因
`struct S { Vec(int) preset = [1,2,3] }` 中，字段默认值 `[1,2,3]` 被 checker 判为
`array(int,3)`（字面量默认类型推断走数组路径），而非经 `Vec.__from_list` 构造的 `Vec(int)`；
`Vec(f64) data = []` 的空 `[]` 也无法从字段声明类型反推元素类型。字段默认值的检查/构造
（checker `checker.c:1005,7856` 存 `default_expr`；codegen 在 struct 字面量补默认值
`codegen.c:6898` 读 `default_expr`）走的是通用字面量路径，未接入 `__from_list` 协议
（该协议目前只在 **var-decl 初始化** `Type v = [..]` 处生效）。

### 设计
让 struct 字段默认值的 `[..]`/`{}` 复用 var-decl 已有的「LHS 类型已知 → `__from_list`/
零初始化」推断：字段声明类型 `Vec(T)` 即 LHS 类型，默认值 `[a,b,c]` 应按 `Vec(T) tmp = {};
tmp.__from_list(a); ...` 构造，`[]`/`{}` 按零初始化（元素类型取自字段 `Vec(T)` 的 `T`）。

### 实现步骤
1. checker：字段默认值检查时，以字段声明类型为 `expected_type`（复用 `c->expected_type`
   机制，checker.c:82），使 `[..]` 对用户容器走 `__from_list` 协议校验、`[]` 从 `Vec(T)`
   取元素类型，而非默认 array 推断。
2. codegen：struct 字面量补默认字段值时（`codegen.c:6898` 一带），对用户容器字段的 `[..]`
   默认值发射 `__from_list` 序列（与 var-decl `Type v=[..]` 同一 emit 路径，抽公共函数复用）。
3. 同步覆盖：嵌套（`Vec(Vec(int)) = [[1],[2]]`）与空（`Vec(f64) = []`）。

### 工作量/风险
中（checker expected_type 传递 + codegen 复用 __from_list 路径）。风险中：struct 字面量
默认值 emit 路径与 var-decl 路径需保持 drop/clone 语义一致——抽公共 emit 函数避免分叉，
逐个 memcheck。

> 若实现中发现 struct 默认值 emit 与 var-decl 差异过大、需大改 struct 字面量 codegen，
> 则升级为独立文档。

---

## 汇总：工作量/优先级
| 编号 | 主题 | 编译器改动 | 工作量 | 优先级 |
|------|------|-----------|--------|--------|
| VR-LIM-002 | 全局 Vec __drop | 是（全局 cleanup +struct 分支） | 小 | 高（正确性/泄漏） |
| VR-LIM-006 | 闭包返回借用 string | 是（return clone） | 中 | 中（正确性） |
| VR-LIM-012 | struct 字段默认 `[..]` | 是（checker+codegen） | 中 | 中 |
| VR-LIM-003 | 越界容错/get 语义 | 否（决策+可选便利方法） | 极小 | 低 |
| VR-LIM-004 | resize 显式 fill | 否（决策） | 极小 | 低 |
| VR-LIM-005 | 闭包写捕获 Vec | 否（迁移规约） | 小 | 低 |
