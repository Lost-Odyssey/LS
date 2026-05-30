# L-009 实现说明书：跨模块函数名 LLVM Name Mangling

> 状态：✅ 已完成（2026-05-29）｜ 实际工期：~半天 ｜ 风险：低 ｜ 价值：★★★★★
> 关联：`docs/feature_inventory.md` L-009 ／ ★★★★★ 表
> 验证基线：2026-05-29（本文所有「实测」均基于 `build/Release/ls.exe`）
>
> **实现摘要**：模块内自由函数 LLVM 符号前缀化为 `<modpath>__<fn>`（根/主文件函数不变）。
> 单一权威 mangle 函数 `cg_module_fn_symbol`（`src/codegen.c`）；定义端在 codegen_compile
> 两个模块 Pass 经 `ctx->current_emit_module` 驱动（含 `codegen_fn_decl` 内自由函数符号），
> 调用端「先查 mangled 后回退裸名」（裸名调用 + 模块限定调用）。新增 `test_l009_mangle`
> （AOT+JIT+memcheck）通过；`ctest` 全绿 73/73（期间另发现并修复 BF-040 array `__drop`
> 双触发回归 + 同步 BF-039 的 map.set clone 语义测试期望，详见 `bugfix_registry.md`）。
> **§6 的 struct 方法 / 泛型仍为 L-009.1。**

---

## 1. 问题陈述

LS 当前把**函数的源码名直接当作 LLVM 符号名**。当两个函数同名时，LLVM 模块内符号冲突，产生两类后果（均已实测）：

### 后果 ① 用户本地函数 + import 同名函数 → IR verification 崩溃

```ls
import io                       // io 内部有 fn read_file(...) -> Result(string,string)
fn read_file(string s) -> string { return s }   // 用户本地同名
fn main() -> int { print(read_file("x")); return 0 }
```

实测输出：

```
Function return type does not match operand type of return inst!
  ret %LsString %s143
 %"Result(string,string)" = type { i8, [16 x i8] }
[codegen] module verification failed
```

两个 `@read_file` 定义合并，签名不一致直接挂掉。

### 后果 ② 两个模块定义同名函数 → 静默返回错值（最危险）

```ls
// mod_a.ls:  module mod_a   fn helper() -> int { return 1 }
// mod_b.ls:  module mod_b   fn helper() -> int { return 2 }
import mod_a
import mod_b
fn main() -> int { print(f"a={mod_a.helper()} b={mod_b.helper()}"); return 0 }
```

实测输出：`a=1 b=1` —— 两个调用都打到了 `mod_a.helper`。**不报错、不崩溃，直接算错。**

### 当前为何 json/stdlib 仍可用

现有 stdlib（`std/json.ls`、`std/io.ls`）靠**命名约定**规避冲突：私有 helper 一律加 `_` 前缀、公开函数互不重名、`json` 直接复用 `io.read_file` 而不自己定义。因此 `json_e2e_test.ls` / `json_file_io_test.ls` 实测全部 memcheck clean。**L-009 不是为了"解锁 json"（已可用），而是消除这一脆弱约定、根除后果②的静默错值。**

---

## 2. 根因定位（精确到代码站点）

| 角色 | 文件:行 | 现状 |
|------|---------|------|
| 模块函数**前置声明** | `src/codegen.c:18985-19000`（Pass A） | `LLVMAddFunction(module, decl->as.fn_decl.name, …)` 直接用源码名；`LLVMGetNamedFunction` 守卫导致第二个同名 decl 被跳过 |
| 模块函数**函数体** | `src/codegen.c:19024-19030`（Pass B）→ `codegen_fn_decl` | `codegen_fn_decl` 用 `node->as.fn_decl.name`（`codegen.c:14811, 14842-14845`） |
| 主文件函数 | 同 `codegen_fn_decl` | 同样用源码名（根模块，建议保持不 mangle） |
| **模块限定调用** `mod.fn()` | `src/codegen.c:10591-10630` | 剥掉模块名，只查 `field`（裸名）→ 命中第一个同名定义 |
| **裸名调用** `fn()` | `src/codegen.c:10536-10538` | `fn_name = ident.name`，查裸名 |
| FFI / extern fn | `src/codegen.c:16184` | 必须保持 C 符号名，**不可 mangle** |
| struct 方法 | `src/codegen.c:15898, 16033` | 已 mangle 为 `Struct.method`（先例，但未含模块前缀，见 §6 遗留） |
| 泛型实例 | `src/codegen.c:19394, 10543` | 已有独立 mangle 方案（`Pair(int,string).get_first`），同样未含模块前缀（见 §6） |

模块如何进入 codegen：`codegen_compile`（`codegen.c:18920`）遍历 `registry->modules[m]`，分 Pass A（前置声明）/ Pass B（函数体）两轮。每个模块名即 `registry->modules[m].name`（见 `src/module.h:11`）。

---

## 3. 设计

### 3.1 Mangling 方案

- **只 mangle 定义在 `module X` 文件里的顶层函数。** 根/主文件函数保持原名不变（最小改动、`main` 与现有主文件行为零影响）。
- 规范符号名：`<modpath>__<fn>`，其中 `<modpath>` 把模块路径里的 `.` 替换为 `_`。
  - 例：`io` 的 `read_file` → `io__read_file`；`std.json` 的 `parse` → `std_json__parse`。
- 选 `__`（双下划线）而非 `.`，以区别于 struct 方法已用的 `Struct.method` 命名空间。

> **铁律**：定义端与所有调用端必须用**同一个** mangle 函数产生**完全一致**的字符串。新增单一权威函数：
> ```c
> /* codegen.c —— 唯一权威 mangle 入口 */
> void cg_module_fn_symbol(char *out, size_t cap, const char *module_path, const char *fn);
> /* module_path == NULL 或 "" → 直接拷贝 fn（根模块，不 mangle） */
> ```

### 3.2 不 mangle 的清单（必须豁免）

1. 主/根文件的所有函数（含 `main`）。
2. `extern fn`（FFI，须匹配动态库 C 符号）。
3. 内建：`printf` / `malloc` / `free` / `ls_*` 运行时 / memcheck 包装等。
4. `math` / `perf` 等 builtin 模块调用——在 `codegen.c:10603-10618` 提前 `return`，本就不走符号查找，无需改动。

### 3.3 调用端解析策略（关键：增量安全）

采用「**先查 mangled，回退裸名**」，避免对 checker 做深度改造，并天然兼容 JIT/REPL 增量编译：

- **模块限定调用** `alias.fn()`（`codegen.c:10591`）：
  目标模块名已知（`field_access.object->resolved_type->as.module.name`）。
  先查 `cg_module_fn_symbol(mod_name, fn)`，命中即用；未命中回退裸 `fn`（兼容 builtin/未来情况）。
- **裸名调用** `fn()`（`codegen.c:10536`）：
  在 codegen 增设 `ctx->current_emit_module`（emit 某模块的 Pass A/B 期间置为该模块名，根文件期间为 `NULL`）。
  若非空：先查 `<current_emit_module>__fn`，命中即用；否则回退裸 `fn`（覆盖 builtin / 运行时 / 根模块函数）。

### 3.4 定义端改造

在 `codegen.c` 的两个模块 Pass 循环里设置 `ctx->current_emit_module = registry->modules[m].name`，并在该模块内：

- Pass A（`18991-18994`）：用 `cg_module_fn_symbol` 生成符号名再 `LLVMAddFunction`（保留 `LLVMInternalLinkage`）。
- Pass B（`19027-19030`）：沿用现有「临时改名」范式（参考泛型 `19413-19416`、struct 方法 `15899`）——临时把 `node->as.fn_decl.name` 换成 mangled 名，调 `codegen_fn_decl`，再换回。
- 根文件 Pass（`19048+`）：`ctx->current_emit_module = NULL`，行为不变。

---

## 4. 实施步骤（建议提交粒度）

1. **Step 1 — 权威 mangle 函数**：在 `codegen.c` 新增 `cg_module_fn_symbol`，加 `ctx->current_emit_module` 字段（`codegen.h` 的 `CodegenContext`，初始化为 `NULL`）。
2. **Step 2 — 定义端**：改 Pass A（`18991`）与 Pass B（`19024`）循环，按 §3.4 mangle + 设/清 `current_emit_module`。此时调用端尚未改，预期**故意编译失败**（找不到符号），用于确认改动面。
3. **Step 3 — 模块限定调用**：改 `codegen.c:10623`，按 §3.3 先 mangled 后回退。
4. **Step 4 — 裸名调用**：改 `codegen.c:10582`（`callee = LLVMGetNamedFunction(ctx->module, fn_name)` 之前），按 §3.3 先 mangled 后回退。
5. **Step 5 — 回归 + 新测**：见 §5。
6. **Step 6 — 文档**：把 `feature_inventory.md` 的 L-009 与 ★★★★★ 行标记为完成，更新本文状态。

> JIT 路径（`src/jit.c`）与 AOT 走同一 `codegen_compile`，理论上一并生效；Step 5 必须含 `ls run`（JIT）与 `ls compile`（AOT）双验证。

---

## 5. 测试计划

### 5.1 新增 e2e（必须，三重验证 AOT + JIT + memcheck）

| 用例 | 期望 |
|------|------|
| `tests/samples/mangle_collision_modules.ls`：两模块同名 `helper`（后果②复现） | 输出 `a=1 b=2`（修复前为 `a=1 b=1`） |
| `tests/samples/mangle_local_vs_import.ls`：本地 `read_file` + `import io`（后果①复现） | 编译通过，本地版被调用，无 verification 错误 |
| `tests/samples/mangle_transitive.ls`：A import B，B 与 A 有同名 helper | 各自调用正确，memcheck clean |
| 已有 `json_e2e_test.ls` / `json_file_io_test.ls` | 保持 PASS + memcheck clean（防回归） |

### 5.2 回归

```powershell
cd build && ctest --output-on-failure -C Release      # 期望维持 54/54（或 +新增）
```

### 5.3 排查清单（沿用内存模型整改附录 B 精神）

```
□ ls run <新用例>            → JIT 正确
□ ls run --memcheck <新用例> → OK clean
□ ls compile <新用例> -o t.exe && t.exe → AOT 正确
□ ls run 现有 json e2e        → 无回归
□ ctest -C Release            → 全 PASS
```

---

## 6. L-009.1 后续阶段（部分已完成）

L-009 本期只覆盖**自由函数**。后续 L-009.1 跟进项的实测结论与进展：

### 6.A 模块泛型（✅ 已完成 2026-05-30）

**实测发现**：原以为只是"加前缀"，实际有两层 bug：
- **A1**：模块内泛型**连单模块都不工作** —— `checker.c` 递归检查模块时传 `out_gm=NULL`，模块触发的泛型实例化（`pending_generic_methods`）被丢弃 → 调用点报 `undefined function 'identity(int)'`。**修复**：import handler 用局部 `sub_gm` 收集并合并进当前 checker 的 pending 队列，逐层上浮到根→codegen；codegen 调用点对未声明的泛型实例**按需前置声明**（绕开 Pass B 早于 gm 声明的顺序问题）；body 发射加 dedup 守卫。
- **A2**：同名不同体泛型跨模块 → 都 mangle 成 `tag(int)` → **静默错值**（`a=1 b=1` 应 `a=1 b=2`）。**修复**：实例化符号按定义模块前缀化 `<mod>__tag(int)`。checker 经 `registry->current_check_module`→`Checker.module_name` 拿到模块名前缀化 `owned_mangled`；codegen 调用点用 `current_emit_module` 前缀化 `g2_mangled`；根模块（NULL）不前缀。
- 测试：`test_l0091_modgen`（AOT+JIT+memcheck）。

### 6.B struct 类型名模块命名空间（⬜ 待做 = L-009.1 剩余）

**实测发现**：struct 方法符号冲突（原 §6 #1）在**同 struct 名跨模块**时才发生，但 checker 在 impl_registry（按裸 struct 名索引，`find_or_create_impl`）阶段就先报 `conflicting method` —— 方法符号冲突被**提前屏蔽**，单独做 `mod__Foo.bar` 方法 mangling 够不到。真正会咬人的是 **struct 类型名本身不做模块命名空间**：
- 同名 + 相同布局 + 无方法 → 侥幸正常（靠布局相同）
- 同名 + 有方法 → checker 报 `conflicting method`
- **同名 + 不同布局 → codegen 崩溃**（`Invalid indices for GEP`，checker 放行、LLVM verify 失败）

修法（大改动）：checker 的类型注册 / impl_registry + codegen 的 struct/enum LLVM 类型注册 + 方法/drop/clone 符号全部按模块命名空间化。

### 6.C 类型名 / enum 变体

跨模块同名 enum 同理，随 6.B 一并审计。

---

## 7. 风险评估

- **低**：改动集中在 4 个明确站点 + 1 个新函数，无类型系统/所有权改动。
- 「先 mangled 后回退」策略对未覆盖路径（builtin、根模块）天然安全，不会引入新崩溃。
- 主要风险点是**定义端与调用端 mangle 字符串不一致**（漏改某个站点）——由 §5.1 后果①②的直接复现用例兜底捕获。
