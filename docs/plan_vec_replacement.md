# 计划：用纯 LS `Vec(T)` 全面替换内建 `vec`

> 状态：定稿 2026-06-07（分支 `feat/rawvec`，起点 ctest 158/158）。**决策已全部敲定**，
> 本文是**可直接派发给其他 agent 执行的施工蓝图**——每个 Phase 自包含、带精确文件清单、
> 逐步操作、验收命令。接手 agent 无需本对话上下文。
> 前置阅读：[plan_rawvec.md](plan_rawvec.md)（RawVec 试点已完成）、[ownership.md](ownership.md)
> （借用/move/RAII）、CLAUDE.md §7（所有权）、§8（闭包捕获）。

## 用户目标（三步 + 一个前置精简阶段）
1. **Phase 1 重命名**：`RawVec` → `Vec`（`std/rawvec.ls` → `std/vec.ls`，`import std.vec`）。
2. **Phase 1.5 API 精简（替换前先做）**：定稿 `Vec` 方法名 + 引入 `?`/`!` 后缀语言特性，
   使后续迁移直接对准最终 API（避免测试改两遍）。
3. **Phase 2 迁移**：所有依赖内建 `vec` 的测试 → `import std.vec` 后用 `Vec`。
4. **Phase 3 拆除**：删除编译器里内建 `vec` 的全部特殊实现。

## 已敲定的决策（不再讨论）
| 决策 | 选择 | 影响 |
|------|------|------|
| **D1 闭包捕获 Vec** | **by-move**（与 string/struct 一致） | 改写 ~8 个 closure 用例：闭包内改 vec 改为传 `&!Vec` 或闭包返回新 Vec |
| **D2 借用/move negative 测试** | **按 struct 借用规则重写**（不复刻内建 vec 借用规则） | 审计每条 negative「在保护什么」，保留有意义的、按 struct 借用语义重写表述 |
| **Q3 测试改名** | **`test_rawvec_*` → `test_vec_*`** | 同步更新 CMakeLists 的 `DEPENDS` 依赖链 |
| **API 后缀约定** | `?`=谓词、`!`=unsafe；可变性由 `&!self` 表达，**不引入** Julia 风 `sort!` | 见 §2、§3 |

---

## 0. 可行性结论

功能 API 层面 `Vec(T)` 已能替换内建 `vec`（push/pop/insert/remove/index/literal/
functional/drop/clone/borrow 全具备，见 plan_rawvec 验收）。语义层两处差异已由 D1/D2
决策消解（收敛 by-move + 按 struct 借用重写）。**剩下是工程执行，无未知技术阻塞。**

---

## 1. Phase 0 —— 解锁 `import std.vec`（✅ 已完成）

**问题**：`vec` 被扫描为 `TOKEN_VEC` 关键字（`src/scanner.c:167`），而 import 路径段
旧代码要求 `TOKEN_IDENTIFIER`（`src/parser.c`），导致 `import std.vec` 截断成 `"std"`。

**已做的修改**（`src/parser.c` `parse_import_decl`）：新增谓词
```c
static bool is_import_path_segment(TokenType t) {
    return t == TOKEN_IDENTIFIER ||
           t == TOKEN_VEC || t == TOKEN_MAP || t == TOKEN_ARRAY;
}
```
两处 `check(p, TOKEN_IDENTIFIER)` 改用它。模块文件解析本就按 `std.vec → std/vec.ls`
（`src/module.c:99,128`）。**已编译通过，import 解析回归正常**（`rawvec_api_test` 仍绿）。

> Phase 3 删除 `TOKEN_VEC` 后，可把此谓词回退为只认 `TOKEN_IDENTIFIER`（`vec` 届时
> 变普通标识符，无需特例）。

---

## 2. Phase 1.5 的语言特性：标识符尾部 `?` / `!` 后缀

这是 API 精简的**前置语言改动**（scanner + 少量 parser/codegen 验证），通用于全语言，
不限 Vec。

### 2.1 语义约定（一个符号一个精确含义，零重叠）
| 后缀 | 含义 | 例 |
|------|------|-----|
| 无 | 普通方法（含原地修改——可变性由 `&!self` 签名表达） | `push`、`pop`、`sort`、`clear` |
| `?` | **谓词**，返回 `bool` | `empty?`、`has?(x)` |
| `!` | **unsafe**：不做边界/有效性检查，越界即 UB，调用方自负责任 | `get!(i)` |

> 不引入 Julia 风「mutation bang」（`sort!`）：LS 已用 `&!self` vs `&self` 精确表达可变性，
> 再加 `!` 冗余。`!` 专用于 unsafe。

### 2.2 词法设计（`src/scanner.c` `scan_identifier`，:313）
当前规则：`while (isalnum || '_') advance;`。**改为**：扫完基础标识符后，按下列规则
可选并入**一个**尾缀符号——

1. 先对**基础词**做 `check_keyword`。**若是关键字（`if`/`for`/`vec`…），不并入任何尾缀**
   （避免 `if!cond` 被读成标识符 `if!`；`if !cond` 有空格本就无歧义）。
2. 基础词是普通标识符时：
   - 见 `?` → 并入（`?` 当前完全未使用，零冲突）。
   - 见 `!` 且**下一个字符不是 `=`** → 并入（保 `a!=b` 仍为 `a != b`；`v.get!(i)` 为 `get!`）。
   - 至多并入一个尾缀符号（不支持 `get!?` 之类）。
3. 含尾缀的词一律返回 `TOKEN_IDENTIFIER`，token 文本含该符号。

### 2.3 上下游影响（多数零改动）
- **parser**：方法声明/调用/`impl`/字段访问读取的是 `TOKEN_IDENTIFIER` 文本，尾缀随文本
  自然带入 → 声明 `fn empty?(&self)->bool` 与调用 `v.empty?` **无需改 parser**。需冒烟验证
  `infix_field`（`src/parser.c:1549`）对 `.empty?` 正常。
- **codegen 符号名**：`Vec(int).get!` 作 LLVM 函数名——LLVM 允许引号内任意字符（现有
  `"RawVec(int).map(int)"` 已是先例），零问题。
- **f-string / 运算符**：`!=`、前缀 `!expr`、`a?b` 均不受影响（规则 1/2 保证）。

### 2.4 验收
新增 `tests/samples/ident_suffix_test.ls`：定义并调用 `fn ok?(int x)->bool`、
`fn danger!(int x)->int`；断言 `a!=b`、`!flag` 仍正常解析。JIT+AOT 绿。

---

## 3. Phase 1 + 1.5 的 Vec API 最终表

> Phase 1（重命名 RawVec→Vec）与 Phase 1.5（API 精简）建议合并为一次集中改动，落在
> `std/vec.ls` + 14 个原 rawvec 样本上（量小、自包含）。

### 3.1 重命名表（`std/vec.ls` 内 + 所有调用点）
| 旧（RawVec） | 新（Vec） | 类别 |
|--------------|-----------|------|
| `struct RawVec(T)` | `struct Vec(T)` | 类型 |
| `impl(T) RawVec(T)` | `impl(T) Vec(T)` | impl |
| `length()` | `len()` | 重命名 |
| `capacity()` | `cap()` | 重命名 |
| `find_index(pred)` | `pos(pred)` | 重命名 |
| `is_empty()` | `empty?` | 谓词后缀 |
| `contains(x)` | `has?(x)` | 谓词后缀 |
| `get_unsafe(i)` | `get!(i)` | unsafe 后缀 |
| 其余（`push`/`pop`/`insert`/`remove`/`clear`/`get`/`set`/`swap`/`reverse`/`resize`/`slice`/`extend`/`copy`/`truncate`/`reserve`/`shrink_to_fit`/`any`/`all`/`count`/`count_eq`/`each`/`filter`/`find`/`map`/`reduce`/`sort`/`sort_by`/`index_of`/`first`/`last`/`as_ptr`） | **不变** | — |
| 保留协议方法 `__from_list`/`__index`/`__index_set`/`__clone`/`__drop` | **不变** | — |

> 注：`__index`/`[]` 与 `get(i)` 当前都是**不查边界**的裸读（rawvec.ls:127 注释）。本次仅把
> 显式的 `get_unsafe` 升级为 `get!` 以彰显语义；`get(i)` 是否改返回 `Option(T)`（安全版）
> 留作后续（CLAUDE.md §6 待实现项），**本计划不动**。

### 3.2 字面量/构造（迁移配方核心）
- 空：内建 `vec(T) v = []` → `Vec(T) v = {}`
- 字面量：内建 `vec(T) v = [a,b,c]` → `Vec(T) v = [a,b,c]`（`__from_list` 协议，**不变**）

---

## 4. 测试桶（精确清单，Phase 2 派发单元）

`tests/samples/` 引用 `vec` 的文件共 **148 个**。

### 桶 A — 随 Phase 1/1.5 重命名（14 个，已是 Vec 语义）
`rawvec_api_test` `rawvec_functional_p3_test` `rawvec_kid_lazy_test`
`rawvec_kid_missing_eq_fail` `rawvec_m1_test` `rawvec_m2_test` `rawvec_map_reduce_test`
`rawvec_move_test` `rawvec_parity_p1_test` `rawvec_poc_test` `rawvec_ptr_index_test`
`rawvec_realloc_test` `rawvec_sizeof_test` `inferred_init_test`
→ 改 `import std.rawvec`→`std.vec`、`RawVec`→`Vec`、应用 §3.1 API 新名。
→ **文件改名** `rawvec_*` → `vec_*`（与桶 B 旧 `vec_*` 可能撞名，加前缀如 `vecstd_*` 或
   保留 `rawvec_` 文件名只改测试 NAME——见 §5 Phase 1 注）。
→ `.cmake` + CMakeLists：`test_rawvec_*` → `test_vec_*`，更新 `DEPENDS` 链。

### 桶 D — 闭包捕获 vec（~8，需 by-move 改写，**等 §2 语言特性 + D1**）
`closure_e1_test` `closure_e2_e4_test` `closure_f1_test` `closure_f4_test`
`closure_f5_test` `closure_f7_stress_test` `closure_g` `closure_phase_c7_test`
→ 逐个判定：闭包是否**捕获并修改**外层 vec。若是 → 改为传 `&!Vec` 可写借用，或闭包返回
   新 Vec。纯读捕获 by-move 即可。每个 `--memcheck` 0/0/0。

### 桶 E — vec 借用/move 语义（~23，**按 D2 重写**）
- vecref（14）：`vecref_neg_alias` `vecref_neg_copy_out_mut` `vecref_neg_copy_out_readonly`
  `vecref_neg_elem_assign_readonly` `vecref_neg_implicit_mut` `vecref_neg_push_readonly`
  `vecref_neg_readonly_upgrade` `vecref_pos_downgrade` `vecref_pos_forward`
  `vecref_pos_methods` `vecref_pos_push` `vecref_pos_read` `vecref_pos_string_elem`
  `vecref_pos_write_elem`
- move/borrow 含 `vec(...)`（9）：`move_explicit_test` `move_phase_b_loop_neg`
  `move_phase_b_neg` `move_phase_b_test` `move_struct_test` `borrow_caller_live_test`
  `borrow_neg_move` `borrow_neg_move_explicit` `mutref_step2_neg_move`
→ 审计每条 negative「保护什么」：保留对 `&Vec`（只读）/`&!Vec`（可写）仍成立的（如只读
   借用不能 push、别名 mut+mut 拒绝），按 struct 借用语义重写；删除仅对内建 vec 成立、
   struct 借用下不再适用的。pos 用例迁移为 `Vec` + `&Vec`/`&!Vec`。

### 桶 B/C/F — 机械迁移（~103，§7 配方）
其余全部：纯 vec 使用（`vec_test` `vec_simple_test` `vec_bounds_test` `vec_fn_test`
`vec_global_test` `vec_local_only` `vec_algorithms_test` `vec_struct_test` `vec_string_test`
`vec_get_test` `vec_get_unsafe_test` `vec_literal_test` `vec_functional_v1..v5` `vec_batch_a..e`
`vec_string_move_test`/`2` `vec_struct_clone_test` `vvs_test` 等）、vec 作字段/嵌套
（`enum_*vec*` `struct_*` `cmatrix/*` `plot_*` `json_*` `stack`/`ring`）、std 库间接
（`std.md`/`std.html`/`std.json`/`re_*`/`fs_*`/`io_*` 自身 `import std.vec`）。
→ 按 §7 配方逐文件改；每批 `--repeat until-pass:2` 全绿。

---

## 5. 施工阶段（每阶段 ctest 全绿为关；建议每阶段一提交，可二分回退）

### Phase 1 + 1.5（合并）：重命名 + API 精简 + `?`/`!` 特性
1. **语言特性**（§2）：改 `scanner.c:scan_identifier` 加尾缀规则；加 `ident_suffix_test`。
   先单独验证此特性 JIT+AOT 绿，再动 Vec。
2. **`std/rawvec.ls` → `std/vec.ls`**：`git mv`；改 struct/impl 名 + 应用 §3.1 API 表。
3. **14 个桶 A 样本** + 对应 `.cmake` + CMakeLists：改 import/类型/API 名/测试 NAME/DEPENDS。
   > 文件名冲突处置：桶 A 改名后若与桶 B 现存 `vec_*.ls` 撞名，桶 A 文件用 `vecstd_` 前缀
   > （如 `vecstd_api_test.ls`），或暂保留 `rawvec_` 文件名仅改 ctest NAME 为 `test_vec_*`。
   > **推荐**：保留物理文件名 `rawvec_*.ls` 不动（避免连锁），只改 ① 文件内容 ②.cmake 里
   > 的 SRC 指向不变 ③ ctest NAME `test_rawvec_*`→`test_vec_*` ④ DEPENDS。物理改名留到
   > Phase 2 桶 B 旧 `vec_*` 迁移完、命名空间腾出后再统一。
4. 验收：全量 ctest 绿（数量不变，纯重命名+特性，零语义回归）。

### Phase 2：迁移测试（分桶推进，每桶一提交）
- **2A** 桶 B/C/F 机械迁移（§7 配方）——可与 2D/2E 并行。
- **2D** 桶 D（闭包 by-move 改写）。
- **2E** 桶 E（borrow/move 按 struct 语义重写）。
- 每子阶段：迁移 → 该批绿 → 全量 `--repeat until-pass:2` 绿 + 抽样 `--memcheck` 0/0/0。

### Phase 2.5：内建 string→vec 方法下沉到 `impl string`（**Phase 3 前置**）✅ 已完成（2026-06-07）
> 详细设计 + 落地补记见独立文档 [plan_impl_builtin_types.md](plan_impl_builtin_types.md)。
> 摘要：`impl string` 语言特性三层落地；`split`/`lines`/`chars`/`join` 迁到 `std/string.ls`
> 返回 `Vec(T)`，内建 checker+codegen 分支删除；调用方「内建优先→回退用户 impl」。
> `test_impl_string`（JIT+AOT+memcheck 0/0/0），迁移文件 + std.md/std.plottl 全绿，ctest 162/162。
> 已知限制 VR-LIM-002（模块函数内 Vec 局部不自动 drop）用显式 clear+shrink 绕行。
- 新增语言特性「`impl` 内建类型（扩展方法）」：parser 允许 `impl string`、checker 注册到
  `impl_registry["string"]`、codegen 发裸名 `string.<method>`、调用解析内建优先→回退用户 impl。
- 把 `s.split`/`s.lines`/`s.chars`/`sep.join`（现 `codegen.c:6618/6938/7199/6654` 用 C 构造
  内建 vec）迁到纯 LS `std/string.ls` 的 `impl string`，返回 `Vec(T)`；删除对应内建分支 +
  runtime helper（`__ls_str_split`/`__ls_str_join`）。
- 受影响调用点（~10 文件：`std/md.ls` `std/plottl.ls` + `str_split_*`/`string_utils_test`/
  `regex_test`/`string_batch3`/`string_loop` 等）加 `import std.string`。
- **硬约束**：必须在 Phase 3 之前完成——否则删 `TYPE_VECTOR` 后这些方法返回类型悬空。
- 验收：`impl_string_test` + 迁移文件全绿 + memcheck 0/0/0 + 输出与内建逐字一致。

### Phase 3：拆除内建 vec（**最后做，不可逆，分 3A~3F 小步**）
- **3A** scanner：删 `TOKEN_VEC`（`scanner.c:167`）；Phase 0 谓词回退为只认 IDENTIFIER。
- **3B** parser：删 `vec(T)` 类型语法 + vec 字面量特殊路径（**保留**通用 `[..]`→`__from_list`）。
- **3C** types：删 `TYPE_VECTOR`/`type_vector`/相关谓词（`types.h:2`、`types.c:5`）。
- **3D** checker：删 27 处 vec 特例（`checker.c`：方法表、借用 vec 分支、move vec 分支、
  字面量类型推断）。
- **3E** codegen：删 69 处（`codegen.c`：`ls_vec_type`、`emit_vec_*`、scope cleanup vec 分支、
  `capture_type_is_*_cg` vec 分支、`cg_push_temp_drop` vec 分支、`AST_INDEX`/字面量 vec 路径…）。
- **3F** docs：CLAUDE.md §1 特性表/§8 捕获表、`plan_vec_functional.md` 等标注「vec 已降级为
  std.vec」。
- 每删一类**立即全量 `--memcheck`**；全部完成后全样本 memcheck 0/0/0 + 抽样 AOT。

---

## 6. 风险登记册
| 风险 | 等级 | 缓解 |
|------|------|------|
| `?`/`!` 尾缀误伤 `!=`/`!expr`/关键字 | 中 | §2.2 规则 1（关键字不并尾缀）+ 规则 2（`!` 后非 `=` 才并）；`ident_suffix_test` 专测边界 |
| Phase 3 删除后大面积 memcheck 回归 | 高 | 分 3A~3F 小步提交，每步全量 memcheck，可二分回退 |
| 桶 A 文件改名连锁打乱 ctest 链 | 中 | Phase 1 先只改 ctest NAME/DEPENDS，物理改名延后（§5 Phase1 注） |
| D1 by-move 改写引入悬垂/双释放 | 中 | by-move 是已验证成熟路径；逐个 memcheck |
| 性能回归（纯 LS Vec 慢于内建） | 中 | 依赖 JIT O2 内联（plan_rawvec §6）；迁移后跑 alloc bench 复测 |
| 两套容器并存期 `[..]` 字面量歧义 | 低 | 并存期 `vec(T)=[..]` 走内建、`Vec(T)=[..]/{}` 走 `__from_list`；类型标注消歧 |

### 6.1 Phase 2 已知限制 / 暂不处理问题（迁移时记录）

这些是迁移过程中暴露的旧内建 `vec` 与纯 LS `Vec` 语义差异或既有编译器边界。本轮替换先记录并绕行，
不展开编译器修复。

| 编号 | 问题 | 触发/表现 | 当前绕行 |
|------|------|-----------|----------|
| ~~VR-LIM-001~~ | ~~`Vec` 不是编译器内建 for-in 迭代对象~~ | ✅ 已解除（2026-06-07）：用户迭代协议已落地，`Vec.iter()` + `VecIter.next()` 支持 `for x in v`，含 rvalue 源/嵌套迭代。 | 验证：`test_iter_protocol`（JIT+AOT+memcheck 0/0/0）。迁移时可保留/使用 `for x in Vec`。 |
| ~~VR-LIM-002~~ | ~~纯 LS `Vec` 全局变量不会自动调用 `__drop`~~ | ✅ 已解除（2026-06-07）：全局 cleanup 已覆盖 `TYPE_STRUCT(has_drop)` 等用户容器路径。 | 验证：`test_vec_global_drop`（JIT+AOT+memcheck 0/0/0）。迁移时不再需要末尾手动 `clear()` + `shrink_to_fit()`，除非样本本身想测试该 API。 |
| VR-LIM-003 | 内建 `vec` 的越界容错/默认值 API 与 `Vec` 不同 | 旧样本依赖 `get(99)`/`first()`/`last()`/`remove(99)`/`swap(0,99)` 警告并返回默认值；纯 LS `Vec` 当前多为 unchecked 或返回 `Option(T)` | 迁移样本按 `Vec` API 重写断言；越界容错旧行为不保留 |
| VR-LIM-004 | 内建 `vec.resize(n)` 有默认填充值，`Vec.resize(n, fill)` 需要显式 fill | `vec_batch_d` 一类样本调用 `resize(6)` 期待 0/空字符串填充 | 改为 `resize(n, 0)`、`resize(n, f"")` 等显式填充值 |
| VR-LIM-005 | 闭包捕获外层 `Vec` 修改不等价于内建 `vec` by-ref 捕获 | `nums.each(|x| { acc.push(x) })` 这类样本对纯 LS `Vec` 会触发 by-move/所有权语义差异 | 桶 B 机械迁移中先避免外层 `Vec` 写捕获；需要改为 `&!Vec` 参数或归入桶 D 专项 |
| VR-LIM-006 | 闭包直接返回 string 形参存在既有脆弱边界 | 长链测试中 `reduce(string)(..., |acc, s| { if acc.length == 0 { return s } ... })` 曾出现首元素错值 | 样本中改为返回新字符串表达式，如 `f"" + s`；编译器层后续另案 |
| ~~VR-LIM-007~~ | ~~自定义 `__drop` 副作用精确计数与纯 LS `Vec` 不等价~~ | ✅ 实质解除为迁移规约：`Vec` 走普通用户容器 clone/drop 语义，副作用计数不应再按内建 `vec` 的旧精确次数断言。 | 迁移 `vec_struct_test` 这类样本时改为行为 + memcheck 验收；不要保留旧 `drop_count` 精确实现细节断言。 |
| ~~VR-LIM-008~~ | ~~`Vec(struct含string)[i]` 读出 clone 存在剩余泄漏边界~~ | ✅ 完全解除（F3，2026-06-08）：先前 `v[i].field` / `f(v[i])` / `Person p=v[i]` 已 0/0/0（`test_vec_owndrop`）；本次修复残留的「整个 has_drop struct rvalue 直接作 `print` 实参」——print struct 路径（`codegen.c:5334`）求值 arg 得到 deep-clone 后只打印不析构。修法：打印后，若 `t.has_drop` 且 arg 为 owned-rvalue 产生者（`AST_INDEX`/`AST_CALL`），spill 到 temp + `emit_drop_value`；裸 ident/field-read（live 借用）不析构。vec/map 元素 print 路径经测无此泄漏，无需改。 | 验证：`vec_struct_clone_test` 还原 `print(vp[0])`（含 index-clone + call-return），JIT+AOT+memcheck 0/0/0，clone 独立性正确；ctest 166/166。可迁移 `print(v[i])` 路径。 |
| ~~VR-LIM-009~~ | ~~`Vec(string).push(rvalue string)` 与后续覆盖/清理组合存在泄漏边界~~ | ✅ 已解除（2026-06-07）：owned rvalue string move-into-container ABI 已覆盖用户 `Vec.push/insert/set/index_set`。 | 验证：`test_vec_owndrop`（JIT+AOT+memcheck 0/0/0）。迁移时可直接保留 `v.push("x".upper())` 等 rvalue 写入。 |
| ~~VR-LIM-010~~ | ~~命名函数值不能直接传给 `Block(...)` 参数~~ | ✅ 已解除：命名函数可作为 `Block(...)` 值使用。 | 验证：`test_fn_as_block`。迁移时可把 `d.sort_by(cmp_desc)` 保留为命名函数实参。 |
| ~~VR-LIM-011~~ | ~~`Vec` 作为 has_drop enum payload 不稳定~~ | ✅ 已解除（2026-06-07）：enum payload 持有用户 `Vec(T)` 已覆盖。 | 验证：`test_enum_user_vec_payload`（JIT+AOT+memcheck 0/0/0）。可迁移 enum payload 中的 `vec(T)` → `Vec(T)`。 |
| ~~VR-LIM-012~~ | ~~struct 字段默认值中的 `[..]` 未走 `Vec.__from_list`~~ | ✅ 已解除（2026-06-07）：struct 字段默认值已支持用户容器 `__from_list` / 空初始化推断。 | 验证：`test_struct_field_defaults_uservec`（JIT+AOT+memcheck 0/0/0）。可迁移字段默认值里的 `vec(T)` → `Vec(T)`。 |
| VR-LIM-013 | `Vec(Option(T))` 这类嵌套泛型实参仍有替换边界 | 2026-06-07 复核 `std.ring` 迁移时触发：`Vec(Option(T)).get/set` 单态化后把元素类型错推成内层 `T`，报 `cannot initialize 'tmp' (type 'int') with value of type 'Option(int)'` / `cannot assign '*string' to '*Option(string)'`。 | 本轮暂不迁移 `std.ring` 的 `vec(Option(T))` backing buffer；待泛型实参递归替换修复后再迁。 |
| ~~VR-LIM-014~~ | ~~`Vec.pop()` discard rvalue `Option(T)` temp 未释放内部 has_drop T~~ | ✅ 已修复（F2，2026-06-08）：根因是 `AST_EXPR_STMT` 丢弃一个返回 owned has_drop 值（by value）的**调用**时，返回的 rvalue 聚合无人绑定/析构。修法：`AST_EXPR_STMT` 在 `codegen_expr` 后，若被丢弃表达式是 `AST_CALL` 且 resolved_type 为 has_drop struct/enum 或 vec/map，则 spill 到 temp alloca 并 `cg_push_temp_drop`，由随后的 `cg_flush_temps` 析构。**仅限 `AST_CALL`**（裸 ident/field-read 是对 live 绑定的借用，不可析构）；`TYPE_STRING` 排除（已由 temp-string 机制释放）。 | 验证：`vec_string_test` 还原裸 `v.pop()`，JIT+AOT+memcheck 0/0/0；chained/filter/函数返回 Option 等丢弃路径专测 0/0/0；ctest 166/166。 |
| VR-LIM-015 | `Vec(T)` generic 方法的 by-value 参数不标记 named var 为 moved | 2026-06-08 迁移 `test_mem_m5_neg_push` 时发现：`Vec(string).push(s)` 调用后 checker 不标记 `s` 为 moved（generic `T` 参数不触发 move 分析），内建 `vec(string).push(s)` 正常标记。 | 迁移时负向测试（move-after-use 检查）不适用于 Vec(T)。`Vec(T)` 的 by-value 参数一律 clone（源继续保持 live）。 |
| ~~VR-LIM-016~~ | ~~全局变量 `Vec(T) v = [literal]` 触发 `__from_list` 缺失~~ | ✅ 已修复（F1，2026-06-08）：根因是 `__ls_global_stmts`（含全局 init）在 G1.5 pending-generic-method 发射**之前**生成，故 `Vec(T).__from_list` 尚未单态化。修法：`emit_user_from_list_value` 在 `LLVMGetNamedFunction` 落空时改 `cg_declare_pending_generic_method` 从 checker pending 队列前向声明，body 仍在 G1.5 发射（镜像局部路径与其他泛型调用点）。 | 验证：`test_global_vec_lit` 还原全局字面量 `Vec(int)=[1,2,3]`/`Vec(string)`/`Vec(Tag)`（has_drop），JIT+AOT+memcheck 0/0/0。ctest 166/166。 |
| VR-LIM-017 | `Vec(Block(...))` 不兼容——`push` 内部赋值 Block 参数被 checker 拒绝 | 2026-06-08 迁移 `closure_g.ls` 时发现：`Vec(Fn).push(|x| x + base)` 触发 `cannot assign Block parameter`。Vec.push 的方法体 `self.data[self.len] = x` 中 `x` 是 `T` 类型参数，当 `T=Block` 时 checker 拒绝赋值 Block 参数。 | 本轮不迁移含有 `Vec(Block)` 的文件。保留内建 vec。待编译器允许 Block 参数赋值后再迁。 |

---

## 7. 附录：机械迁移配方（桶 B/C/F 逐文件）

对每个文件：
1. 顶部加 `import std.vec`（若已 import 其他 std，并列加）。
2. 类型声明：`vec(T)` → `Vec(T)`（含字段 `vec(T) f`、参数 `vec(T) x`、返回 `-> vec(T)`、
   嵌套 `vec(vec(T))` → `Vec(Vec(T))`）。
3. 构造：`= []`（空）→ `= {}`；`= [a,b,c]`（字面量）→ 不变。
4. **API 名差异**（最易踩坑）：
   - `v.length` （内建是**字段**，无括号）→ `v.len()`（方法，**加括号**）
   - `v.capacity` → `v.cap()`
   - `v.is_empty()` → `v.empty?`
   - `v.contains(x)` → `v.has?(x)`
   - `v.find_index(p)` → `v.pos(p)`
   - `v.get_unsafe(i)` → `v.get!(i)`
   - `v[i]` 读/写、`v.push/pop/get/set/...` → 不变
5. 借用参数：`&vec(T)` → `&Vec(T)`、`&!vec(T)` → `&!Vec(T)`。
6. 验证：`ls run <f>`（断言串 PASS）→ `ls run --memcheck <f>`（SUMMARY 0/0/0）→
   `ls compile <f> -o tmp.exe && tmp.exe`（AOT 一致）。三绿才算迁移完成。

### API 速查（内建 vec → Vec）
| 内建 vec | Vec | 注意 |
|----------|-----|------|
| `v.length`（字段） | `v.len()`（方法） | **加括号** |
| `v.capacity` | `v.cap()` | 加括号 |
| `v.is_empty()` | `v.empty?` | 去括号、加 `?` |
| `v.contains(x)` | `v.has?(x)` | 改名 |
| `v.find_index(p)` | `v.pos(p)` | 改名 |
| `v.get_unsafe(i)` | `v.get!(i)` | 改名 |
| `v[i]` / `v.push(x)` / `v.pop()` / `v.get(i)` / `v.set(i,x)` / `v.map/filter/reduce/...` | 同名 | — |
| `vec(T) v = []` | `Vec(T) v = {}` | 空构造 |
| `vec(T) v = [a,b]` | `Vec(T) v = [a,b]` | 字面量不变 |
