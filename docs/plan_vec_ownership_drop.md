# 设计：纯 LS `Vec` 元素值语义——所有权 / clone / drop 正确性

> **进度（2026-06-07）**：§008 ✅ 修复 / §009 ✅ 验证已通过 / §007 待样本迁移。
> 基础设施落地见下方各节「实现结果」。验收 `test_vec_owndrop`（JIT+AOT+memcheck 0/0/0），
> 样本 `tests/samples/vec_owndrop_test.ls`；ctest 160/160。
>
> 状态：起草 2026-06-07（分支 `feat/rawvec`）。**正确性关键文档**（memcheck 泄漏）。
> 来源：[plan_vec_replacement.md](plan_vec_replacement.md) §6.1 的 VR-LIM-007 / 008 / 009。
> 这三条同根：**用户容器（has_drop struct）的"按值读出 / rvalue 入容器"在编译器的
> clone-on-read、临时 drop、move-into-container ABI 上未被完整覆盖**——内建 `vec` 由
> 编译器专属 codegen 兜住，而 `Vec(T)` 经普通方法调用走，缺了对应的 owner/temp 兜底。

---

## 0. 背景：内建 vec vs 纯 LS Vec 的所有权机器差异

内建 `vec[i]`（读）/ `vec.push(x)`（写）由编译器**专属 codegen**实现，clone/drop/move 都在
那段代码里显式处理（`AST_INDEX` 的 vec 分支会对 has_drop 元素 `emit_clone_value` 并
`cg_push_temp_drop`；`push` 的 rvalue 实参经 owned-param ABI move-in）。

纯 LS `Vec(T)`：
- `v[i]` → `Vec.__index(&self,i) -> T`（`std/vec.ls:143`），体内 `get` 做 `T tmp = self.data[i]`
  （**clone-on-read**：has_drop 的 `T` 经 `emit_clone_value` 深拷），`return tmp` move 出。
- `v.push(x)` → `Vec.push(&!self, T x)`（`std/vec.ls:65`），`T x` 经 owned-param ABI 入参，
  `self.data[self.len] = x` 裸存。

差异点：内建走"编译器知道这是容器操作"的专属兜底；`Vec` 走"普通方法调用 + 普通赋值"，
**调用结果 / 实参的临时所有权要由通用 call/return/assign 机器兜**，目前有三个洞。

---

## VR-LIM-008 —— `Vec(struct 含 string)[i]` 读出 clone 泄漏 ★最高优先

### 现象
`vec_struct_clone_test` 迁为 `Vec(Person)`（`Person{string name}`）后，`older[0].name == "amy"`
一类 **「索引读出 struct 再取字段」** 在 memcheck 下泄漏：`Vec.__index → get` 返回的
`Person` clone（其 `name` string 已深拷）在表达式用完后**没有被 drop**。

### 根因
`older[0]` 经 `Vec.__index` 返回一个 **has_drop struct rvalue（Person）**。当它作为
`older[0].name` 的中间 struct 被「读穿透」取 `.name` 时：
- 内建 vec 路径有专属处理（`AST_INDEX` vec 分支 + `cg_push_temp_drop`，且 BF 中
  "struct field readthrough" 已对中间 struct 借址 GEP 而非深拷，CLAUDE.md 记录）。
- 但 `Vec.__index` 是**普通方法调用**返回 struct：该 rvalue 落到一个临时槽，`.name` 从中
  GEP 读出（string 又 clone 一次或借出），而**整个 Person 临时本身的 `__drop` 未注册** →
  Person 的 `name` buffer 泄漏。

证据：`cg_push_temp_drop` 在 call-arg / 字面量 / match subject 等处都有注册
（`codegen.c:10295,12010,12144,12566,12690,16827`），但**「方法调用返回 has_drop struct 的
rvalue，被 field-access 立即消费」**这条路径没有注册临时 drop。

### 设计
把"方法调用/索引返回的 has_drop 值 rvalue"统一纳入临时 drop 注册——尤其当它**不被绑定到
命名变量、而是被 field-access / 进一步索引立即消费**时：
1. **读穿透优先借址**：`callresult.field` 的中间 `callresult`（has_drop struct rvalue）应
   spill 到临时槽、注册 `cg_push_temp_drop`，`.field` 从该槽 GEP；语句末 flush 时
   `emit_struct_drop` 释放整个临时（含 `name`）。这正是 §VR-LIM-008 缺的那一步。
2. 复用既有 `codegen_addr_of` 的 `AST_CALL` 分支（本会话刚加，`codegen.c:10632` 一带：
   rvalue 接收者 spill+temp-drop）——把它推广到「has_drop 返回值被 field/index 消费」场景，
   即 `AST_FIELD.object` / `AST_INDEX.object` 为返回 has_drop struct 的 call 时，走同一
   spill+temp-drop。

### 实现步骤
1. 复现：`tests/samples/vec_struct_index_field_test.ls`（`Vec(Person)`，`v[i].name` 断言），
   `run --memcheck` 当前应泄漏。
2. 在 field-access / index codegen 的 object 求值处：若 object 是返回 has_drop struct 的
   call（含 `__index`），spill 到 alloca + `cg_push_temp_drop`，field/index 从 alloca GEP。
3. 终端绑定（`Person p = v[i]`）仍走既有 var-decl move（不退化为临时泄漏）。
4. memcheck 0/0/0；覆盖 `v[i].field`、`v[i].inner.field`（嵌套）、`f(v[i])`（作实参）。

### 工作量/风险
中-大（触及通用 field/index 的 object 求值临时管理）。风险中：与既有"struct field
readthrough 借址"修复（CLAUDE.md）要协同，避免对**命名 struct 变量字段**误加临时 drop。
判定关键：object 是否为**拥有的 rvalue**（call/index 返回）vs **借用/命名左值**。

### 实现结果（✅ 2026-06-07）
两处 codegen 修复，根因比设计预想更细：
1. **嵌套 struct 字段 drop 惰性兜底**（`emit_auto_drop_fn`，`codegen.c:3816`）：`Vec(Person)`
   方法（`__drop_at` → `Person.__drop`）经惰性单态化发射，可能**早于**主文件 Pass 2.5
   生成 `Inner.__drop` → 原代码查不到成员 `__drop` 即静默跳过 → `Inner.tag` 泄漏。修复：
   成员 `__drop` 缺失时按需 `emit_auto_drop_fn(field_type)` 惰性生成（同 `emit_struct_drop_cond/_separate`
   既有模式，`emit_auto_drop_fn` 自存/恢复 builder，递归安全；struct 不可值循环故无死循环）。
2. **链式读穿透的中间 owned-clone 临时 drop**（field-access spill 路径，`codegen.c:12577`）：
   `v[i].inner.tag` 的中间 `v[i].inner` 经 `emit_struct_clone_val` 返回**owned Inner 克隆**，
   但 spill-temp-drop 注册条件原仅含 `AST_INDEX`/`AST_CALL`，漏了 `AST_FIELD` → Inner 克隆
   的 `tag` 泄漏。修复：条件加 `AST_FIELD`（进入此 else 分支即说明 obj 无稳定左值 →
   spill 的必是 owned rvalue，注册 temp-drop 安全；命名变量链 `p.inner.tag` 经
   `codegen_lvalue_ptr` 借址不进此分支，不受影响）。
覆盖 `v[i].field` / `v[i].inner.field` / `f(v[i])` / 终端绑定 `Person p=v[i]`，均 memcheck clean。

---

## VR-LIM-009 —— `Vec(string).push(rvalue string)` 泄漏

### 现象
`vec_string_test` 迁移后，`v.push("hello".upper())` 再经 `pop` / `v[0]=...` 路径，memcheck 报
**最初 `string.upper()` 的分配泄漏**；先 `string tmp = ...; v.push(tmp)` 的样本干净。

### 根因
`"hello".upper()` 是 **owned rvalue string**。`Vec.push(&!self, T x)` 的 `T=string` 经
owned-param/move-into-container ABI（CLAUDE.md「string push 追平 vec」）入参——该 ABI 期望
**调用方把 owned rvalue 的所有权干净转移给被调方**。但 rvalue string 实参在调用点同时被
登记进 `temp_string_slots`（语句末 `cg_flush_temps` 会 free 它）——若 move-into-container 未
把该临时**标记为已转移（moved）**，则：buffer 被 push 进 Vec（Vec.__drop 时 free）**且**
语句末临时 flush 又 free 一次 → 实为 double-free 边界；或反之 push ABI clone 了一份而原
rvalue 临时没 free → 泄漏（现象是泄漏，说明是后者：push 路径对 user 容器没接管 rvalue
临时的所有权标记）。

对比：内建 `vec.push` 的 rvalue string 实参在编译器专属 codegen 里有"标记 moved / 不重复
free"的处理；`Vec.push` 是普通方法调用，rvalue string 实参的 move-in 标记缺失。

### 设计
调用**用户方法**且某 `string` 实参是 **owned rvalue**（call/字面量/`__move`，非命名借用
变量）时，按 owned-param ABI 传入后，**把该 rvalue 的语句级临时标记为 moved**（从
`temp_string_slots` 摘除 / cap 置 -1），使语句末 flush 不再 free——所有权已转移给容器，由
`Vec.__drop` 唯一释放。这与 §11.x 已有的「call-arg string ownership：owned vs borrowed」
处理（`codegen.c:12017+`）同源，需确认它对**用户方法**（非内建）同样生效。

### 实现步骤
1. 复现：`tests/samples/vec_push_rvalue_str_test.ls`：`v.push("x".upper())` + `pop`/覆盖，
   `run --memcheck` 当前泄漏。
2. 审 `codegen.c:12017+` 的 string-arg ownership 分支：确认对用户方法调用（`Vec.push`）也
   走「owned rvalue → 标记 moved，不在调用方 flush」；若它只对内建/特定路径生效，扩到
   用户方法实参。
3. 区分 borrowed（命名变量，cap=-2，调用方保留所有权）vs owned rvalue（cap 保留，标记
   moved）——勿误把命名变量标记 moved。
4. memcheck 0/0/0；覆盖 `push(rvalue)` / `insert(i, rvalue)` / `set(i, rvalue)`。

### 工作量/风险
中。风险中：owned/borrowed 判定错误会导致 double-free（owned 误判 borrowed→clone+原 free）
或 use-after-free（borrowed 误判 owned→标记 moved 后调用方再用）。判定须严格按实参 AST
形态（IDENT=借用 / call·字面量·__move=owned）。

### 实现结果（✅ 验证 2026-06-07，无需改动）
`feat/rawvec` 既有的 owned-param / move-into-container ABI（CLAUDE.md「string push 追平 vec」+
call-arg string ownership owned-vs-borrowed，`codegen.c:12017+`）**已对用户方法生效**：
`v.push(rvalue)` / `insert(i, rvalue)` / `set(i, rvalue)` / `v[i]=rvalue`（含 `"x".upper()`、
f-string、拼接、rvalue struct 含 string）经 `pop`/覆盖后 memcheck 0/0/0；命名变量实参仍 clone-in
（调用方保留所有权）。`test_vec_owndrop` 覆盖。设计预想的「漏标 moved」未实际发生。

---

## VR-LIM-007 —— 自定义 `__drop` 副作用计数与 `Vec` 不等价

### 现象
`vec_struct_test` 迁移后，`Vec` 的 clone / 临时 / `Option(T)` 返回值的 drop 路径**额外触发**
`Item.__drop`，旧的 `drop_count`（精确计数 `__drop` 调用次数）断言失败。

### 根因
纯 LS `Vec` 比内建 vec **多了若干 clone/临时点**：`get` clone-on-read、`pop`/`first`/`last`
经 `Option(T)` 包装与解包、方法返回值临时等，每个 clone 出来的 `T` 最终都要 drop → `__drop`
被调次数比内建 vec 多。这**不是泄漏**（memcheck 干净），而是**可观察的 drop 次数语义差异**。

### 设计（决策为主）
clone-on-read 是 `Vec` 值语义的固有代价，且只要 §008/§009 修好就**收支平衡（clone N 次 →
drop N 次，无泄漏）**。因此：
- **不**为了"对齐内建计数"去削减 `Vec` 的必要 clone（那会破坏值语义/正确性）。
- 受影响样本（用 `__drop` 副作用精确计数验证所有权的）**改为 memcheck/行为验收**：断言
  "无泄漏 + 最终可见状态正确"，而非"`__drop` 恰好被调 K 次"。精确计数本就是脆弱的白盒断言。
- 若确需"读元素不 clone"，应走**借用读**（`&Vec` + 元素借用，enum-borrow Phase B 风），那是
  独立优化（见"后续"），不在本轮。

### 实现步骤
无编译器改动（待 §008/§009 修复后，此类样本应 memcheck clean）。迁移侧把精确 `drop_count`
断言改为 memcheck 验收。

### 工作量/风险
小（决策 + 样本断言改写）。前置依赖：§008/§009 必须先修（否则不是"多 drop"而是"漏 drop"）。

---

## 三条的依赖与推进顺序
1. **先 §008**（最高优先，读出 struct 泄漏，影响面最广：任何 `Vec(has_drop struct)[i]`）。
2. **再 §009**（rvalue 入容器，影响 `Vec(string)`/`Vec(has_drop)` 的 push/insert/set）。
3. **后 §007**（待 008/009 clean 后，纯做样本断言收尾 + 决策登记）。

每步：最小复现 → 定位通用 call/return/assign 临时管理 → 修 → 全量 memcheck 0/0/0 + 抽样 AOT。

## 总验收
- 新增 `vec_struct_index_field_test` / `vec_push_rvalue_str_test`（JIT+AOT+memcheck 0/0/0）。
- §6.1 中因这三条暂留内建 vec 的样本（`vec_struct_clone_test` / `vec_string_test` /
  `vec_struct_test` / `enum_*` 等）回迁为 `Vec` 后 memcheck clean。

## 后续（非本轮）
- `&Vec` 借用读元素（零 clone）：消除 `get` clone-on-read 开销，需"借用返回 / 元素借用绑定"
  （enum-borrow Phase B 已有先例）。这会从根上让 §007 的计数也趋近内建。
