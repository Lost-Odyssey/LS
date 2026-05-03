# LS 实现阶段详细规范

Phase 1-8 已完成。本文档为历史参考规范。

---

## Phase 1: Scanner ✅

**Token 类型**：`TOKEN_INT_LIT / FLOAT_LIT / STRING_LIT / CHAR_LIT / TRUE / FALSE / NIL`，关键字（`fn return if else while for in match struct impl module import load self break continue static extern`），类型关键字（`int i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool string void object array vec map`），运算符，界定符，`TOKEN_FSTRING_START / FSTRING_TEXT / FSTRING_END`，`TOKEN_EOF / TOKEN_ERROR`。

**Scanner 结构**：`{ source, start, current, line, column, start_column, in_fstring, fstring_brace_depth }`

**要点**：Token 只存 `(start, length)` 切片，不复制源码。f-string 用 `fstring_brace_depth` 追踪嵌套。关键字用哈希/trie 查找。

---

## Phase 2: AST + Parser ✅

**核心 AST 节点**：
- 字面量：`AST_INT_LIT / FLOAT_LIT / STRING_LIT / BOOL_LIT / NIL_LIT / FORMAT_STRING`
- 表达式：`AST_IDENT / UNARY / BINARY / CALL / INDEX / FIELD / CLOSURE / MATCH / CAST / RANGE / ARRAY_LIT`
- 语句：`AST_VAR_DECL / ASSIGN / RETURN / IF / WHILE / FOR(foreach) / FOR_C / BLOCK / EXPR_STMT / BREAK / CONTINUE`
- 声明：`AST_FN_DECL / STRUCT_DECL / IMPL_DECL / MODULE_DECL / IMPORT / EXTERN_FN`
- FFI：`AST_LOAD_LIB / FFI_CALL`

**Parser**：Pratt Parser，`ParseRule` 表（prefix_fn, infix_fn, precedence）。优先级：ASSIGNMENT < OR < AND < EQUALITY < COMPARISON < BITWISE < SHIFT < TERM < FACTOR < UNARY < CALL < PRIMARY。分号可选。

---

## Phase 3: 类型系统 ✅

**TypeKind**：`TYPE_INT / I8..I64 / U8..U64 / F32 / F64 / BOOL / STRING / VOID / NIL / POINTER / ARRAY / FUNCTION / STRUCT / OBJECT / MODULE / LIB / VEC / MAP`

**符号表**：作用域链 `Scope { symbols[], parent }`，`scope_define` / `scope_resolve`（沿链向上查找）。`Symbol` 含 `llvm_value`（codegen 回填）。

**检查规则**：不允许隐式类型转换（需显式 `as`）；`*T → object` 隐式允许，反向需 `as`；print 接受任意可打印类型；f-string 表达式整体类型为 string。

---

## Phase 4: LLVM IR 代码生成 ✅

**类型映射**：
```
int/i32→Int32, i8→Int8, i64→Int64, f32→Float, f64→Double, bool→Int1
string→%LsString{i8*,i32,i32}, object→ptr, *T→ptr, array(T,N)→[N x T]
vec(T)→%LsVec{ptr,i32,i32}, map(K,V)→%LsMap{ptr,i32,i32}
```

**关键策略**：
- 变量：`alloca` + `store`；引用：`load`
- print：intrinsic，按 `resolved_type` 展开为 `printf`
- string：静态字面量 cap=0，动态 cap>=16（`LS_MIN_STR_CAP`）
- RAII：`emit_scope_cleanup` 在 block 退出时 free owned strings
- 全局变量：Pass 1 创建 LLVM global，`__ls_global_init()` 初始化，main 入口注入调用
- FFI：`__ls_ffi_init()` 加载动态库，main 入口注入调用

---

## Phase 5: LLJIT 增量编译 ✅

**JitEngine**：`{ LLVMOrcLLJITRef, main_dylib, ts_context, fn_registry[] }`

**增量策略**：函数级粒度，AST 哈希变更检测，`jit_add_module` 注入，JITDylib 符号覆盖。

**REPL**：每行/块作为增量 Module 注入，维护全局状态。

---

## Phase 6: C FFI ✅

**两层架构**：
1. `ffi.h/c`：C 层跨平台封装（`ffi_load / ffi_unload / ffi_symbol`），供编译器内部用
2. Codegen 直接生成平台 API 调用（AOT 产物不依赖 ffi.c）

**语言层面**：
- `lib x = load("foo.dll")` → 全局句柄 + `__ls_ffi_init()`
- `extern fn name(params) -> ret from lib` → 类型检查 + LLVM 外部函数声明
- `lib.call("name", args...)` → 运行时 GetProcAddress/dlsym + 函数指针调用（unsafe）

**平台 API**：Windows: `LoadLibraryA / GetProcAddress / FreeLibrary`；Linux: `dlopen / dlsym / dlclose`

---

## Phase 7: 模块系统 ✅

**规则**：`module name` 声明模块名；`import path` 映射到文件系统（`math` → `./math.ls`）；循环导入检测（导入栈）；所有顶层符号默认公开。

**跨模块变量**：`module.var` 语法，导入模块的全局变量 forward-declare 为 LLVM external global，在 `__ls_global_init()` 中统一初始化（先导入模块，后主模块）。

**JIT**：`jit_run_file` 传递 `ModuleRegistry`，模块系统在 JIT 模式下可用。

---

## Phase 8: enum (Tagged Union) + Option/Result + match 穷尽性 ✅

**新增 Token / AST**：`TOKEN_ENUM`；`AST_ENUM_DECL { name, variants[] }` —— 每个 variant 含 `name` + `payload_types[]` + `payload_names[]` + `payload_count`。变体 ctor 与 pattern 复用 `AST_CALL` / `AST_IDENT`，由 checker 阶段识别。

**新增类型**：`TYPE_ENUM { name, variants[], has_drop, drop_fn }`；nominal 等价按 mangled name（`Option(int)` ≠ `Option(string)`）。

**Parser**：`parse_enum_decl` 镜像 `parse_struct_decl`；variant 之间 `;` / `,` / 换行任一可省。`parse_type` 在 IDENT 后可消费 `(args)`，支持 `Option(int)` 语法；`starts_var_decl` 启发式扩展为 `IDENT(...) IDENT [=|;]` 才算 var decl，避免误判 `print(a) print(b)`。

**Checker**：
- `check_enum_decl`：构建 TYPE_ENUM、resolve payload 类型、自动 has_drop、注册到 `enum_types`；自递归 payload（payload type == 自身）单独标记 has_drop
- `find_variant`：全局变体查找含上下文偏好（`expected_type` 是 enum 时优先匹配该 enum）
- `check_variant_ctor`：校验 ctor 参数数量与类型
- `AST_IDENT` / `AST_CALL` 早路径识别变体名 → 旁路函数解析直接产 TYPE_ENUM
- `AST_MATCH` enum subject 分支：variant pattern 识别、binder 推入新 scope 按 payload type 绑定、bitmap 跟踪 variant 覆盖、强制穷尽性（缺失 variant 列表清单报错）
- 内建模板注册表：Option(T) / Result(T,E) 在 checker 启动时注册，`resolve_type_node` IDENT-with-args 缺找时调 `instantiate_template` 单态化并缓存

**Codegen**：
- LLVM 布局：`%EnumName = type { i8 disc, [N x i8] payload }`，N 取所有 variant payload struct 的最大 ABI 大小（`LLVMABISizeOfType`）
- ctor (`emit_enum_ctor`)：alloca + memset 清零 + store disc + 通过 bitcast variant struct 写 payload；string 字段 `emit_string_clone_val` 深拷；自递归字段 malloc + store + 存 box 指针
- match codegen：`LLVMBuildSwitch` on disc，每 variant 一个 case block；binder 通过 `LLVMBuildStructGEP2` + bitcast 提取，alloca + load 后绑定到当前 scope（`is_borrowed=true`，scope 清理跳过）；自递归 binder 先 load box 再 load enum 值
- drop fn（`emit_auto_enum_drop_fn`）：has_drop enum 自动生成 `EnumName.__drop(EnumName *self)`，switch on disc，每 variant 释放 owned 字段（string→`emit_string_free`、struct→`emit_struct_drop`、自递归→递归 drop + free box）；scope 清理在 `emit_scope_cleanup` 中按 has_drop 触发
- 模板实例化的 enum 在 `type_to_llvm` 首次访问时懒构建 LLVM 类型 + drop fn（无 AST 节点路径）

**测试**：`test_codegen.c` 增 4 个 enum 单元测试；`tests/samples/` 增 5 个端到端文件（`enum_basic_test.ls` / `enum_payload_test.ls` / `enum_string_test.ls` / `enum_recursive_test.ls` / `option_result_test.ls`），AOT + JIT 双跑结果一致。

---

## Phase 指令模板（供参考）

```
Phase N: 请根据 docs/phases.md 的 Phase N 要求，实现 [组件]。
确保 cmake build 零警告，ctest 全部通过，ASan 无报错。
```
