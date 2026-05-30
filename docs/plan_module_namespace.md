# 模块命名空间收尾：全局变量 + struct/enum 类型

> 状态：Part 1 全部完成 ✅ + B-1 完成 ✅ ｜ B-full（B-2~B-6）已定方向：**类型位置限定 `mod.Type`/`alias.Type`**（见 §2.1bis），待实现 ｜ 关联：L-009 / L-009.1（`docs/plan_l009_mangling.md`）
> 验证基线：2026-05-30（本文所有「实测」均基于 `build/Release/ls.exe`）
> 范围：把"模块命名空间"从**函数**（已完成）推广到**全局变量**与 **struct/enum 类型/方法**。

---

## 0. 背景：模块命名空间的现状（实测）

| 类别 | 有命名空间？ | 现状 |
|------|:---:|------|
| 自由函数 | ✅ | L-009：符号 `<mod>__fn`，同名跨模块各自正确 |
| 泛型函数 | ✅ | L-009.1 6.A：符号 `<mod>__fn(args)` |
| **全局变量** | ❌ | **连单模块都不工作**：模块函数体引用模块全局变量 → `undefined variable`；且 LLVM 名裸名、跨模块会冲突（详见 Part 1） |
| **struct/enum 方法** | ❌ | 符号裸名 `Struct.method`（同名 struct 带方法被 checker 提前拦截，故暂够不到） |
| **struct/enum 类型** | ❌ | 同名 struct 跨模块：不同字段数 → codegen GEP 崩溃；不同布局且都用 → 静默内存损坏（详见 Part 2） |

**统一机制**：复用 L-009 已有的模块前缀方案 `cg_module_fn_symbol`（`src/codegen.c`，scheme `<modpath 把 . 换成 _>__<name>`）+ `ctx->current_emit_module`（codegen 正在发射哪个模块）+ `Checker.module_name` / `registry->current_check_module`（checker 正在检查哪个模块，L-009.1 已引入）。

> 本文分两个**相互独立**的 Part。Part 1（全局变量）更小、自包含，建议先做。Part 2（类型）更大、有语言设计点。每个 Step 结束都必须 `ctest` 全绿，并尽量新增测试。

---

## Part 1：模块全局变量

### 1.0 问题与根因（实测）

```ls
// mod_a.ls
module mod_a
int counter = 100
fn get() -> int { return counter }   // 实测：[codegen] undefined variable 'counter'
```
单模块即失败；根文件同写法正常。

**根因（两处）**：
1. **scope 未注册**：codegen 模块 Pass A（`codegen.c:19126` 的 `AST_VAR_DECL` 分支）只 `LLVMAddGlobal` 声明了全局，但**没有 `cg_scope_define`**。模块函数体（Pass B）按名 `cg_scope_resolve("counter")` 查不到 → 报错。根全局变量在根 Pass 1 里被 `cg_scope_define`，故正常。
2. **无命名空间**：模块全局 LLVM 名是**裸名** `@counter`（`codegen.c:19132-19133`）。即使能用，两模块的 `counter` 也会在 IR 冲突。`emit_global_var_init`（`codegen.c:17726`）也用裸名 `LLVMGetNamedGlobal(decl->as.var_decl.name)` 存初值。

> 注：初始化调度本身已覆盖模块全局（`codegen.c:19339` 遍历模块 VAR_DECL 调 `emit_global_var_init`），只是符号名与 scope 没接通。

### 1.1 设计

- 模块全局 LLVM 符号前缀化：`<mod>__counter`（复用 `cg_module_fn_symbol`）。
- 模块函数体发射时（Pass B，`current_emit_module` 已设为该模块），把该模块的全局变量以**裸名 key** `cg_scope_define` 进一个 scope，value 指向**前缀化的 LLVM 全局**。每个模块发射前后 push/pop，避免跨模块 scope 冲突（与"每模块独立 emit"一致）。
- `emit_global_var_init` 在 `current_emit_module != NULL` 时用前缀名解析全局。
- 模块全局 free（`codegen.c:19476` 区域，has_drop 全局的退出清理）同步前缀名。

### 1.2 Step 拆分（每步独立、ctest 全绿）

#### Step P1-1：模块全局符号前缀化 + scope 注册（核心修复）✅ 已完成（2026-05-30）
- 改 `codegen.c:19126` Pass A：用 `cg_module_fn_symbol(sym, current_emit_module, var_name)` 作为 `LLVMAddGlobal` 名（`current_emit_module` 此时已在 Pass A 循环里设好）。
- 新增：在 Pass B 每个模块迭代里，先把该模块的所有 `AST_VAR_DECL` 以裸名 `cg_scope_define` 到 `ctx->current_scope`（value=前缀化全局，type=resolved_type）。push/pop 一层 scope 包裹该模块的 Pass B 发射；或在根 scope 注册后于模块切换时清理。
  - ⚠️ 实现注意：Pass B 当前对所有模块共用一个循环；需要在「设 `current_emit_module = modules[m].name`」后、发射该模块函数体前注册其全局，发射后注销（避免 mod_b 的 body 看到 mod_a 的 `counter`）。
- 改 `emit_global_var_init`（`codegen.c:17726`）：解析全局名时若 `current_emit_module != NULL` 用前缀名。该函数在 `__ls_global_stmts`（`codegen.c:19328` 模块 init 循环）里被调用——需在调用前临时设 `current_emit_module = modules[m].name`，调用后还原。
- **验证**：单模块全局变量读/写；`mod_a.get()==100`。
- **新测**：`tests/samples/modvar_basic/`（单模块 + 根 import），JIT+AOT+memcheck。

#### Step P1-2：跨模块同名全局变量 ✅ 已完成（2026-05-30）
- 依赖 P1-1 的前缀化即天然解决（`mod_a__counter` vs `mod_b__counter`）。
- **验证**：两模块各 `counter`（100/200）→ `a=100 b=200`。
- **新测**：扩展 `tests/test_modvar.cmake` 增加双模块同名用例。

#### Step P1-3：has_drop 全局变量（string/struct/vec/map）跨模块 ✅ 已完成（2026-05-30）
- 审查模块全局的 drop/free 路径（`codegen.c:19476` 区域）用前缀名；确保退出清理对每模块各自的全局只 drop 一次。
- **验证**：模块里 `string g = "x".upper()` 全局；memcheck clean。
- **新测**：has_drop 全局变量样例（string + struct），memcheck 0 leak/0 dfree。

#### Step P1-4：模块全局变量的可变性 + 跨函数共享 ✅ 已完成（2026-05-30）
- 验证模块内多个函数读写同一全局（`inc()` / `get()`）状态一致；模块全局对导入方**不可**直接裸名访问（只能通过模块函数），符合现有封装语义——确认 checker 行为并加测试锁定。
- **新测**：模块全局自增计数器，多次调用累加正确。

---

## Part 2：struct / enum 类型命名空间

### 2.0 问题与后果（实测矩阵）

| # | 场景 | checker | 后果 |
|---|------|---------|------|
| S1 | 同名 + 相同布局 + 无方法 | 放行 | ✅ 侥幸正常 |
| S2 | 同名 + **不同字段数** + 无方法 | 放行 | ❌ **codegen 崩溃** `Invalid indices for GEP` |
| S3 | 同名 + 有方法 | ❌ `conflicting method` | 已拦截 |
| S4 | 同名 + 同大小 + 字段顺序不同 | 放行 | ✅ 侥幸正常（索引自洽） |
| S5 | 同名 + 类型不同 + 只用一个模块 | 放行 | ✅ 侥幸正常 |
| S6 | 同名 + **不同布局** + 两模块都用 | 放行 | ❌ **静默内存损坏**（乱码/非确定崩溃） |
| S7 | 根 struct vs 模块 struct 同名 | ❌ `already defined` | 已拦截 |

**根因**：checker 的 `find_struct_type`（`checker.c:128`）/ impl_registry（`checker.c:492` `find_or_create_impl`）与 codegen 的 `find_struct_llvm`/`register_struct_llvm`（`codegen.c:416/431`）+ `type_to_llvm` TYPE_STRUCT（`codegen.c:3632`，按 `t->as.strukt.name` 索引 + `LLVMStructCreateNamed`）全部**按裸 struct 名索引**。两模块的 `struct Config` 塌缩成同一个 `%Config`（first/last wins），布局不一致即崩溃/损坏。方法名（`codegen.c:16014` `Struct.method`）同样裸名。

### 2.1 两条路线

- **B-safe（检测报错）**：把 S2/S6 这两个漏网 footgun 也变成像 S3/S7 那样的清晰**编译错误**。小、低风险。同名 struct 仍不能跨模块共存（需用户重命名）。
- **B-full（真命名空间）**：同名 struct 跨模块各自独立可用。大、需语言设计决策（导入方裸名引用歧义）。

> 建议：**先做 B-safe（Step B-1）独立交付**——立即消除崩溃/静默损坏；再按需推进 B-full（Step B-2~B-6）。B-safe 完成后即可暂停而不留 footgun。

### 2.2 Step 拆分

#### Step B-1（B-safe）：同名类型跨模块冲突 → 清晰编译错误 ✅ 已完成（2026-05-30）
- 在 checker 收集导入符号处（`checker.c:7895` 注册导入 struct / `7905` 注册导入 enum）：注册前检查 importer 的 struct/enum 注册表里是否已有同名条目，且**布局不同**（字段数/字段名/字段类型/或简单地：只要来自不同模块的同名定义）。
  - 若冲突 → `checker_error`：`type 'Config' is defined in multiple modules (mod_a, mod_b); rename one or use a single source`。
  - 相同布局（S1/S4/S5）可选择放行（保持现状）或一并提示（推荐：不同模块同名一律报错，最安全、最简单；S1 那种"侥幸"也不值得依赖）。
- **风险**：可能让现有"侥幸能跑"的代码（S1/S4/S5）开始报错。需先全仓 grep 是否有 stdlib/测试依赖跨模块同名类型（预期无）。
- **验证**：S2/S6 现在给出清晰错误而非崩溃；S3/S7 行为不变；正常程序不受影响。
- **新测**：`tests/test_modtype_conflict.cmake`：S2/S6 期望编译错误（非 0 退出 + 含 "multiple modules"）；并跑一遍现有 json/stdlib 确认无回归。

### 2.1bis B-full 设计：类型位置限定（复用现有 `import as` + `mod.member` 文法）

**消歧方案已定**（2026-05-30，实测驱动）：用**类型位置限定** `mod_a.Config` / `A.Config`，
对称扩展现有的 `mod.fn()` 调用限定。理由：
- 实测 `import mod_a as A` + `A.make()`（值/调用位置的模块成员访问）**已支持**；缺的只是
  让同样的 `A.Config` 在**类型位置**也能用。不是发明新机制，是对称补齐。
- 不需要符号级 import（`from mod import X` 这种 LS 目前完全没有，代价大）。

**统一前缀方案**：内部仍复用 `cg_module_fn_symbol` 的 `<mod>__Name`（`.`→`_`）作为
struct/enum 的**内部唯一名**（Type.name / LLVM 类型名 / 方法名）；`mod_a.Config` 只是
**用户书写的限定引用**，在 checker 里解析到那个前缀化的 Type。

**关键代码坐标**（已核实，行号可能漂移，实现前用 Grep 复核）：
| 角色 | 位置 |
|------|------|
| 非泛型 struct Type 构建 + 注册 | `checker.c:6702/6760`、`7223/7260`、`8227`（`type_struct(name,n)` + `register_struct_type(c,name,st)`） |
| struct/enum 注册表查找 | `find_struct_type`（`checker.c:128`）/ `find_enum_type` |
| 类型解析（命名类型） | `resolve_type_node` `TYPE_NODE_NAMED` 分支（`checker.c:1114`，无 arg 时 alias→struct→enum 按名查） |
| TypeNode 命名结构 | `ast.h:62` `named { char *name; TypeNode **args; int arg_count; }`（**需加 `char *module` 限定符**） |
| parser 命名类型 | `parser.c:1711`（IDENT [+ `(args)`]） |
| parser 变量声明启发式 | `starts_var_decl`（`parser.c:1745`） |
| import 文法（已支持 `as`） | `parse_import_decl`（`parser.c:2652`，`import a.b.c [as X]`） |
| 模块导出表 | `type_module_add_export`（`types.c:179`），`Type.as.module.exports[]{name,type}`（**需加 find_export**） |
| codegen 方法 qualified name | `codegen_impl_decl`（`codegen.c:16001`，用 `impl_decl.name`） |
| checker 模块上下文 | `Checker.module_name` / `registry->current_check_module`（L-009.1 已引入） |

### 2.2 Step 拆分（type-position-qualification 方向）

> 每步独立、ctest 全绿（当前基线 **78/78**）+ JIT/AOT/memcheck 三重 + json/stdlib 无回归。

#### Step B-1（B-safe）：同名类型跨模块冲突 → 清晰编译错误 ✅ 已完成（2026-05-30）
（见上方，已实现于 checker import 注册处，`test_modtype_conflict`。）

#### Step B-2：struct/enum Type 名烘焙模块前缀 ﹝可交 Sonnet﹞
- 在**模块的递归 checker**里（`c->module_name != NULL`），struct/enum 类型**构建时**把名字设为
  `<mod>__Name`（用与 `cg_module_fn_symbol` 一致的 scheme：`.`→`_` + `__`）。落点：上表三处
  `type_struct(name,…)`/enum 等价处之前，先算前缀名。
- 同一 Type 对象被模块自身 AST 共享 → 模块函数签名 / codegen / 方法调用点（用 Type.name）**自动跟随**前缀。
- importer 注册（`checker.c:7902/7913`）：**暂仍按裸名注册 + 保留 B-1 冲突检测**（B-2 不改变
  "同时 import 两个同名 → 报错"的现状；放开留给 B-4）。单模块场景：裸名 `Config` → 前缀 Type，照常工作。
- **影响面**：`type_name()`（`checker.c:158` 返回 `strukt.name`）→ 错误信息会显示 `mod_a__Config`。
  **本步加一个"展示名"**：`type_name` 对含 `__` 的模块类型，打印时去前缀显示 `Config`（或 `mod_a.Config`），
  内部名不变。
- **验证**：单模块 + 跨模块（只 import 一个）struct/enum 全部照常；dump IR 断言模块 struct LLVM 类型名带前缀。
- **新测**：`test_modtype_ns` 第一组——单模块 struct/enum 经前缀化后行为不变（JIT+AOT+memcheck）。
- **风险**：中（动类型身份，但 78 ctest + memcheck 兜底）。**务必每改一处全量回归。**

#### Step B-3：codegen 方法 / drop / clone 名跟随前缀 ﹝可交 Sonnet﹞
- `codegen_impl_decl`（`codegen.c:16001`）：`impl_decl.name` 在 `current_emit_module != NULL` 时
  前缀化，使方法 qualified name 变 `mod_a__Config.method`。
- 实例/静态方法调用点（`codegen.c:10518` 区域）用的是 struct **Type.name**（B-2 已前缀）→ 自动匹配；
  确认 drop/clone 合成函数名（跟随 struct 名）也随之前缀化（审 `emit_*_drop`/`emit_*_clone` 命名点）。
- **验证**：单模块 struct + 方法 + has_drop 全部正常（前缀只是改了内部符号）。
- **新测**：`test_modtype_ns` 第二组——单模块 struct + 方法 + `__drop`，memcheck clean。
- 注：B-2+B-3 完成后，**同名 struct 在各自模块内部已是不同 LLVM 类型**，只是 importer 仍按 B-1 拦同时导入。

#### Step B-4：类型位置限定语法 `mod.Type` / `alias.Type` ﹝我做 / 至少 starts_var_decl 我审﹞
本步是放开同名共存的关键，也是唯一有 parser 歧义的一步。
1. **AST**：`ast.h:62` `named` 加 `char *module`（NULL=非限定）。
2. **parser**（`parse_type` `parser.c:1711`）：解析首个 IDENT 后，若下一个是 `.` 且其后是 IDENT，
   则把首个当 `module`、其后当 `name`（`A.Config`）。
3. **starts_var_decl**（`parser.c:1745`，**唯一真歧义**）：`A.Config x = …`（变量声明）vs `A.foo`
   （表达式语句）。需 lookahead：`IDENT . IDENT IDENT`（点路径后再跟一个 IDENT）→ 判定为变量声明。
   用 scanner 保存/恢复多 token 前瞻（参考现有 `starts_var_decl` 里 `*ident ident` 的双 peek 实现）。
4. **resolve_type_node**（`checker.c:1114`）：`module != NULL` 时——查 `module`（别名或模块名）对应的
   `TYPE_MODULE`，在其 `exports[]`（新增 `type_module_find_export`）里找 `name` 的类型；找到即返回
   那个前缀化 Type。
5. **importer 歧义放开**（`checker.c:7895/7905`）：裸名 `Config` 来自 2+ 模块时，**不再硬报错**，而是
   注册为"歧义"标记；裸名引用歧义类型 → 报错提示"用 `mod_a.Config` 限定"；限定引用 → 直接走 #4 精确解析。
   （单一来源的裸名仍照常工作，零破坏。）
- **验证**：S6（两模块同名 `Config` 不同布局都用）→ 用 `mod_a.Config`/`mod_b.Config` 限定后各自正确；
  裸名 `Config` → 歧义错误；只 import 一个 → 裸名仍可用。
- **新测**：`test_modtype_ns` 第三组——两模块同名 struct 都用（限定）+ 裸名歧义报错。
- **风险**：高（parser 歧义）。`starts_var_decl` 改动须有针对性用例：`A.Config x`、`A.foo()`（表达式）、
  `A.foo` 字段读、`A.B.C x`（多段）等都不能误判。

#### Step B-5：enum 跨模块 + 变体限定 ﹝我做﹞
- 同名 enum 跨模块（变体名也可能撞，如两个 `enum Status { Ok, Err }`）：B-2 已让 enum 内部名前缀化；
  本步确保**变体构造 / `match`** 在限定与裸名下都正确解析（`find_enum_llvm` / 变体 ctor / 穷尽性）。
- 变体的限定写法（如需）：`mod_a.Status.Ok` 或在已知类型上下文里裸 `Ok`——按实测最小可用为准。
- **验证**：两模块各 `enum Status` 不同语义 → 各自正确（限定）。
- **新测**：`test_modtype_ns` 第四组——同名 enum 跨模块 + match，JIT+AOT+memcheck。

#### Step B-6：has_drop 同名 struct/enum 跨模块综合 memcheck ﹝可交 Sonnet（纯测试）﹞
- 综合压测：两模块同名 has_drop struct（string 字段）+ enum + vec/map 容器，跨模块传值/返回（限定类型）；
  memcheck 0 leak/0 dfree。
- **新测**：`test_modtype_ns` 收尾组（综合 memcheck）。

### 2.3 Sonnet / 我 的分工建议

| Step | 谁 | 理由 |
|------|----|------|
| B-2 类型名烘焙 + type_name 展示名 | **Sonnet** | 机械、边界清晰、78 ctest+memcheck 强兜底；但动类型身份，须每步全量回归 |
| B-3 codegen 方法/drop/clone 跟随 | **Sonnet** | 跟随 B-2，机械 |
| B-4 限定类型语法（含 starts_var_decl 歧义） | **我**（或我审 starts_var_decl） | parser 歧义是真陷阱，错了会误判正常代码 |
| B-5 enum 跨模块 + 变体 | **我** | 变体解析 / match 穷尽性有微妙处 |
| B-6 综合 memcheck 测试 | **Sonnet** | 纯测试编写 |

> 建议顺序：B-2 → B-3（Sonnet，打基础）→ 回到我做 B-4 → B-5 → Sonnet 补 B-6。B-2/B-3 完成即可
> 中途验证（同名 struct 各自模块内已是不同类型），再由 B-4 放开 importer 歧义。

---

## 3. 测试与回归策略（贯穿所有 Step）

每个 Step 结束**必做**（沿用内存整改附录 B 精神）：
```
□ ctest --output-on-failure -C Release         → 全部 PASS（当前基线 74/74）
□ ls run <新样例>                               → JIT 正确
□ ls run --memcheck <新样例>                    → OK clean
□ ls compile <新样例> -o t.exe && t.exe         → AOT 正确
□ 现有 json/stdlib e2e（test_std_json 等）      → 无回归
```

**新增测试落点**（cmake 驱动，仿 `test_l009_mangle.cmake` / `test_l0091_modgen.cmake`）：
| 测试 | 覆盖 |
|------|------|
| `test_modvar`（Part 1） | 单模块全局、跨模块同名全局、has_drop 全局、可变累加 |
| `test_modtype_conflict`（B-1） | S2/S6 → 清晰编译错误 |
| `test_modtype_ns`（B-2~B-6） | 同名 struct/enum/方法跨模块各自正确 + 限定引用 + memcheck |

每个测试三重验证（JIT + AOT + memcheck），注册到 `CMakeLists.txt` 并设 `DEPENDS` 串到现有链尾。

---

## 4. 依赖与建议顺序

```
Part 1（全局变量，自包含）
  P1-1 → P1-2 → P1-3 → P1-4

Part 2（类型）
  B-1（safe，独立可交付，立即消除 footgun）
   └─► B-2 → B-3 → B-4 → B-5 → B-6（full，逐步开放同名共存）
```

- Part 1 与 Part 2 互相独立，可并行/任意先后。
- **最小安全交付**：Part 1 全部 + B-1。此后即使停工也无崩溃/静默损坏遗留。
- B-2 起改动类型身份，风险升高，务必每步全量回归 + IR 断言。

---

## 5. 风险登记

| 风险 | 缓解 |
|------|------|
| B-1 让"侥幸能跑"的同名类型代码开始报错 | 先全仓 grep 确认无 stdlib/测试依赖；报错信息给出修复指引 |
| B-2 前缀名污染错误信息（`type_name` 显示 `mod_a__Config`） | 单独小步：加展示名或打印时去前缀 |
| B-4 限定类型语法需 parser/checker 扩展 | 评估现有 `module.Type` 是否已可解析；否则最小扩展 |
| 前缀字符串定义端/调用端不一致（同 L-009 教训） | 统一走 `cg_module_fn_symbol`；每步用同名跨模块复现用例兜底 |
| 模块全局 scope 跨模块串味 | 每模块 Pass B 前后 push/pop scope，仅注册本模块全局 |
