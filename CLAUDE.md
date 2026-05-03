# LS (LLVM Script) — Claude Code 项目上下文

## 0. 重要交互规则

- **永远用中文回答用户**，无论用户用什么语言提问。

---

## 1. 项目概述

用 C 语言实现通用编程语言 **LS** (LLVM Script)，C + Ruby 混合风格，LLVM 18 后端，支持 AOT 编译和 JIT 增量编译。

### 1.1 核心设计决策（不可更改）

| 维度 | 决策 |
|------|------|
| 实现语言 | C17（MSVC on Windows，兼容 GCC/Clang） |
| 构建系统 | CMake >= 3.20 + Visual Studio 17 2022 (或 Ninja) |
| 目标平台 | Windows 10 x64（首要），Linux/macOS（次要） |
| 后端 | LLVM 18 C API |
| LLVM 链接 | **静态链接**（ls.exe 独立可执行，不依赖 LLVM DLL） |
| 类型系统 | 静态类型 + 显式标注 |
| 内存管理 | 手动 malloc/free，无 GC |
| JIT 引擎 | LLJIT（后期迁移 ORC v2） |
| C FFI | 动态加载 shared library（Windows: LoadLibrary/GetProcAddress） |
| 外部依赖 | 仅 LLVM |

### 1.2 已实现语言特性

- 变量：C 风格类型前置 `int x = 42`，分号**可选**
- 函数：`fn add(int a, int b) -> int { ... }`，无返回值省略 `-> type`
- `struct` + `impl` 块（旧式隐式 `self` / 显式 `&self` / `&!self`，`static fn` 静态方法）
- `match` 模式匹配（含 enum 变体解构 `Some(v)` / `Node(v,l,r)` + 强制穷尽性检查）
- `enum`（Phase 8，tagged union）：`enum Color { Red Green Blue RGB(int r, int g, int b) }`，支持 payload 变体、自递归（`Tree { Leaf  Node(int, Tree, Tree) }` 编译器自动 box）、has_drop enum 自动生成 drop 函数；分隔符 `;` / `,` / 换行任一可省
- 内建 `Option(T)` / `Result(T,E)`（Phase 8，按需单态化模板）：`Option(int) o = Some(42)` / `Result(int,string) r = Err("msg")`，构造表达式按上下文类型自动消歧
- `for (init; cond; update)` C 风格循环，`for i in 0..10` range 迭代
- `string` = `LsString { i8* data, i32 len, i32 cap }`，RAII 自动释放
  - 方法：`empty`, `at`, `find`, `rfind`, `count`, `contains`, `starts_with`, `ends_with`, `compare`, `upper`, `lower`, `substr`, `trim`, `replace`, `append`, `split`, `join`, `copy`
- 借用：`&string` / `&!string`（Phase 5 / 5.5），`&vec(T)` / `&!vec(T)`（Phase 5.6），`&map(K,V)` / `&!map(K,V)`（Phase 5.7），`&struct` / `&!struct`（Phase 5.8，含 string/vec/map/drop 字段亦可），`&self` / `&!self` 方法（Phase A1，含 string/vec/map/drop 字段亦可）
  - ABI：`&string` by-value（cap=0 标记），其他 `&T` / `&!T` 全部 pointer
  - 只读借用支持 auto-borrow（`&T ← T` 隐式）；可写借用必须显式 `&!x`
  - 两者均禁止转移所有权（`vec.push` / `__move` / copy-out 全部拒绝）；可写借用额外允许 `=` / `+=` / `.append`、vec/map mutating 方法（`push/pop/clear/set/remove/...`）、`v[i] = x`、struct 字段写 `s.field = x`
  - 支持类型：`string` / `vec(T)` / `map(K,V)` / `struct`（含 has_drop 字段亦可）；仅用于函数参数位置
  - struct 实例方法可显式声明 `fn m(&self, ...)` / `fn m(&!self, ...)`：&self 禁写 self.field，可被 owned/&Struct/&!Struct 调用；&!self 允许 self.field 写，可被 owned/&!Struct 调用；旧式 `fn m()` 形式保留但无法在借用对象上调用
- `array(T, N)` 固定大小数组（索引读写、`.length`、for-in 迭代）
- `vec(T)` 内置动态数组（`push`, `pop`, `get`, `set`, `len`, `cap`, `is_empty`），支持 `vec(T) v = [..]` 字面量（含空 `[]`）
- `map(K, V)` 内置哈希映射（`set`, `get`, `contains_key`, `remove`, `clear`, `keys`, `values`）
- `print()` 内建 intrinsic（任意可打印类型），`f"text {expr}"` 格式化字符串
- `object` 类型擦除指针（`void*`）
- `module` / `import` 模块系统（含跨模块全局变量）
- C FFI：`lib x = load("foo.dll")` + `extern fn` 声明 + `lib.call(...)` 动态调用
- 全局变量（顶层声明，跨函数读写，跨模块导出）
- 内建 stdlib 模块：`math`（详见 §6 已完成段落）。**保留名约定**：`math` / 未来的 `io` / `fs` / `string` 等是 stdlib 模块名；用户可创建同名 `.ls` 完全 shadow（部分覆盖会丢失内建符号），但建议避开以兼容未来 stdlib 扩展

> 完整语法示例见 [docs/syntax_guide.md](docs/syntax_guide.md)

---

## 2. 目录结构

```
src/
  main.c          入口：CLI、REPL、文件编译
  common.h        公共类型、宏（含 CG_DEBUG 开关）
  scanner.h/c     词法分析
  token.h         Token 枚举 + Token 结构体
  ast.h/c         AST 节点定义
  parser.h/c      Pratt Parser
  types.h/c       类型系统 + 类型检查器
  symtable.h      符号表（作用域链）
  codegen.h/c     AST → LLVM IR
  jit.h/c         LLJIT 封装（增量编译）
  ffi.h/c         C FFI：动态库加载
  module.h/c      模块系统
  debug.h/c       LLVM IR 打印、诊断

runtime/builtins.c    内建函数运行时
tests/test_*.c        单元测试
tests/samples/*.ls    端到端测试
docs/                 grammar.ebnf, types.md, phases.md, syntax_guide.md, build.md
```

---

## 3. 构建与运行

```powershell
# Windows (Ninja，推荐)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build

# Windows (Visual Studio)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build --config Release
```

```bash
# Linux/macOS
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

```
# 使用
ls.exe compile input.ls -o output.exe   # AOT 编译
ls.exe run input.ls                     # JIT 执行
ls.exe repl                             # REPL

# 测试
cd build && ctest --output-on-failure -C Release
```

> 完整 CMakeLists.txt 配置见 [docs/build.md](docs/build.md)

---

## 4. 实现阶段状态

| Phase | 内容 | 状态 |
|-------|------|------|
| 1 | Scanner + Token | ✅ |
| 2 | AST + Pratt Parser | ✅ |
| 3 | 类型系统 + 符号表 | ✅ |
| 4 | LLVM IR 代码生成 (AOT) | ✅ |
| 5 | LLJIT 增量编译 + REPL | ✅ |
| 6 | C FFI（动态库加载） | ✅ |
| 7 | 模块系统（module/import） | ✅ |
| 8 | enum (tagged union) + Option/Result + match 穷尽性 | ✅ |
| 8.5 | `try` 早返操作符 (Zig 风格关键字) | ✅ |
| 9 (math) | 内建 `math` 模块（LLVM intrinsic + libm 直 call） | ✅ |
| 9 (io v1) | 内建 `io` 模块：read_file/write_file/exists/open/close/read_all/write + OpenMode enum + File struct | ✅ |
| 9 (io v2) | seek/tell/size/rewind + append_file/remove + SeekFrom enum + binary-mode gate | ✅ |
| 9 (memcheck A) | LS 自带内存检查器：`ls run --memcheck` JIT 路径，泄漏/重复释放/无效释放报告 | ✅ |
| 9 (memcheck A.5) | 细粒度 site 标签：每处 alloc 带 kind (`string.upper` / `io.slurp` / ...) + LS 源码行/列号 | ✅ |
| 9 (memcheck B) | vec/map/struct/enum 路径全跟踪 + 修复 5 类真实内存 bug（try / 字段 clone / 递归 enum） | ✅ |

> 各 Phase 详细规范见 [docs/phases.md](docs/phases.md)

---

## 5. 编码规范（必须遵守）

### 5.1 命名规范

- 函数：`module_verb_noun`（如 `scanner_next_token`, `codegen_emit_binary`）
- 结构体：`PascalCase`（如 `AstNode`, `CodegenContext`）
- 枚举值：`UPPER_SNAKE_CASE`（如 `TOKEN_INT_LIT`, `AST_BINARY`）
- 宏：`UPPER_SNAKE_CASE`；局部变量：`snake_case`
- 指针星号靠近类型：`int *ptr`（不是 `int* ptr`）

### 5.2 内存管理

- 所有 `malloc` 必须检查返回值；谁分配谁释放
- AST 节点用 `ast_free` 递归释放；LLVM 对象用 `LLVMDispose*` 释放
- Debug 模式下 AddressSanitizer 跑所有测试

### 5.3 错误处理

- Scanner 错误 → `TOKEN_ERROR`（含行号列号）
- Parser 错误 → `had_error = true`，同步到语句边界后继续
- 类型错误一次性收集（最多 20 条），格式：`[error_type] file:line:col: message`

### 5.4 测试要求

```c
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } \
} while(0)
#define ASSERT_EQ(a, b)     ASSERT((a) == (b), #a " != " #b)
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp(a, b) == 0, #a " != " #b)
```

每个新特性必须有：单元测试（`test_*.c`）+ `.ls` 端到端测试（AOT + JIT 双验证）。

### 5.5 CG_DEBUG 内存跟踪（必须遵守）

`CG_DEBUG` 定义在 `src/common.h`（默认 `0`）。启用后（`-DCG_DEBUG=1`）在 LLVM IR 中注入运行时内存追踪 printf，输出格式：

```
[cg] str.clone  cap=32 len=5 ptr=0x...
[cg] str.free   cap=32 ptr=0x...
[cg] scope.drop  var=name  type=string
[cg] vec.grow   old_cap=4 new_cap=8
```

**规则**：
1. 每个新的自动内存分配/释放/clone 点必须加 `#if CG_DEBUG` 块
2. 使用 `cg_emit_debug_printf(ctx, fmt, args, nargs)` 接口（`CG_DEBUG=0` 时为 no-op，直接调用无需判断）
3. 不要在 `#if CG_DEBUG` 块外调用 `cg_emit_debug_printf`
4. `common.h` 是 `CG_DEBUG` 的唯一权威来源，不在其他文件重定义

---

## 6. 待实现特性

### 规划中（未实现）

- 内建 `io` v3+：`read_line` / `read_bytes(n)` / `flush` / 异步 I/O / 目录列举
- 内建 `io` 当前限制：`File` 不参与自动 drop（需手动 `io.close`）；`File` 当作普通 struct 可被多次 close（UB）；`io.read_file` / `io.read_all` 仍用 `ftell/fseek`（C `long`），Windows 上 ≤ 2GB；seek/tell/size 已升级到 `_fseeki64`/`_ftelli64` 走 i64
- 方法语法（`f.read_all()` / `f.close()`）—— 当前 io 仅支持 free function 风格
- 内建 stdlib 扩展：`fs` / `string` / `time` / `env` 模块（参考已上线的 `math` / `io` 范式：编译器内建 + 用户优先 shadow + libc 直 call）
- `vec.get` / `map.get` 改返回 `Option(T)`（breaking change，Phase 9）
- 闭包 codegen（AST 已定义，codegen 缺失）
- 用户自定义泛型（`struct LinkedList<T>`；Option/Result 模板可改写为通用泛型流水线）
- 借用作为返回类型 / 变量声明 / struct 字段（需引入生命期系统）
- enum 含 vec/map payload 的 drop（当前 string + struct + 自递归 box 已支持）
- enum binder 的 move 跟踪（当前为只读借用语义；自递归复杂复用场景待 Phase B）
- 正则表达式 builtin
- f16 半精度浮点数原生支持

### 已完成（近期）

- **Memcheck Phase C（AOT 集成）** — 2026-05-03
  - CMakeLists 新增 `add_library(ls_memcheck STATIC runtime/memcheck.c)`，与 `ls.exe` 同目录产出（`ARCHIVE_OUTPUT_DIRECTORY` 同时覆盖 single-config Ninja 与 multi-config VS 生成器）；`runtime/memcheck.c` 同时仍编进 `ls.exe` 自身，让 JIT 路径的 `LLVMOrcAbsoluteSymbols` 注册不变
  - `src/main.c` 新增 `get_executable_dir()`（Windows `GetModuleFileNameA` / macOS `_NSGetExecutablePath` / Linux `readlink("/proc/self/exe")` 三平台），`cmd_compile` 在 `--memcheck` 时把 `<libdir>/ls_memcheck.lib` 拼进 clang 链接命令；为避开 `<windows.h>` 中 `_TOKEN_INFORMATION_CLASS TokenType` 与 LS 自身 `TokenType` 枚举的命名冲突，对 `GetModuleFileNameA` 做单点 forward-declare 而不引入 `windows.h`
  - `runtime/memcheck.c` 的 `ls_mc_report` 开头加 `fflush(stdout)`：AOT 报告由 `atexit` 触发，stdout 缓冲必须先 flush，否则 stderr 上的报告会比程序自身输出先到终端
  - 新增 `tests/test_memcheck_aot.cmake`（cmake -P 驱动，跨平台），编译并运行 `memcheck_phase_a.ls` AOT 后断言 stderr 含 `[memcheck] OK clean` + `SUMMARY: 0 leak(s)`；注册为 ctest `test_memcheck_aot`，依赖 `test_memory`
  - 验证：`memcheck_phase_a.ls` / `memcheck_kinds.ls` / `memcheck_edge.ls` AOT 均 ✓ clean，与 JIT 报告字面一致；ctest 9/9 通过；无 `--memcheck` 时 AOT 路径零回退

- **Memcheck Phase B（vec/map/struct/enum 全跟踪 + 5 类真实 bug 修复）**
  - alloc/free 全路径替换：`emit_string_free` 系列 + vec/map/enum drop 全部走 `cg_emit_free` 带 kind 标签
  - 新增 `cg_install_memcheck_wrappers` 也拦截 `realloc` → `ls_mc_realloc`（vec.grow 路径）
  - 新增 `calloc` wrapper 防 map bucket 数组未跟踪
  - **Bug 1：enum.box 泄漏（12 leaks）** — 修复 `instantiate_enum_template` 中自递归 enum has_drop 循环依赖；emit_cleanup_to 加 TYPE_ENUM 处理；enum 构造器 box 后 memset 源 alloca（move 语义）
  - **Bug 2：try 早返路径 string 泄漏（5 leaks，4160 bytes）** — var_decl auto-clone 对 rvalue 函数返回值会丢失原 heap；修复：检测 `init_node->kind == AST_CALL || AST_TRY` 时跳过 clone（直接 store 转移所有权）
  - **Bug 3：struct 构造期间 double-free** — AST_NEW_EXPR 字段初始化时先 emit_string_clone_val 再 store
  - **Bug 4：calloc 未追踪** — 加 calloc wrapper
  - **Bug 5：sum_tree match 绑定 box double-free（34 dfree）** — 自递归 enum 函数参数（`fn sum_tree(Tree t)`）作 borrowed 处理（`psym->is_borrowed = true`，scope cleanup 跳过）；match.subj 自动 drop 在 subject 是 self-recursive 类型时跳过；subject ident 是 borrowed sym 时也跳过
  - **加分：struct 字段读 clone temp 跟踪** — AST_FIELD 读 string 字段时 emit_string_clone_val 后 cg_push_temp_string，让下一个语句边界 flush；之前漏注册导致 print(p.name) 泄漏
  - 实测：`memcheck_edge.ls`（vec/map/struct/enum/try/break/recursive enum 极端场景）从 19 leaks + 1 dfree + 1 ifree 全部归零；`io_basic_test.ls` / `io_seek_test.ls` / `memcheck_phase_a.ls` / `memcheck_kinds.ls` 全 ✓ clean；ctest 8/8 仍通过

- **Memcheck Phase A.5（细粒度 site 标签）**
  - `cg_emit_alloc(ctx, size, kind, line, col)` 提升为 codegen.h 公开 API；现在内置 stdlib 也能用（io 已用上）
  - 替换 7 个高 ROI alloc 路径，从默认 `(unknown):0:0` 升级为有意义的 kind + 实际 LS 行/列：
    - `string.copy` / `string.upper` / `string.lower` / `string.substr` / `string.trim` —— `codegen_string_method` 内的 5 处 malloc
    - `string.concat` —— 二元 `+` 字符串拼接路径（`case AST_BINARY` TYPE_STRING 分支）
    - `string.fstring` —— `codegen_format_string` 的 4096 字节缓冲分配
    - `io.slurp` —— `builtins_io_cg.c::emit_slurp_file`，新增 line/col 参数从 args[0] 取
  - 已替换的路径若未来出 leak/double-free，报告里能看到形如：
    ```
    [memcheck] LEAK    16 bytes  tests/foo.ls:19:24  (io.slurp)
    ```
    精确到 LS 源码行号 + 操作类型，不需要在 IR 层 grep
  - IR 体积影响：每个独特 (kind, line, col) 共用一个 `LsMcSite` 全局；`tests/samples/memcheck_kinds.ls`（exercise 7 种 string kind）IR 里只有 7 个 site 全局，dedup 工作正常
  - 未替换的路径（vec.grow / map.node / enum.box / 用户 struct __drop / scope cleanup 自身）仍走 @malloc/@free 内部 wrapper，依然被跟踪但只标 `(unknown):0:0`。Phase B 接续替换
  - 测试：`tests/samples/memcheck_kinds.ls`（exercise 7 种 alloc kind，✓ clean）；`io_basic_test.ls` IR 里 `io.slurp` site 行号 19/27/49 与源码 read_file 调用位置一一对应；ctest 8/8 仍通过

- **LS 自带内存检查器 Phase A（`ls run --memcheck`）**
  - 实现 `runtime/memcheck.c`：open-addressing hash table (ptr → {size, alloc_site, free_site, freed})，`ls_mc_alloc` / `ls_mc_free` / `ls_mc_report`
  - 编译期注入：`cg_install_memcheck_wrappers` 在 LLVM 模块里**重命名 extern `malloc`/`free` 为 `ls_mc_real_*`**，再添加同名内部 wrapper（internal linkage）转发到 `ls_mc_alloc`/`ls_mc_free`，配上一个 default 站点。所有现有 codegen 调用点零修改即接入跟踪
  - JIT 符号绑定：`jit_init` 用 `LLVMOrcAbsoluteSymbols` 把 `ls_mc_alloc/free/report` 显式注册到 main dylib，绕过 Windows .exe 不导出符号给 GetProcAddress 的限制
  - 报告时机：在 `jit_run_file_memcheck` 里 `main_fn()` 返回后**显式调用** `ls_mc_report()`，必须在 `jit_destroy` 之前 —— 站点 globals 存在于 JIT 模块内存里，模块销毁后访问会段错；`g_reported` 标记保证 atexit 后续触发也不会重复打印
  - 输出示例：
    ```
    === LS memcheck report ===
    [memcheck] LEAK    16 bytes  tests/samples/io_basic_test.ls:0:0  (unknown)
    [memcheck] SUMMARY: 3 leak(s) (48 bytes), 0 double-free, 0 invalid free
    ```
    Phase A 用统一 default site，所有泄漏都标 `(unknown)` + 行号 0；Phase A.5 替换主要 alloc 路径（string.upper/concat/io.slurp/vec.grow 等）为 `cg_emit_alloc(..., kind, line, col)` 后能给出**LS 源码级**精确定位
  - **首次跑就抓到 5 个真实泄漏**（已修）：`io_basic_test.ls` 3 个 16-byte leak、`io_seek_test.ls` 2 个 64-byte leak —— 根因：`match io.read_file(...) { Ok(s) => ... }` 路径里 Ok binder 标记为 `is_borrowed=true`（move 跟踪是 Phase B 任务），但**临时 subject enum**（io.read_file 返回的 rvalue）退出 match 后没人 drop，payload 里 malloc 的 string buffer 就泄漏。修复：在 `case AST_MATCH` 的 enum 路径，merge_bb 之前判断 subject 是不是 scope 中已有 owned 变量；若不是（rvalue temp），调 `emit_enum_drop(ctx, subj_alloca, subj_type)`。`subj_node->kind == AST_IDENT` 且 `cg_scope_resolve` 找到 owned sym 时跳过（避免和 scope cleanup 重复 drop）。修复后 5 个泄漏全归零，ctest 8/8 仍通过
  - CLI：`ls run --memcheck file.ls`（JIT；AOT 待 Phase C 链接 `ls_memcheck.lib`）；`memcheck_enabled` 字段加在 CodegenContext 和 JitEngine
  - 跨子系统暴露：`checker.h` 已有的；`codegen.h` 暴露 `ls_string_*`；新增 `cg_make_site` / `cg_emit_alloc` / `cg_emit_free`（codegen.c 内部 static helper）
  - 测试：`tests/samples/memcheck_phase_a.ls`（4 个 string 用例，✓ clean）；ctest 8/8 仍通过（默认关闭，零开销）

- **内建 `io` 模块 v2（positioning + 路径操作）**
  - 新增 `SeekFrom` enum：`Start` / `Current` / `End`（disc 0/1/2 直对应 SEEK_SET/CUR/END）
  - 新增 6 个函数：
    - `io.seek(File, i64, SeekFrom) -> Result(i64, string)` —— 返回 seek 后的绝对位置（i64）
    - `io.tell(File) -> Result(i64, string)`
    - `io.size(File) -> Result(i64, string)` —— 不破坏当前位置：save → seek(end) → ftell → restore
    - `io.rewind(File) -> Result(int, string)` —— 等价 seek(0, Start)
    - `io.append_file(string, string) -> Result(int, string)` —— `fopen("ab")` 后 fwrite
    - `io.remove(string) -> Result(int, string)`
  - **二进制模式门**（binary-mode gate）：seek/tell/size/rewind 在 `is_binary == false` 或 `handle == NULL` 时立即返 Err `"io: file is text-mode or closed (positioning requires binary)"`，运行时检查 File.is_binary 字段。理由：Windows text-mode `\r\n` 翻译让 ftell 偏移与字节偏移失配
  - 64-bit positioning：升级到 `_fseeki64` / `_ftelli64`（Windows MSVC）/ `fseeko64` / `ftello64`（Linux glibc），符号在 `builtins_io_cg.c` 用 `#ifdef _WIN32` 选择；offset 全程 i64，不再受 long 32-bit 限制
  - 修复 v1 遗留 bug：`io.read_all` 之前总是 `fseek(0)` 再 `fread(total)`，会把已 seek 的位置重置；现在改为 saved=ftell → fseek(end) → total=ftell → sz=total-saved → fseek(saved) → fread(sz)，从当前位置读到 EOF
  - 类型扩展（`builtins_io.c`）：`make_simple_enum` 重构 OpenMode/SeekFrom 共用代码；新增 `Result(i64, string)` 模板实例化；隐式 widening：seek 的 offset 实参（int=i32）在 codegen `emit_seek` 自动 sext 到 i64
  - 测试：`tests/samples/io_seek_test.ls`（17 个用例 AOT + JIT，覆盖 size/tell/seek(Start)/seek(End)/seek(Current)/rewind/text-mode 拒绝/append_file/read_file 验证/remove/remove 不存在文件）

- **内建 `io` 模块 v1（编译器内置 stdlib 第二个模块）**
  - 用法：`import io` → `io.read_file(path)` / `io.open(path, ReadBinary)` / `io.close(f)` / ...
  - 类型设施：
    - `OpenMode` enum：`Read` / `Write` / `Append` / `ReadBinary` / `WriteBinary` / `AppendBinary`（按 disc → C mode 字符串映射）
    - `File` struct：`{ object handle, bool is_binary }`（v1 不自动 drop，用户必须 `io.close(f)`）
    - 由 io 模块在 import 时通过 `checker_register_struct` / `checker_register_enum` 注册到 checker registry，使 `File` 与变体名（`Read`、`WriteBinary` 等）在源码中可直接命名
  - v1 函数集（7 个）：
    - `io.read_file(string) -> Result(string, string)` —— 一次读全文件
    - `io.write_file(string, string) -> Result(int, string)` —— 一次写全文件，返回写入字节数
    - `io.exists(string) -> bool` —— `fopen("rb")` + 关闭探测
    - `io.open(string, OpenMode) -> Result(File, string)`
    - `io.close(File) -> int` —— 返回 fclose 状态码
    - `io.read_all(File) -> Result(string, string)` —— 在已开句柄上读全部
    - `io.write(File, string) -> Result(int, string)`
  - 实现路径：
    - 类型侧 (`src/builtins_io.c`)：`builtin_io_make_type(c)` 接收 Checker，构造 OpenMode/File，并通过 `checker_instantiate_result(c, T, string)` 拿到 `Result(string,string)` / `Result(int,string)` / `Result(File,string)` 三个实例化类型
    - 代码生成 (`src/builtins_io_cg.c`)：每个 io 函数 emit 一段直接调 libc 的 IR（`fopen` / `fread` / `fwrite` / `fclose` / `fseek` / `ftell` / `malloc` / `memset` / `memcpy`），按需在 IR 里 alloca Result enum 并直接写 disc + payload。`OpenMode` 通过模块级 `[6 x i8*]` 全局表把 disc → C mode 字符串
    - 零运行时 link：所有 libc 符号在 ls.exe（AOT 由 clang 链接）和 LLJIT（process symbol resolver）下都自然可解析；不需要 stdlib runtime archive
    - 跨编译单元复用：暴露 `checker_register_struct` / `checker_register_enum` / `checker_find_enum` / `checker_instantiate_result` / `checker_instantiate_option`（在 `src/checker.h`），暴露 `ls_string_type` / `ls_string_make` / `ls_string_from_literal`（在 `src/codegen.h`）
  - **用户优先 shadow 策略**：和 `math` 一致，若 importing 文件目录下存在 `io.ls`，使用用户版本；否则落到内建
  - 测试：`tests/samples/io_basic_test.ls`（10 个用例 AOT + JIT 双跑：write_file / exists / read_file roundtrip / open+read_all+close / open+write+close / 错误路径）

- **`math` 模块多态分派（abs / min / max int 版本）**
  - `math.abs(int) -> int` / `math.abs(i64) -> i64` / `math.abs(f64) -> f64` 按参数类型自动选 LLVM intrinsic：
    - 有符号整数 → `llvm.abs.iN(x, false)`（INT_MIN 不 poison）
    - 无符号整数 → 原值返回（abs 等同于 identity）
    - 浮点 → `llvm.fabs.f64`
  - `math.min(a,b)` / `math.max(a,b)` 同样多态：
    - 有符号整数 → `llvm.smin.iN` / `llvm.smax.iN`
    - 无符号整数 → `llvm.umin.iN` / `llvm.umax.iN`
    - 浮点 → `llvm.minnum/maxnum.f64`
  - 实现：
    - 类型检查器在 `case AST_CALL` 顶部拦截 `<built-in math>.<poly_fn>` 调用，按参数类型用 `type_numeric_common` 计算结果类型，重写 callee 的 `resolved_type` 为对应签名（让后续 widening 走对路径）
    - 守卫条件：`obj_node->kind == AST_IDENT` 且 `scope_resolve` 解析到 `is_builtin == true` 的 `TYPE_MODULE` —— 避免误触 `Point.origin()` 这类 struct 静态方法
    - codegen `builtin_math_emit_call` 看到 `MATH_POLY_INT_OR_FLOAT` 且全部参数是整数时走 `emit_int_poly_call`，按位宽拼 intrinsic 名（`llvm.abs.i32` / `llvm.smin.i64` 等）；混合 int+float 落回 f64 路径
  - 收益：`int diff = math.abs(-42)` 直接返回 int，无需 `as int` 强转，整数语义全程保持精度
  - 测试：`tests/samples/math_poly_test.ls`（15 个用例 AOT + JIT，含 int / i64 / 混合 int+i64 / 混合 int+float / 整数运算后参与 int 表达式）

- **数值类型隐式扩展（Zig 风规则）**
  - 原则：**目标类型能精确表达源类型的所有值** → 隐式扩展；否则必须 `as` 显式转换
  - 允许（隐式）：
    - `iN` → `iM`（M ≥ N，有符号→有符号）
    - `uN` → `uM`（M ≥ N，无符号→无符号）
    - `uN` → `iM`（M > N，无符号→更大有符号）
    - `i8/i16` → `f32`（≤ 24 位尾数）；`i8/i16/i32` → `f64`（≤ 53 位尾数）
    - `f32` → `f64`
    - `int`(=i32) → `i64` / `f64`（解决 `int` 默认 32 位与 i64 / f64 之间的累赘 cast）
  - 禁止（必须显式 `as`）：所有收窄、`f64`→`f32`、`f64/f32`→`int`、`i64`→`f64`（尾数溢出）、跨符号同宽（如 `int`↔`u32`）
  - 应用位置：
    - 变量初始化：`f64 a = 5` ✅
    - 赋值：`x = some_int`（x 是 f64）✅
    - 函数参数：`math.sqrt(int_var)` 自动提升为 f64 ✅
    - 返回值：`fn f() -> f64 { return 5 }` ✅
    - 二元算术 / 比较 / 位运算：`2.0 + 3` → f64；`int_var + i64_var` → i64 ✅
    - 内建 math 模块特殊路径（`builtins_math_cg.c`）也会做 sitofp/fpext widening
  - 实现：
    - `type_widens_to(src, dst)` / `type_numeric_common(a, b)` 在 `src/types.c`
    - `cg_widen()` 在 `src/codegen.c`，根据 from/to 类型 emit `sext` / `zext` / `sitofp` / `uitofp` / `fpext`
    - `ctx->current_fn_return_type` 新增字段，函数体编译期间记录 LS 返回类型供 return 语句 widening 使用
  - 测试：`tests/samples/numeric_widen_test.ls`（10 个用例 AOT + JIT）+ 4 个负向用例（收窄/同宽跨符号/i64→f64/float→int 全部正确编译错）

- **内建 `math` 模块（编译器内置 stdlib 起点）**
  - 用法：`import math` → `math.sqrt(x)` / `math.PI` / `math.atan2(y,x)` / ...
  - 函数集：`abs/min/max/floor/ceil/round/trunc/sqrt/pow/exp/exp2/log/log2/log10/sin/cos/tan/asin/acos/atan/atan2/sinh/cosh/tanh`（全部 f64）
  - 常量：`PI / E / TAU / INF / NAN`
  - 实现路径：
    - 类型侧 (`src/builtins_math.c`)：`builtin_module_make_type` 构造 TYPE_MODULE 含全部导出符号；类型检查器在 `import` 处发现内建名 + 无用户文件时落到这里
    - 代码生成 (`src/builtins_math_cg.c`)：INTRINSIC 项 emit `call @llvm.<name>.f64`（LLVM 后端自动 lower 成 `vsqrtsd` 等单条指令；无硬件指令时 fallback 到 libm）；LIBM 项直接 `call @<name>` 由 libm 提供
    - 零运行时开销：不走 FFI 的 `LoadLibrary`/`GetProcAddress`；JIT 由 LLJIT 经 dlsym 解析 libm 符号
  - **用户优先 shadow 策略**：若 importing 文件目录下存在同名 `math.ls`，使用用户版本（保持现有 `tests/samples/module_test/` 兼容）；否则落到内建。`module_user_file_exists` 在 `module.c` 提供
  - 测试：`tests/samples/math_basic_test.ls`（19 个用例 AOT + JIT 双跑）；`tests/samples/module_test/`（用户 shadow 路径）

- **Phase 8.5: `try` 早返操作符（Zig 风格前缀关键字）**
  - 语法：`int v = try fn_returning_result(...)` / `try fn_returning_option(...)`
  - 语义：成功路径 `Ok(v)`/`Some(v)` 解包出内层值；失败路径 `Err(e)`/`None` 早返当前函数
  - 检查器严格模式（无隐式转换）：
    - `try Result(T,E)` 要求当前函数返回 `Result(_, E)`，E 类型必须严格相等
    - `try Option(T)` 要求当前函数返回 `Option(_)`
    - 跨 Err 类型/Result↔Option 转换均编译错（需手写 `match` 显式处理）
  - Codegen：直接 IR 生成（switch on disc → 成功路径 GEP 提取 T；失败路径 memset+memcpy 构造返回 enum，触发 RAII cleanup 后 `LLVMBuildRet`）
  - 失败路径**字节级移动** Err payload，零额外 clone（heap 单一持有人贯穿调用栈）
  - 测试：`tests/samples/try_basic_test.ls`（AOT + JIT 双跑，含 chain/链式失败/Option 路径）

- **Phase 8: enum + Option/Result + match 穷尽性**
  - 用户 enum 含 payload variants（含命名字段 `RGB(int r, int g, int b)`）
  - 自递归 enum 自动堆 boxing（`Tree { Leaf  Node(int, Tree, Tree) }`）
  - has_drop enum 自动生成 `EnumName.__drop` + 作用域 RAII 清理
  - 内建 `Option(T)` / `Result(T,E)` 模板按需单态化（mangled name 缓存）
  - match 强制穷尽性检查（缺失 variant 列表清单报错）
  - 上下文驱动的 ctor 消歧（`Some(42)` 在 `Option(int)` 与 `Option(string)` 共存时按预期类型选择）
  - enum 体分隔符 `;` / `,` / 换行任一可省（与 struct 字段一致）
- Move 语义检查器（string Phase A/B，struct Phase A/B）
- `&string` 只读借用（Phase 5）
- `&!string` 可写借用（Phase 5.5，不引入 `mut` 关键字）
- `&vec(T)` / `&!vec(T)` 借用（Phase 5.6，pointer ABI）
- `&map(K,V)` / `&!map(K,V)` 借用（Phase 5.7，pointer ABI）
- `&struct` / `&!struct` 借用（Phase 5.8 POD → Phase B 解除限制，pointer ABI）
- `&self` / `&!self` 显式 self 借用方法（Phase A1 POD → Phase B 解除限制）：解锁 `&Struct`/`&!Struct` 上的方法调用
- String `.split()` / `.join()` / `.rfind()` / `.count()` 方法（runtime helper IR-emitted）

---

## 7. 所有权与 Move 语义（设计已固化，实现中）

### 7.1 LsString 内存三态

| cap 值 | 状态 | 含义 | 释放规则 |
|--------|------|------|----------|
| `== 0` | **Static** | data 指向 `.rodata` 全局常量 | 永不释放，永不 move |
| `> 0` | **Owned** | malloc 分配，当前变量持有所有权 | 退出作用域时 free |
| `== -1` | **Moved** | 所有权已转移 | 跳过 free |

### 7.2 核心规则：MAYBE_MOVED = MOVED = 死亡状态（不可更改）

变量一旦存在**任何执行路径**导致 move，即视为**死亡**，不可再使用或赋值。

```
状态转移（单调不可逆）：LIVE → MAYBE_MOVED → MOVED
```

```ls
string s = "hello".upper()
if cond { v.push(s) }   // s 处于 MAYBE_MOVED
print(s)                 // ❌ 编译错误（即使 else 分支未 move）
```

**理由**：状态单调递增，无需 CFG 不动点迭代；2-pass 循环分析足以处理所有控制流。

### 7.3 Move 触发条件（仅对 cap > 0 的 Owned string 生效）

| 操作 | 说明 |
|------|------|
| `vec.push(s)` | 所有权转入 vec，s 标记为 Moved |
| `map.set(k/v = s)` | 深拷贝到 map 节点，s 标记为 Moved |
| `string t = s` | 直接赋值转移所有权 |
| `fn_call(s)` | 函数参数传递（Phase A/B 实现） |

**Static string（cap==0）永不触发 move。**

### 7.3.1 借用（不转移所有权）

借用是"不移交所有权"的函数传参方式，形参结束时不释放源内存。借用变量**禁止**出现在 §7.3 的 move 触发位点。

| 借用形式 | ABI | 允许 | 禁止 |
|---|---|---|---|
| `&string`（只读） | by-value，cap 字段置 0 | 读 / `.upper` 等返回新 owned 的方法 | `=` / `+=` / `.append` / `vec.push` / `__move` |
| `&!string`（可写） | pointer（LsString\*） | 读 / `=` 重赋值 / `+=` / `.append` | `vec.push` / `__move` / `string t = s` copy-out |
| `&vec(T)`（只读） | pointer（LsVec\*） | 读（`v[i]` / `.length` / `.get` / `.is_empty` / `.first` / `.last` / `.contains` / `.index_of` / `.slice` / `.copy`） | `push` / `pop` / `clear` / `set` / `reserve` / `remove` / `truncate` / `swap` / `reverse` / `extend` / `insert` / `resize` / `sort` / `sort_by` / `shrink_to_fit` / `v[i] = x` / `v = new_vec` / copy-out |
| `&!vec(T)`（可写） | pointer（LsVec\*） | 读 + 所有 mutating 方法 + `v[i] = x` + `v = new_vec` | `__move` / `vec(T) t = v` copy-out |
| `&map(K,V)`（只读） | pointer（LsMap\*） | 读（`.length` / `.get` / `.contains_key` / `.keys` / `.values`） | `set` / `remove` / `clear` / copy-out |
| `&!map(K,V)`（可写） | pointer（LsMap\*） | 读 + 所有 mutating 方法（`set` / `remove` / `clear`） | `__move` / `map(K,V) t = m` copy-out |
| `&struct`（只读） | pointer（Struct\*） | 读字段（`s.field`，drop 字段返回 owned clone）；`&self` 方法调用 | `s.field = x` / `&!self` 方法调用 / copy-out |
| `&!struct`（可写） | pointer（Struct\*） | 读字段 + 字段写 `s.field = x`；`&self`/`&!self` 方法调用 | `__move` / `Struct t = s` copy-out |

调用点：
- 只读借用支持 auto-borrow（`f(my_owned)` 自动传递为 `&T`）
- 可写借用**必须显式** `f(&!my_owned)`；可向 `&T` 形参降级
- 同一 call 内同名变量不能同时出现 `&!x` 与其它借用（aliasing 禁止）
- 当前支持的借用类型：`string` / `vec(T)` / `map(K,V)` / `struct`（含 has_drop 字段亦可）；变量声明 / 返回类型 / struct 字段不支持

**ABI 策略**：`&string` 是 16 字节 POD 值传递特化（cap=0 标记阻止 callee free）；其他 `&T` / `&!T` 默认 pointer，callee 的 `sym->value` 直接作为 `LsT*` 使用，和 owned 形参路径共享同一份 codegen。

详细设计与测试矩阵见 [docs/move_semantics_plan.md](docs/move_semantics_plan.md) 第 11、12、13、14、15 节。

### 7.4 运行时保护（已实现）

- `mark_string_moved()` 仅对 `cap > 0` 生效，设 cap = -1
- `map.set` 若 key 已 moved（`cap < 0`），打印 warning 并 nop
- cap==0 时 `MAP_EMIT_COPY_KEY` 跳过深拷贝，直接存指针

### 7.5 Move 语义适用类型

| 类型 | 需要跟踪 | 说明 |
|------|----------|------|
| `string` | ✅ | cap>0 时有堆内存所有权 |
| `struct`（含 string 字段或自定义 `__drop`） | ✅ | Phase A/B 实现 |
| `vec(T)`, `map(K,V)` | ✅ | 含堆内存 |
| `int/f64/bool` | ❌ | 值语义，拷贝即可 |
| `*T`, `object` | ❌ | 裸指针，不做 RAII |

### 7.6 实现计划（优先级顺序）

String Phase A（顺序语句线性分析）→ String Phase B（if/else + 循环控制流）→ Struct Phase A/B → 借用语义

> 详细计划见 [docs/move_semantics_plan.md](docs/move_semantics_plan.md)
