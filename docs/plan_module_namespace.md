# 模块命名空间收尾：全局变量 + struct/enum 类型

> 状态：Part 1 全部完成 ✅ + B-1 完成 ✅ ｜ 关联：L-009 / L-009.1（`docs/plan_l009_mangling.md`）
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

#### Step B-2（B-full 起点）：struct/enum Type 名烘焙模块前缀
- 在**模块的递归 checker**里（`Checker.module_name != NULL`），struct/enum 类型注册/创建时把 `Type.as.strukt.name`（及 enum 名）设为 `<mod>__Name`。
  - 影响面：`type_name()`（错误信息会显示前缀——可加一个"展示名"字段或在打印时去前缀，单独小步处理）。
  - 同一 Type 对象被模块自身 AST 共享 → 模块函数签名、codegen 自然用前缀名。
- importer 注册（`checker.c:7902/7913`）：把前缀类型同时以**裸名**与**前缀名**登记（裸名用于无歧义时的无限定引用；见 B-4 歧义处理）。
- **验证**：单模块 struct 仍正常；模块 struct 的 LLVM 类型变为 `%mod_a__Config`。
- **新测**：dump IR 断言模块 struct 类型名带前缀；功能行为不变。

#### Step B-3：codegen 方法 / drop / clone 名跟随前缀
- `codegen_impl_decl`（`codegen.c:16001`）：`impl_decl.name` 在 `current_emit_module != NULL` 时前缀化，使方法 qualified name 变 `mod_a__Config.method`。
- 确认调用点（`codegen.c:10518` 静态方法 / 实例方法路径）用的是 struct **Type 名**（B-2 已前缀）→ 自动匹配。
- drop/clone 合成函数名（跟随 struct 名）随之前缀化——审查 `emit_*_drop` / `emit_*_clone` 命名点。
- **验证**：同名 struct + 方法跨模块（S3 场景）现在能编译且各自正确。
- **新测**：两模块各 `struct Widget` + `describe()` 不同返回值 → 各自正确（S3 转为通过）。

#### Step B-4：导入方裸名类型引用的歧义处理（语言设计点）
- 规则：裸名 `Config` 在**仅一个来源**定义时正常解析；**2+ 来源**时报歧义错误，提示用限定写法。
- 限定类型语法 `mod_a.Config`：检查 parser/`resolve_type_node` 是否支持 `<module>.<Type>` 作为类型；若不支持需小幅扩展（parser 类型位置允许 `ident.ident`；checker `resolve_type_node` 走 module 导出表查类型）。
- **验证**：S6 用 `mod_a.Box` / `mod_b.Box` 限定后各自正确；不限定 → 歧义错误。
- **新测**：限定类型引用样例（两模块同名 struct 都用，均正确）；不限定 → 期望错误。

#### Step B-5：enum 变体跨模块
- 同名 enum 跨模块（变体名也可能撞）：变体构造/`match` 解析按前缀 enum 名走；审查 `find_enum_llvm` / 变体 ctor / match 穷尽性。
- **验证**：两模块各 `enum Status { Ok, Err }` 不同语义 → 各自正确。
- **新测**：同名 enum 跨模块 + match，JIT+AOT+memcheck。

#### Step B-6：has_drop 同名 struct/enum 跨模块 memcheck
- 综合压测：两模块同名 has_drop struct（string 字段）+ vec/map 容器，跨模块传值/返回；memcheck 0 leak/0 dfree。
- **新测**：综合 memcheck 用例。

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
