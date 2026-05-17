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
| 9 (std.time) | 纯 LS 标准库 `std.time`：DateTime struct、now_local/now_utc、format/parse/iso8601、add/diff_s/duration_*、sleep_ms/sleep_us；C 后端 `runtime/os_win32.c`（`ls_os_time_*` / `ls_os_sleep_*`）；`ls_os_backend.lib` AOT 静态库；`import X as Y` 别名语法；codegen 两阶段（forward-declare + body）解决跨模块依赖排序；ctest `test_std_time` | ✅ |
| 10 (closure A) | type 别名 + `Block(...) -> R` 类型语法 + Ruby 风 `\|x\| body` 字面量 + trailing closure 糖 + 强制别名规则（return/struct field 拒绝裸 Block）；Phase A 仅 parse + check，codegen 在 Phase B/C | ✅ |
| 10 (closure B) | 无捕获闭包 codegen：lambda lifting (`__closure_N(env, ...)`) + LsBlock 16B 胖指针 + 间接 call；类型推导从 callee `Block(...)` 形参反推 `\|x\|` 字面量类型；trailing closure 单表达式自动 return | ✅ |
| 10 (closure C) | POD 捕获 + 堆 env + RAII：自由变量扫描 → AST `captures[]` → 合成匿名 env struct → `cg_emit_alloc("closure.env")` → body 入口 alloca+load 还原；闭包 var 离开作用域自动 free env（emit_scope_cleanup / emit_cleanup_to 双路径）；POD-only 限制（int/i64/f64/bool/char/*T/object）；嵌套闭包暂不支持 | ✅ |
| 10 (closure C.5) | string by-move 捕获：env 布局加 `drop_fn` slot（field 0），按 closure id 合成 `__env_drop_<N>`；外层 alloca 写 cap=-1 标 MOVED；Block 形参标 borrowed（callee 不再 free env，避免 double-free）；temp_block_envs 跟踪 rvalue 闭包字面量，var_decl/return 消费时 pop，否则 cg_flush_temps 在语句边界 free | ✅ |
| 10 (closure C.7) | vec(T) / map(K,V) / struct(has_drop) 捕获：vec/map 走 borrow 语义（env 不释放，body 标 is_borrowed，map 形参补 is_borrowed=true），struct 走完整 by-move（env 调 `Struct.__drop`，外层 moved_flag=true）；env_drop 按 capture kind 分派（string/vec inline，map/struct 复用现有 `__ls_map_drop` / `Struct.__drop`）；Block-call 返回 string 自动 `cg_push_temp_string`，避免 print 路径双注册（`expr_produces_dynamic_string` 对 Block 拒绝） | ✅ |
| 10 (closure E.1) | by-ref capture 重构：vec/map 捕获 env 存外层 alloca 指针（非值拷贝）；closure body 读写直达外层变量，捕获后 push/新增 key 对闭包可见；outer 变量不标 moved，可多次调用；修复 call site `arg_type` 未声明导致结构体克隆逻辑跳过的编译错 | ✅ |
| 10 (closure E.2) | 闭包参数 `\|v\|` 的 vec/map/Block 标 `is_borrowed=true`（与普通 fn 参数一致），防止 closure body scope cleanup 释放调用方持有的 heap → double-free | ✅ |
| 10 (closure E.4) | `array(POD,N)` by-value 捕获：checker 放行 POD-element 数组，codegen 走 by-copy 路径（env 字段类型 `[N x T]`，全量值拷贝，outer 不标 moved） | ✅ |
| 10 (closure F.1) | `[move v]` 语法：vec/map 显式 by-move 捕获，解决工厂模式悬垂（by-ref 默认改 by-move：env 字段存值而非指针，env_drop 调 drop helper，outer cap 字段置 0 防 scope 双释放） | ✅ |
| 10 (closure F.2) | Block 赋值 + 移动语义：`type_is_movable` 加 TYPE_BLOCK；fn/closure/method 的 Block 参数标 `is_borrow=true`；`F h = g` 后 g.env_ptr 置 NULL；`g = h` 先 drop 旧 env 再 null source；`return g` 正确 skip scope cleanup；memcheck 0 leak / 0 dfree | ✅ |
| 10 (closure F.3) | Block 作为 struct 字段：has_drop 扩展接受 TYPE_BLOCK；struct literal 构造时 pop temp_block_envs / null source env（env 转移给 struct）；emit_auto_drop_fn 按字段 emit env drop；checker 禁 `Block g = p.step1` aliasing 读；struct literal 字段 expected_type 传播让 closure 字面量正确推导类型；memcheck 0 leak / 0 dfree | ✅ |
| 10 (closure F.4) | vec(Block) / map(K, Block)：vec 元素为 Block 时 push 转移 env 所有权（null source），drop 调 emit_vec_elem_drop_at；map value 为 Block 时 set 转移 env，MAP_EMIT_FREE_VAL 扩展 val_is_block；checker push/set 传播 expected_type；直接下标调用 `vec[i](args)` / `map.get(k)(args)` 避免 env 别名（aliasing）；parser 修复 starts_var_decl 行号检查防止 `fn(x)` 后接多行被误判为 var_decl；F.4A checker 拒绝 `Block g = ns[i]` / `Block g = ops.get(k)` aliasing 读（报错并提示直接调用）；memcheck 0 leak / 0 dfree | ✅ |
| 10 (closure F.5) | enum capture + 完整 drop 模型：非 has_drop enum（disc-only/POD payload）by-copy 捕获（env 无额外 drop）；has_drop enum（含 string/struct payload）by-move 捕获（outer 标 moved_flag=1，body is_borrowed=true，env_drop 调 enum.__drop）；`emit_enum_drop_cond` 新函数（类似 `emit_struct_drop_cond`，按 moved_flag 有条件 drop）；`emit_auto_enum_drop_fn` 扩展支持 vec(T)/map(K,V)/嵌套 has_drop enum payload；`emit_scope_cleanup`/`emit_cleanup_to` enum 分支改用 `emit_enum_drop_cond`；修复 match codegen "Terminator found in middle of basic block" bug（match arm 含 `return` 语句时 `LLVMBuildBr` 前加终结符检查）；memcheck 0 leak / 0 dfree | ✅ |
| 10 (closure F.6) | CG_DEBUG 全面铺开：闭包 capture/move/borrow/drop 全操作路径结构化日志；四个 helper（`cg_dbg_capture`/`cg_dbg_outer_mark`/`cg_dbg_env_alloc`/`cg_dbg_block_op`）均 `#if CG_DEBUG` 包裹，`CG_DEBUG=0` 时零开销；注入点：`codegen_closure_literal` step 0（cap_outer_vals 加载区分 borrow/move-expl/move/copy）、step 9b（env.alloc + outer.mark cap=-1/moved_flag=1/vec.cap=0）；`cg_emit_block_env_drop`（env.drop）；`cg_null_block_env`（block.move src→null）；`emit_scope_cleanup`/`emit_cleanup_to` TYPE_BLOCK 分支（block.drop var='name'）；`emit_auto_drop_fn` TYPE_BLOCK 字段（field.drop）；`emit_vec_elem_drop_at` TYPE_BLOCK（elem.drop）；CMake 选项 `-DLS_CG_DEBUG=ON`（非 `-DCG_DEBUG=1`）；ctest 29/29 | ✅ |
| 10 (closure F.7) | Memcheck 压力测试（1000 次迭代，S1–S6 六种捕获模式）+ 两个真实 bug 修复：(1) `emit_enum_clone_val` 新函数：has_drop enum 函数参数按值传递时深拷贝 string/struct/enum payload，防双释放（与 struct 的 `emit_struct_clone_val` 对称）；AST_CALL argument clone 加 `TYPE_ENUM && has_drop` 分支；(2) enum match arm string binder 独立所有权：`Some(s) => { return s }` 场景下 binder `s` 现在克隆 payload 字符串（`emit_string_clone_val`），不再与 enum 共享数据指针，消除 `return` 后 env_drop + caller scope cleanup 双 free；ctest 30/30 | ✅ |

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
- 内建 stdlib 扩展：`fs` / `string` / `env` 模块（参考已上线的 `math` / `io` / `std.time` 范式）
- `vec.get` / `map.get` 改返回 `Option(T)`（breaking change，Phase 9）
- 闭包 codegen（AST 已定义，codegen 缺失）
- 用户自定义泛型（`struct LinkedList<T>`；Option/Result 模板可改写为通用泛型流水线）
- 借用作为返回类型 / 变量声明 / struct 字段（需引入生命期系统）
- enum 含 vec/map payload 的 drop（当前 string + struct + 自递归 box 已支持）
- enum binder 的 move 跟踪（当前为只读借用语义；自递归复杂复用场景待 Phase B）
- **Phase G（可选）：Block env 深拷贝**（`Block g = ns[i]` / `Block g = ops.get(k)` 当前 checker 拒绝）；需合成 `__env_clone_N`，env 增加 clone_fn slot；仅支持 POD + string capture；详细设计见 [`docs/block_clone_plan.md`](docs/block_clone_plan.md)
- **Phase H（独立）：struct 深拷贝**（`MyStruct b = vec_of_struct[i]` 含 has_drop 时 double-free，与 Phase G 共用 clone 基础设施）
- 正则表达式 builtin
- f16 半精度浮点数原生支持

### 已完成（近期）

- **`std.time` 标准库模块（纯 LS 实现 + C 后端 + AOT 静态库）** — 2026-05-17
  - **架构**：纯 LS 层 `std/time.ls`（导入 `std.os as _os`）+ C 后端 `runtime/os_win32.c` / `runtime/os_posix.c`（`ls_os_time_*` / `ls_os_sleep_*`）；遵循"纯 LS stdlib + platform C 实现"的范式
  - **DateTime struct**：`year / month / day / hour / minute / second / weekday / yday / utcoff / unix_s(i64)` 10 个字段
  - **函数集（全部通过 `import std.time as T` 使用）**：
    - 当前时间：`now_unix_ns() -> i64` / `now_unix_ms() -> i64` / `now_unix_s() -> f64` / `now_local() -> DateTime` / `now_utc() -> DateTime`
    - 格式化：`format(dt, fmt) -> string`（strftime 风格）/ `iso8601(dt) -> string`（自动处理 UTC offset）
    - 解析：`parse(text, fmt) -> Result(DateTime, string)`
    - 算术：`duration_ns/us/ms/s(i64) -> i64`（返回纳秒）/ `add(DateTime, i64) -> DateTime` / `diff_s(dt1, dt2) -> i64`
    - 睡眠：`sleep_ms(i64)` / `sleep_us(i64)`
  - **`import X as Y` 别名语法**：parser 在点号路径后检测 `TOKEN_AS` → 解析别名；AST `import_decl.alias` 字段；checker `scope_define` 用别名（或原 path）绑定模块；对 `std/time.ls` 的内部 `import std.os as _os` 和用户的 `import std.time as T` 都生效
  - **codegen 两阶段（修复跨模块依赖排序 bug）**：旧版单 pass 在 `std.time`（m=0）生成函数体时 `std.os`（m=1）尚未 forward-declare → `[codegen] undefined function 'raw_time_get_year' in module`；新版 Pass A 对所有模块做 forward-declare（struct / extern / fn 签名 / 全局变量），Pass B 再生成所有函数体
  - **AOT 静态库**：CMakeLists 新增 `add_library(ls_os_backend STATIC ${LS_OS_SOURCE})`，ARCHIVE_OUTPUT_DIRECTORY 同 ls_memcheck/ls_profiler 模式放置在 ls.exe 旁；`main.c` AOT 链接命令无条件追加 `ls_os_backend.lib`（ls.exe 本身已含 LS_OS_SOURCE 供 JIT 的 AbsoluteSymbols 注册）
  - **JIT AbsoluteSymbols 扩展**：从 10 个扩展到 63 个，新增全部 `ls_os_time_*` / `ls_os_sleep_*` / `ls_os_exec_*` / `ls_os_fs_*` / `ls_os_env_*` 等符号；移除 `jit.c` 内的 inline `ls_os_perf_now`，改用 `extern` 声明（定义在 os_win32.c）
  - **测试**：`tests/samples/time_basic_test.ls`（24 个 PASS 用例：unix 时间戳、duration 构造、now_local 字段范围、now_utc utcoff==0、format、iso8601、add+diff_s、parse、sleep_ms）；ctest `test_std_time` JIT + AOT 双跑；依赖 `test_at_time_bench`
  - 验证：ctest 32/32 通过

- **闭包 Phase F.7（Memcheck 压力测试 + 两个真实 bug 修复）** — 2026-05-14
  - **压力测试**：`tests/samples/closure_f7_stress_test.ls`（1000 次迭代，每次 6 种捕获模式：S1 POD by-copy / S2 string by-move factory / S3 struct(has_drop)+Block field / S4 vec(Block) push+call+drop / S5 has_drop enum by-move capture / S6 inline [move] vec capture）；`tests/test_phase_f7_stress.cmake` JIT + JIT-memcheck + AOT + AOT-memcheck 四重；ctest `test_phase_f7_stress` 依赖 `test_phase_f5_closure`
  - **Bug 1 修复：`emit_enum_clone_val`（has_drop enum 函数参数未深拷贝）**
    - 根因：`AST_CALL` argument codegen 对 `TYPE_STRUCT && has_drop` 会调 `emit_struct_clone_val` 深拷贝，但对 `TYPE_ENUM && has_drop` 没有对应处理。当 `Option(string) opt` 传给 `make_opt_getter(opt)` 时，参数和 outer `opt` 共享同一 string data 指针 → double-free
    - 修复：新增 `emit_enum_clone_val(ctx, enum_val, enum_type)`（在 `emit_struct_clone_val` 之后）：alloca 临时存储 → load disc → switch on disc → 每个 has_drop variant 按字段类型克隆（string: `emit_string_clone_val`；has_drop struct: `emit_struct_clone_val`；嵌套 has_drop enum: 递归）→ load 并返回新值；前向声明加到 line ~252
    - AST_CALL argument codegen（line ~8694 struct clone 块之后）新增 `else if (TYPE_ENUM && has_drop)` 分支，调用 `emit_enum_clone_val`；同样支持 `__move(e)` 显式转移（设 moved_flag）
  - **Bug 2 修复：enum match arm string binder 独立所有权**
    - 根因：`Some(s) => { return s }` 中 binder `s` 直接 load 了 enum payload 的 string struct（共享 data 指针），标 `is_borrowed=true` 防止 scope cleanup 释放。但 `return s` 把这个共享指针传给 caller → `ov`（caller）和 env_drop（closure）各自 free 同一指针 → double-free
    - 修复：enum match binder 对 `TYPE_STRING` 字段调 `emit_string_clone_val`，给 binder 独立所有权；此时 `is_borrowed = !binder_owns`（string binder 标 false，允许 scope cleanup 在 arm 正常退出时释放克隆；return 路径由 return_alloca skip 机制保护）
  - **验证**：ctest 30/30 通过；JIT + AOT memcheck 均 0 leaks / 0 double-free

- **闭包 Phase F.5（enum capture + 完整 drop 模型）** — 2026-05-14
  - **F.5.1 checker：capture_type_supported 接受 enum**：非 has_drop enum（disc-only / POD payload，如 Direction、Color）走 by-copy；has_drop enum（含 string/struct payload，如 Result/Option-with-string）走 by-move；`capture_type_is_by_move` 新增 `case TYPE_ENUM: return t->as.enom.has_drop`
  - **F.5.2 codegen：by-copy 路径**：非 has_drop enum 的捕获与 POD 一致（env 字段直接存值，body 端 alloca+store，scope cleanup 跳过——enum 无堆内存），无额外 drop
  - **F.5.3 codegen：by-move 路径**：has_drop enum 捕获时 env 接管堆内存；outer 变量通过 `moved_flag`（i1 alloca）标记移动（与 struct 一致）；body 端 `is_borrowed=true` 防止 scope cleanup 双 drop；`var_decl`/`fn_decl` 参数注册处扩展为 `(has_drop struct || has_drop enum)` 分配 moved_flag
  - **F.5.4 `emit_enum_drop_cond`**：新合成辅助函数，类似 `emit_struct_drop_cond`，按 moved_flag 有条件调用 `Enum.__drop(ptr)`；`emit_scope_cleanup` 和 `emit_cleanup_to` 的 enum has_drop 分支改用此函数
  - **F.5.5 env_drop（`__env_drop_N`）enum slot**：枚举捕获进 env_drop 循环，调用 `emit_auto_enum_drop_fn` 确保 drop_fn 已生成，然后 `call enum.__drop(slot_ptr)`
  - **F.5.6 `emit_auto_enum_drop_fn` 完整 drop 模型**：扩展 payload 处理，新增：`TYPE_VECTOR` payload（load LsVec，cap>0 时元素逐个 drop + free data buffer）；`TYPE_MAP` payload（调 `__ls_map_XX_drop`）；嵌套 has_drop enum payload（递归调 `emit_auto_enum_drop_fn`）
  - **F.5.7 match codegen 终结符 bug 修复**：`AST_MATCH` 的 enum switch arm 和 非 enum if/else arm 在编译完 arm body 后，若 body 含 `return` 语句，该 block 已有终结符，再调 `LLVMBuildBr` 会产生 "Terminator found in middle of basic block"；在所有 `LLVMBuildBr(ctx->builder, merge_bb)` 调用前加 `LLVMGetBasicBlockTerminator(...) == NULL` 检查，共 5 处（enum wildcard / enum case / 非 enum wildcard / 非 enum then / 非 enum fallthrough）
  - **测试**：`tests/samples/closure_f5_test.ls`（8 组：Direction 非 has_drop by-copy / Option(int) by-copy / Result(int,string) by-move / 工厂函数 / Direction+int 混合 / Color POD payload by-copy / vec(OptGetter) / 多次调用）；`tests/test_phase_f5_closure.cmake` JIT + memcheck + AOT + AOT-memcheck 四重；ctest `test_phase_f5_closure` 依赖 `test_phase_f4_closure`
  - 验证：ctest 29/29 通过；memcheck 0 leaks / 0 double-free

- **闭包 Phase F.6（CG_DEBUG 全面铺开）** — 2026-05-14
  - **目的**：为闭包 capture/env/drop 全流程注入结构化运行期日志，方便调试 codegen 内部的内存管理决策（Phase C 起每次改动都可 `-DLS_CG_DEBUG=ON` 验证路径正确性）
  - **四个 helper（`codegen.c` lines ~115-176）**：
    - `cg_dbg_capture(ctx, name, t, kind)` — 每个捕获变量记录一行 `[cg] cap.<kind> name='...' type='...'`，kind 为 copy/move/move-expl/borrow
    - `cg_dbg_outer_mark(ctx, name, marker)` — outer 变量被标 MOVED 时记录（cap=-1 / moved_flag=1 / vec.cap=0 / map.cap=0）
    - `cg_dbg_env_alloc(ctx, id, size, env_ptr)` — env 堆分配时记录 closure_id + size + 运行期 ptr
    - `cg_dbg_block_op(ctx, op, label, env_ptr)` — 通用 block 操作（drop/move/field.drop/elem.drop/env.drop）
  - **所有 helper 全部包裹 `#if CG_DEBUG ... #else (void)... #endif`**；涉及 LLVM 指令构建的也在 `#if CG_DEBUG` 内（`cg_null_block_env` 的 load+log、`emit_vec_elem_drop_at` 的 load+log），`CG_DEBUG=0` 时零 IR 开销
  - **注入点完整列表**：
    - `codegen_closure_literal` step 0（cap_outer_vals 加载循环）：按 is_default_by_ref / capture_type_is_by_move_cg / explicit_move+vec/map / copy 四分支各调 `cg_dbg_capture`
    - `codegen_closure_literal` step 9b（env alloc 后）：`cg_dbg_env_alloc`；outer 标记处（string cap=-1、struct/enum moved_flag=1、[move] vec.cap=0、[move] map.cap=0）各调 `cg_dbg_outer_mark`
    - `cg_emit_block_env_drop`：`cg_dbg_block_op(env.drop)` — 每次 env 被释放时记录运行期 ptr
    - `cg_null_block_env`（`#if CG_DEBUG`）：读出 old_env → `cg_dbg_block_op(move, src->null)`
    - `emit_scope_cleanup` TYPE_BLOCK 分支（`#if CG_DEBUG`）：`cg_dbg_block_op(drop, var='name')`
    - `emit_cleanup_to` TYPE_BLOCK 分支（`#if CG_DEBUG`）：`cg_dbg_block_op(drop, var='name')`
    - `emit_auto_drop_fn` TYPE_BLOCK 字段：`cg_dbg_block_op(field.drop, field_name)`
    - `emit_vec_elem_drop_at` TYPE_BLOCK（`#if CG_DEBUG`）：load env ptr → `cg_dbg_block_op(elem.drop, label)`
  - **CMake 使用方式**：`cmake ... -DLS_CG_DEBUG=ON`（注意是 `LS_CG_DEBUG`，不是 `CG_DEBUG`）；正式 build 保持 OFF
  - **示例输出**（F.5 test）：
    ```
    [cg] cap.copy    name='d           ' type='Direction'
    [cg] env.alloc  closure_id=0    size=16     ptr=0x...
    [cg] block.drop     var='f'            env=0x...
    [cg] cap.move    name='r           ' type='Result(int,string)'
    [cg] outer.mark name='r           ' state='MOVED'  marker='moved_flag=1'
    [cg] block.env.drop                   env=0x...
    ```
  - 验证：ctest 29/29（正式 `build/`，CG_DEBUG=0）；`build_dbg/`（CG_DEBUG=1）编译通过，F.5 日志输出完整覆盖所有 8 个测试用例的捕获路径

- **闭包 Phase F.4（vec(Block) / map(K, Block)）** — 2026-05-14
  - **F.4.1 vec(Block) push 所有权转移**：`vec.push` codegen 新增 TYPE_BLOCK 分支：RHS 为 IDENT Block 变量时 `cg_null_block_env` null source env，RHS 为 closure 字面量时 `temp_block_env_count--`（pop rvalue env）
  - **F.4.2 vec(Block) 元素 drop**：`emit_vec_elem_drop_at` 新增 TYPE_BLOCK case：调 `cg_emit_block_drop_at(ctx, elem_ptr)`（4-block 条件释放流）；`emit_cleanup_to` 的 `elem_needs_drop` 判断加入 `TYPE_BLOCK`
  - **F.4.3 map(K, Block) set 所有权转移**：`map.set` codegen value 参数为 TYPE_BLOCK 时，调用后 null source env；MAP_EMIT_FREE_VAL 宏增 `val_is_block` 分支调 `cg_emit_block_env_drop`；`emit_map_helpers_for` 按 `val_is_block` flag 合成 drop/clear 辅助函数
  - **F.4.4 checker expected_type 传播**：`check_vec_method` push 和 `check_map_method` set 对 Block 元素/值类型设 `c->expected_type`，让 closure 字面量 `|x| ...` 正确推导参数和返回类型
  - **F.4.5 直接下标调用语义**：`vec[i](args)` / `map.get(k)(args)` 直接对临时 Block SSA 值发起 block_call，不经过 alloca → 不触发 scope cleanup → 无 env aliasing double-free；这是 vec(Block)/map(Block) 的惯用调用模式
  - **F.4.6 parser 修复 `starts_var_decl` 行号检查**：`starts_var_decl` 的 `TypeName(Args)` → `varname` 推断路径中，增加 `p->current.line == saved_cur.line` 检查：变量名必须与类型在同一行，防止 `fn(x)\n varname =` 跨行误判为 var_decl（根因：`print(cur(10))` 后接 `i = i + 1` 被误识别为 `print(cur_type) i = ...`）
  - **F.4A checker 拒绝容器 Block aliasing**：新增 `checker_reject_block_vec_index` 和 `checker_reject_block_map_get` 两个检查函数；在 `check_var_decl` 和 `check_assign` 的 TYPE_BLOCK 路径调用；拒绝 `Block g = ns[i]`（vec 元素读出）和 `Block g = ops.get(k)`（map 值读出），报错并提示使用直接调用形式；未来 env clone 方案见 `docs/block_clone_plan.md`（Phase G）
  - **测试**：`tests/samples/closure_f4_test.ls`（3 组：vec(H) push 3 lambdas + 直接下标调用 / vec(Namer) push string [move] closures + 直接下标调用 / map(string,H) set lambda + 直接 get+call）；`tests/test_phase_f4_closure.cmake` JIT + AOT + memcheck 四重；ctest `test_phase_f4_closure` 依赖 `test_phase_f3_closure`
  - 验证：ctest 28/28 通过；memcheck 0 leaks / 0 double-free

- **闭包 Phase F.3（Block 作为 struct 字段）** — 2026-05-14
  - **F.3.1 has_drop 扩展**：`check_struct_decl` has_drop 推导循环新增 `TYPE_BLOCK` 分支，含 Block 字段的 struct 自动标 `has_drop=true`，触发 `__drop` 方法合成及注册
  - **F.3.2 expected_type 传播**：AST_NEW_EXPR 字段初始化时对 Block 类型字段设 `c->expected_type = field_type`，让 closure 字面量 `|x| ...` 正确推导 param/return 类型（与函数调用参数推导一致）
  - **F.3.3 Block 字段 env 所有权转移**：struct literal 构造时，若字段值为 closure 字面量则 pop `temp_block_envs`（literal rvalue），若为 IDENT Block 变量则 `cg_null_block_env` 置 null source env；两者均标 `checker_try_mark_moved`
  - **F.3.4 emit_auto_drop_fn Block 字段**：`emit_auto_drop_fn` 字段遍历新增 TYPE_BLOCK 分支：从 struct self_ptr GEP 取字段，load Block struct，提取 env_ptr，调 `cg_emit_block_env_drop`（4-block 有条件 drop+free 流）
  - **F.3.5 AST_FIELD 读 Block 字段**：直接返回 field_val（struct 保持 env 所有权）；不注册 temp，不 clone；checker 通过 `checker_reject_block_field_read` 拒绝 `Block g = p.step1` aliasing 读（只允许 `p.step1(args)` 直接调用）
  - **测试**：`tests/samples/closure_f3_test.ls`（5 组：无 capture 双字段 Pipe / 工厂函数返回 Pipe / string by-move capture Maker / MultiDrop 三种 has_drop 字段混合 / 命名 Block 变量移入字段）；`tests/test_phase_f3_closure.cmake` JIT + AOT + memcheck 四重；ctest `test_phase_f3_closure` 依赖 `test_phase_f2_closure`
  - 验证：ctest 27/27 通过；memcheck 0 leaks / 0 double-free

- **闭包 Phase F.2（Block 赋值 + 移动语义）** — 2026-05-14
  - **F.2.1 type_is_movable 加 TYPE_BLOCK**：`checker_try_mark_moved` 现在会对 Block IDENT 赋值来源标 `is_moved`，后续使用报「use of moved variable」
  - **F.2.2 Block 参数标 is_borrow**：fn_decl / impl 方法 / closure 字面量三处参数注册路径统一将 TYPE_BLOCK 形参的 checker Symbol 标 `is_borrow=true`；新增 `checker_reject_block_param_move`，在 var_decl / assign 两处拒绝把 Block 形参赋给新 Block 变量（防 env 双释放）
  - **F.2.3 codegen 两个辅助函数**：`cg_null_block_env(ctx, alloca)` 将 Block 的 env_ptr 字段（field 1）存 NULL；`cg_emit_block_drop_at(ctx, alloca)` 加载 Block → 提取 env_ptr → 调 `cg_emit_block_env_drop`
  - **F.2.4 var_decl TYPE_BLOCK 拆分路径**：RHS 为 IDENT（Block 变量移入）→ 调 `cg_null_block_env` 置 null source env；RHS 为 closure 字面量 → 沿用 pop temp_block_envs
  - **F.2.5 assign TYPE_BLOCK 新分支**：先 `cg_emit_block_drop_at` drop 目标旧 env，再 store 新值，再对 IDENT source 调 `cg_null_block_env`（closure literal 则 pop temp_block_envs）
  - **F.2.6 return TYPE_BLOCK skip**：AST_RETURN 的 return_alloca skip 列表加入 TYPE_BLOCK，使 `return g` 正确跳过 g 的 scope cleanup，env 转移给 caller
  - **测试**：`tests/samples/closure_f2_test.ls`（5 组用例：简单移动 / POD capture 移动 / 赋值后原变量失效 / 无 capture 移动 / 工厂函数返回 named Block）；`tests/test_phase_f2_closure.cmake` JIT + AOT + memcheck 四重；ctest `test_phase_f2_closure` 依赖 `test_phase_f1_closure`
  - 验证：ctest 26/26 通过；memcheck 0 leaks / 0 double-free

- **闭包 Phase F.1（`[move v]` 语法：vec/map 显式 by-move 捕获）** — 2026-05-14
  - **F.1.1 Parser**：`prefix_array_lit` 检测 `[move ident, ...]` 前缀 → 调 `prefix_capture_spec` → 解析 move_names 列表 → 接着解析闭包字面量并赋给 AST_CLOSURE.move_names / move_count
  - **F.1.2 AST**：`AstCapture.is_explicit_move` 新字段；`AstClosureNode.move_names / move_count` 新字段；ast_free 释放 move_names
  - **F.1.3 Checker**：capture_walk 后处理 move_names → 找到对应 capture 标 `is_explicit_move=true`；对 vec/map 的 explicit move 做 outer moved 检查并标 `outer->is_moved=true`；未捕获名报错
  - **F.1.4 Codegen**：by-ref 分支按 `is_explicit_move` 拆出 by-move 路径：env 字段存值类型（非 `ptr_t`），cap_outer_vals 做 Load（非 alloca 地址），outer cap 字段置 0
  - **F.1.5 env_drop**：explicit-move vec/map 字段进入 drop loop，调对应 `__ls_vec/map_<T>_drop`
  - **测试**：`closure_f1_test.ls`（4 组：vec move factory + pollute 验证 / map move factory / vec move counter / map move string）；ctest 25/25 通过
  - 已知限制：`[move v]` 对 vec/map 函数参数（浅拷贝）不安全（调用方和被调方共享 data 指针），仅安全用于局部声明变量

- **闭包 Phase E.2 + E.4（closure 参数借用 + array 捕获）** — 2026-05-10
  - **E.2 closure 参数 is_borrowed**：`codegen_closure_literal` step 5b 对 `vec(T)` / `map(K,V)` / `Block` 参数设 `is_borrowed=true`，与 `codegen_fn_decl`（line ~12117）保持一致；修复 `|v| { return sum_vec(v) }` 被调时的 double-free（closure body scope cleanup 误 free 调用方的 data buffer）
  - **E.4 array(POD,N) by-value 捕获**：checker `capture_type_is_pod_array()` 辅助函数判断 element 是否 POD；`capture_type_supported` 接受后，codegen 自动走 by-copy 路径（`[N x T]` 整体 load→store 进 env，无额外 drop 注册）；外层数组不标 moved，snapshot 语义（捕获时刻的副本）
  - **测试**：`tests/samples/closure_e2_e4_test.ls`（5 组用例：E.2.1 vec param / E.2.2 map param / E.4.1 基础 index / E.4.2 snapshot / E.4.3 多闭包独立副本）；`tests/test_phase_e2_e4_closure.cmake` JIT + AOT + memcheck 三重；ctest `test_phase_e2_e4_closure` 依赖 `test_phase_e1_closure`
  - 验证：ctest 24/24 通过

- **闭包 Phase E.1（by-ref capture 重构：vec/map 捕获存外层指针）** — 2026-05-10
  - **E.1.1 env layout 变更**：vec/map 的 by-ref 捕获在 env struct 字段中存储的是 `ptr`（指向外层 alloca），而非值拷贝（之前 C.7 存的是 `{ptr, i32, i32}` 值）；env 字段类型由 `type_to_llvm(ct)` 改为 `ptr_t`
  - **E.1.2 body 端恢复**：在 `codegen_closure_literal` step 5a 中，by-ref 路径通过 `LLVMBuildLoad2(ptr_t, field_ptr, "cap.refptr")` 从 env 取出外层 alloca 地址，直接将其设为 `sym->value`；`is_borrowed=true` 防止 body scope cleanup 误 drop
  - **E.1.3 capture 阶段**：`cap_outer_vals[i] = sym->value`（alloca 地址，非 load 值），之后 store 到 env 字段（存指针不存值）；外层变量不标 moved
  - **E.1.4 可见性**：outer 变量的 push/set 在 capture 之后发生时，closure body 读取时能看到最新状态（`nums.push(40)` 后再 `adder()` 返回 100）
  - **E.1.5 bug 修复（call site `arg_type` 未声明）**：由-ref 重构引入的编译错 —— AST_CALL codegen 中 struct-with-drop 克隆路径使用了 `arg_type` 但未声明（在作用域外）；添加 `Type *arg_type = node->as.call.args[i]->resolved_type;` 修复；此 bug 导致 MSVC 在 Release 下将 struct 克隆代码路径作为 UB 处理，也造成了 vec.clone 的意外调用引发泄漏
  - **测试**：`tests/samples/closure_e1_test.ls` 4 大用例（E.1.1 vec nums + push后再调 / E.1.2 vec items len / E.1.3 data summer 多次调用 / E.1.4 map scores key_counter）；`tests/test_phase_e1_closure.cmake` JIT + AOT + memcheck 0 leaks / 0 double-free 三重；ctest `test_phase_e1_closure` 依赖 `test_phase_c7_closure`
  - 已知限制：closure 不能 outlive 外层变量（by-ref 语义栈安全性，运行时无检查）；嵌套闭包仍拒；enum capture 仍未实现
  - 验证：ctest 23/23 通过

- **闭包 Phase C.7（vec/map/struct(drop) 捕获 + 双语义 ABI）** — 2026-05-09
  - **C.7.1 类型扩展**：`capture_type_supported = POD || string || vec(T) || map(K,V) || struct(has_drop)`；checker 路径与 C.5 共用，由 `capture_type_is_by_move` 决定是否标 outer Symbol moved（vec/map 不标 — 走 borrow；struct 标）
  - **C.7.2 双 ABI 策略**：codegen 区分两种 env 所有权模型，避免一刀切：
    - **borrow 语义**（vec/map/string）：env 存 `{data/buckets, len, cap}` 字段原样（cap > 0 保留以便 body 用 `.length` / `.contains_key` 等读操作），但 env_drop **跳过** 这些 slot；原始 owner（caller 或 closure-defining fn 的 caller）继续负责释放；body 端 CgSymbol 标 `is_borrowed = true` 防止 body 自己 drop。string 走 C.5 路径已工作（param ABI 已 cap=0 borrow），vec/map 新增「不进 env_drop loop」即可
    - **ownership transfer**（struct(has_drop)）：env 实际接管，env_drop 调 `Struct.__drop(slot_ptr)`；outer 通过 `moved_flag = true` 跳过自己的 drop；caller 仍持有 struct 容器但 __drop 被 RAII 跳过
  - **C.7.3 env_drop 分派**：合成 `__env_drop_<id>(env_ptr)` 内按 capture kind 走不同分支：string `cap > 0 → free(data)`；struct `Struct.__drop(slot_ptr)`（必要时 emit_auto_drop_fn 兜底）；vec/map 不进 loop
  - **C.7.4 map 形参 borrowed 修复**：`codegen_fn_decl` 把 `TYPE_MAP` 形参也加入 `is_borrowed = true` 列表（之前只有 vec），否则 callee 的 scope cleanup 会调 `__ls_map_XX_drop` 释放 caller 持有的 buckets → 双释放
  - **C.7.5 Block-call 返回 string 临时跟踪**：`codegen_block_call` 末尾若 ret type 是 string，`cg_push_temp_string(result)` 注册到 `temp_string_slots`；同时 `expr_produces_dynamic_string` 对 Block-call 返回 false，避免 print 的 `__argtmp` scope 注册和 temp_string_slots 同时管同一指针 → 双释放
  - **C.7.6 has_drop_n 修复**：之前只数 `TYPE_STRING`，导致 struct(has_drop) 捕获时 drop_fn slot 为 NULL，env_drop 不生成，struct 字段泄漏；改为 `capture_type_is_by_move_cg` 真正包含的所有 has_drop 类型
  - **测试**：`tests/samples/closure_phase_c7_test.ls` 4 大用例 8 行输出（vec(int) picker / vec(string) joiner / map(string,int) lookup with default / struct(has_drop) stamper）；`tests/test_phase_c7_closure.cmake` JIT + AOT + memcheck 0 leaks / 0 double-free 三重；ctest `test_phase_c7_closure` 依赖 `test_phase_c5_closure`
  - 已知限制：vec/map 是 borrow（closure 不能 outlive 源），future Block move 语义解决；嵌套闭包仍拒；enum capture 仍未实现（payload walk 需要单独工作）
  - 验证：ctest 22/22 通过；Phase D（stdlib io.each_line + with_file）接续

- **闭包 Phase C.5（string by-move 捕获 + 每闭包 env_drop + Block 形参借用）** — 2026-05-09
  - **C.5.1 capture_walk 接受 string**：`capture_type_supported = POD || string`；`cap_record` 对 string 走 by-move 路径：检查 outer Symbol 不是 borrow / 不是已 moved；非 static-string 时 `outer->is_moved = true`（接入现有 Move 检查器，subsequent outer 使用编译错）
  - **C.5.2 env 布局加 drop_fn slot**：env struct 字段 0 永远是 `ptr drop_fn`（NULL 表示 POD-only 不需调用），用户 captures 从 field 1 开始；body 入口 capture-load 用 `i+1` 的 GEP；string capture 在 body scope 标 `is_borrowed=true`（env 是唯一 owner，body 只持快照）
  - **C.5.3 outer cap=-1 marker**：codegen 在 store-into-env 之后，对每个 by-move string capture 的 outer alloca emit runtime 分支：`if outer.cap > 0 { outer.cap = -1 }`（`> 0` guard 让 static string 的 cap=0 不被破坏）；outer scope cleanup 跑到时见 cap<0 自动 skip free → 单一 owner = env
  - **C.5.4 合成 `__env_drop_<id>`**：编译期遍历 captures，has_drop_n > 0 时 emit 一个 `void __env_drop_<id>(env_ptr)`；按 capture 类型生成对应释放（v1 string：load slot, `cap > 0` 条件 free data）；env 构造期把这个函数指针 store 到 env field 0
  - **C.5.5 RAII 双路径升级**：`emit_scope_cleanup` 与 `emit_cleanup_to` 的 TYPE_BLOCK 分支都改成 4-block 流：`if env != NULL { drop_fn = env[0]; if drop_fn != NULL drop_fn(env); free(env) }`；POD-only 闭包 drop_fn=NULL 自动跳过
  - **C.5.6 Block 形参 borrowed**：`codegen_fn_decl` 注册 TYPE_BLOCK 参数 sym 时设 `is_borrowed=true`，callee 不参与 env 释放，调用方继续持有；解决 `use_g(g)` + caller 同时 own → double-free
  - **C.5.7 临时闭包 env 跟踪**：CodegenContext 新增 `temp_block_envs[]`，`codegen_closure_literal` 末尾 push 当前 env_val；`var_decl` (TYPE_BLOCK) 与 `return` (TYPE_BLOCK) 在 cg_flush_temps 之前 `temp_block_env_count--` 表示「var/caller 接管」；其余路径走 cg_flush_temps 末尾的 `cg_emit_block_env_drop`（同样的 drop_fn?+free 流），保证 `apply1(7, |x| ...)` 这类 rvalue 在语句边界释放
  - 测试：`tests/samples/closure_phase_c5_test.ls` 4 个用例（INFO: alice owned + cap clone, TAG: bob mixed POD+string, STATIC: carol 静态 string 不 moved, X: y/X: z 同一 closure 多次调用）；`tests/test_phase_c5_closure.cmake` JIT + AOT + memcheck `0 leaks / 0 double-free` 三重断言；ctest `test_phase_c5_closure` 依赖 `test_phase_c_closure`
  - 已知限制：vec / map / struct(drop) capture 仍未实现（`capture_type_supported` 拒）；嵌套 closure literal 仍拒（capture 透传留给 v2）；Block 真正的 move（如存进 vec/struct field）尚需独立设计
  - 验证：ctest 21/21 通过；Phase C.7（vec/map/struct capture）+ Phase D (stdlib io.each_line) 接续

- **闭包 Phase C（POD 捕获 + 堆 env + RAII）** — 2026-05-09
  - **C.1 自由变量扫描**：`capture_walk()` (checker.c) 在 AST_CLOSURE body 上递归遍历，`bound[]` 跟踪当前作用域内已绑定的名字（params + 内部 var_decl + for 循环变量 + match arm binders + block 边界 snapshot/restore）；遇到 AST_IDENT 不在 bound 中、能在 outer scope 找到 → 记录为 capture（dedup by name）。捕获列表存到 AST 节点的 `closure.captures[]`（自有所有权）；后续 codegen 直接读
  - **C.2 Env struct 合成**：codegen 用 capture list 顺序构造匿名 LLVM struct（每个 capture 一个字段，类型来自 outer 时刻的 LS Type）；用 `LLVMABISizeOfType` 求 layout size 给 malloc 用
  - **C.3 Closure 字面量 codegen 升级**：构造前在 outer scope `LLVMBuildLoad2` 抓取每个 capture 的当前值；`cg_emit_alloc(size, "closure.env", line, col)` 走 memcheck 标签；逐字段 `LLVMBuildStructGEP2 + Store` 装填 env；body 入口（`__closure_N` entry block）反向：`StructGEP + Load + alloca + Store + cg_scope_define` 把每个 capture 还原成普通 local sym，AST_IDENT 解析自然命中
  - **C.4 RAII**：`emit_scope_cleanup` 与 `emit_cleanup_to` 双路径都加 TYPE_BLOCK 分支：`load LsBlock → extractvalue 1 (env_ptr) → ICmpNE NULL → 条件 free`；env=NULL 时跳过 free（兼容 Phase B 无捕获闭包）；`cg_emit_free` 走 memcheck wrapper，kind 标 `closure.env`
  - **AST/Parser 改动**：`closure.captures[] / capture_count` 新字段（ast.h）；3 处 AST_CLOSURE 创建点初始化为 NULL/0；ast_free 释放 capture name 数组
  - **Phase C v1 限制**：(a) POD-only 捕获 —— string/vec/map/struct(drop)/enum 捕获报「not yet implemented」（错误信息列出允许的 POD 类型）；(b) 嵌套闭包字面量（闭包 body 内再写 `\|y\| ...`）报错（capture 透传留给 v2）；(c) 闭包字面量传函数后 callee param 也是 TYPE_BLOCK，env 由 callee scope cleanup 释放（make_adder 模式 ✓ clean，但同一 closure 同时存 local + 传函数会 double-free —— 需要 move 标记，留给 Phase C.5）
  - 测试：`tests/samples/closure_phase_c_test.ls` 7 用例（单 int capture / make_adder × 2 / mixed POD (int+bool+f64) / 形参 shadow outer 同名 / capture 用 2 次）；`tests/test_phase_c_closure.cmake` JIT + AOT + memcheck `0 leaks` 三重断言；ctest `test_phase_c_closure` 依赖 `test_phase_b_closure`
  - 验证：ctest 20/20 通过；Phase C.5 (by-move 接 Move 检查器) + C.7 (string capture / 跨函数 closure 转移) 待续

- **闭包 Phase B（无捕获闭包 codegen：lambda lifting + 胖指针 + 间接 call）** — 2026-05-06
  - **B.1 Lambda lifting**：每个 `\|x\| body` 字面量 codegen 时合成一个顶层 LLVM 函数 `__closure_<N>(env_ptr, params...)`，命名走 `ctx->closure_id_counter` 单调计数器（per-module，AOT/JIT 一致）；body 在新函数 entry block 编译，detach 外层 scope（Phase B 无捕获），完成后恢复 builder/scope/current_fn 状态；尾部缺 terminator 时按返回类型自动补 `ret 0` / `ret void`
  - **B.2 LsBlock 胖指针**：`TYPE_BLOCK` lowered 为 LLVM `{ptr, ptr}` 16-byte struct（`type_to_llvm` case）；闭包字面量 codegen 末尾 `LLVMBuildInsertValue` 装配 `{__closure_N_fn_ptr, NULL_env}` 返回；env=NULL 占位（Phase C 接堆 env）
  - **B.3 闭包调用 ABI**：`codegen_block_call` 在 AST_CALL 顶部拦截（callee resolved_type==TYPE_BLOCK），`extractvalue 0/1` 取 fn_ptr + env_ptr，按 `ret(env_ptr, params...)` 重建 LLVM fn type，args 数组 prepend env_ptr，`LLVMBuildCall2` 间接调用；走 `cg_widen` 给每个用户参数应用 Zig 风隐式扩展
  - **B.4 类型推导**：Checker AST_CALL arg 循环把 `c->expected_type` 设为 callee 的形参 LS Type，AST_CLOSURE 检测 `is_ruby_form==true` 时直接从 expected_type（必须是 TYPE_BLOCK/TYPE_FUNCTION + 形参数匹配）拷贝 param_types + return_type；找不到合适 expected_type 报 "cannot infer closure parameter types ..."；body 检查时 expected_type 清空避免污染嵌套表达式
  - **AST 改动**：`closure.is_ruby_form` 新增 bool flag，由 parser 在 3 处 AST_CLOSURE 创建点（prefix `\|x\|` / prefix `\|\|` / trailing closure desugar）置 true，legacy `fn(...) {}` 字面量置 false；checker 用这个 flag 选择推导 vs 解析
  - **Trailing closure 单表达式自动 return**：`f(x) { \|y\| y * 2 }` 与 prefix `f(x, \|y\| y*2)` 现在语义一致 —— 解析器在 trailing closure body 只有 1 个 AST_EXPR_STMT 时把它原地转成 AST_RETURN
  - **Checker 放宽**：AST_CALL 的 "cannot call non-function" 检查同时接受 TYPE_FUNCTION 和 TYPE_BLOCK；后续 arg/arity 校验代码无需改动（共用 function 联合体字段）
  - 测试：`tests/samples/closure_phase_b_test.ls` 6 个用例（prefix `\|x\| x+1` / trailing `f(x){\|y\|y*2}` / multi-arg `\|x,y\|` / 零参 `\|\|` / 返回 bool / 多语句 body 显式 return），AOT + JIT 双跑全 ✓；`tests/test_phase_b_closure.cmake` 注册为 ctest `test_phase_b_closure`，依赖 `test_phase_a_closure`
  - Phase A 的 `test_phase_a_closure` 第 4 步（"closure literal accept-then-defer"）已删除 —— Phase B 让那个延期解除，等价断言迁到 Phase B 测试
  - 验证：ctest 19/19 通过；Phase C（捕获 + 堆 env + RAII）接续

- **闭包 Phase A（type 别名 + Block 类型语法 + Ruby 风字面量 + trailing closure 糖）** — 2026-05-06
  - **A.1 type 别名（L1 透明结构别名）**：新增 `TOKEN_TYPE_ALIAS` keyword，`AST_TYPE_ALIAS_DECL { name; target }`，Checker 加 `type_aliases[]` 注册表；`resolve_type_node` 在 `TYPE_NODE_NAMED`（arg_count==0）时优先查 alias 表再回落 struct/enum；前向声明规则：alias 必须在引用之前声明（同源文件内顺序）
  - **A.2/A.4 Block 类型**：新增 `TOKEN_BLOCK` keyword（"Block" 大小写敏感，binary search 表头新增），`TYPE_NODE_BLOCK`（AST 层）+ `TYPE_BLOCK`（语义层，复用 `function` 联合体），`type_block(params, n, ret)` 构造器；`type_clone` / `type_free` / `type_equals` / `type_name` 全部 fall-through 到 function 路径；codegen 暂未涉及 TYPE_BLOCK lowering（Phase B/C 任务）
  - **A.3 强制别名规则**：Parser 加 `Parser.in_return_type` 标志位，`fn`/`extern fn` 的 `-> RET` 与 `struct { ... }` 字段类型解析期间置 true；`parse_type` 看到 `TOKEN_BLOCK` 且 `in_return_type==true` 时立即报 `[error] Block type cannot appear directly here ...; define a type alias first`；嵌套 `Block(...) -> Block(...)` 也禁，强制内层 Block 也走别名
  - **A.5 Ruby 风闭包字面量 + trailing closure 糖**：`TOKEN_PIPE` / `TOKEN_OR` 新增 prefix handler `prefix_ruby_closure` / `prefix_no_arg_closure` —— `|x, y| body` / `|| body` 字面量；body 可为 `{ block }` 或单表达式（自动包成 `{ return expr }`）；param_types 全 NULL（占位），由 callee 期望签名推导（Phase B/C）；`infix_call` 在 `)` 之后看到 `{` 紧跟 `|` 则消费为 trailing closure 并 append 到 args（`f(args) { \|x\| body }` ≡ `f(args, \|x\| body)`）；`{` 后必须以 `\|` 开头才识别为闭包，避免与 if/while/struct literal 的 `{` 冲突
  - **Checker 优雅失败**：AST_CLOSURE 检测到任一 `param_types[i] == NULL`（Ruby form 标记）时，单点报错 `Ruby-style closure literal '|...| body' requires context-driven type inference (Phase B/C of the closure plan); not yet implemented`，不向下递归 body 检查；fn(...) 形式 closure（旧路径，显式 type）保持原行为
  - 测试：`tests/samples/type_alias_block_test.ls`（type 别名 + Block 类型声明，AOT+JIT 双跑 print 42/hello/3）；`tests/samples/block_return_reject.ls` / `block_field_reject.ls`（裸 Block 在禁区位置编译错）；`tests/samples/closure_literal_phase_a_reject.ls`（`|x| x+1` + trailing form 都进 checker，都报 Phase B/C 延期 message，确认两个form 都 parse 通过）；`tests/test_phase_a_closure.cmake` 注册为 ctest `test_phase_a_closure`，4 个子用例
  - 验证：ctest 18/18 通过（17 旧 + 1 新 Phase A）；闭包 codegen 在 Phase B 接续

- **Memcheck Phase D.1/D.2/D.3（调用栈追踪 + verbose + strict）** — 2026-05-03
  - **D.1 调用栈追踪**：每个用户函数在 `--memcheck` 下注入 `ls_mc_enter(fn,file,line)` prologue + `ls_mc_leave()` epilogue（5 个注入点：fn_decl entry / AST_RETURN 带值 + void / fn_decl 末尾隐式 ret-with-value + ret-void/null）；`runtime/memcheck.c` 维护全局 `g_frame_stack[256]`（v1 单线程；overflow 容忍）；`ls_mc_alloc` 捕获顶部 8 帧到 `LsMcEntry.backtrace`；`ls_mc_report` 打印每条 LEAK 的调用链。验证：3 层嵌套 leak 在 JIT/AOT 下都正确显示 `at deepest / at middle / at outer` 完整链
  - **D.2 verbose 模式**：环境变量 `LS_MEMCHECK_VERBOSE=1` 触发；`runtime/memcheck.c` 在每个 alloc/free/realloc 末尾调 `trace_op()` 打印一行 `[mc] +alloc/.-free/realloc ptr=... size=... kind @ file:line:col`；off 时是单 int 比较；0 codegen 改动
  - **D.3 strict 模式**：环境变量 `LS_MEMCHECK_STRICT=1` 触发；`ls_mc_report` 末尾若有 violation 则 `_Exit(2)`（C99 portable，不重入 atexit）；CI 可直接 `echo $?` 判断
  - jit.c `AbsoluteSymbols` 注册从 4 项扩到 6 项含 `ls_mc_enter` / `ls_mc_leave`；编译器合成的 helper（`__ls_global_init`、`__ls_ffi_init`、`__drop` 字段清理、`__ls_str_replace` 等）走不同 codegen 路径，不进 enter/leave，保持平衡
  - 验证：ctest 9/9 全过；`memcheck_phase_a/kinds/edge` AOT+JIT 双路径全部 ✓ clean，无 enter/leave 失衡

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
| `Block(...)` | ✅ | Phase F.2：env_ptr 非 NULL 时持有堆 env；赋值后 source.env_ptr 置 NULL |
| `int/f64/bool` | ❌ | 值语义，拷贝即可 |
| `*T`, `object` | ❌ | 裸指针，不做 RAII |

### 7.6 实现计划（优先级顺序）

String Phase A（顺序语句线性分析）→ String Phase B（if/else + 循环控制流）→ Struct Phase A/B → 借用语义

> 详细计划见 [docs/move_semantics_plan.md](docs/move_semantics_plan.md)

---

## 8. 闭包捕获策略（设计已固化）

### 8.1 捕获策略总表

| 捕获类型 | 策略 | outer 变量 | 设计理由 |
|----------|------|------------|----------|
| `int / f64 / bool / char / *T / object` | **by-copy** | 保持 live | POD，无堆内存 |
| `array(POD, N)` | **by-copy** | 保持 live（snapshot） | 固定大小值类型，元素无堆内存 |
| `string` | **by-move** | 标 MOVED（cap = −1） | 工厂模式必须持有堆内存（见 §8.2） |
| `struct(has_drop)` | **by-move** | 标 MOVED（moved_flag） | 同上 |
| `vec(T)` | **by-ref** | 保持 live，可继续 push | 逃逸场景极少；by-ref 让捕获后的 push 可见 |
| `map(K,V)` | **by-ref** | 保持 live，可继续 set | 同 vec |
| `enum`（含 has_drop） | 未实现 | — | 需要 payload walk + env_drop 分派 |
| `array(has_drop elem, N)` | 未实现 | — | 需要 element-wise clone |

### 8.2 by-ref 捕获的悬垂风险（所有 by-ref 类型通用）

**by-ref 捕获有一个根本限制：闭包不能 outlive 外层变量。** 这对 vec/map 同样成立——工厂函数返回捕获了局部 vec 的闭包，会在运行时产生悬垂指针（编译器不检查）：

```ls
type Summer = Block() -> int

fn make_summer() -> Summer {
    vec(int) nums = [1, 2, 3]
    return || {                       // nums by-ref → env 存 &nums（栈帧指针）
        int s = 0; int i = 0
        while i < nums.length { s = s + nums[i]; i = i + 1 }
        return s
    }
}   // ← 函数返回，nums 栈帧消亡，env 里的指针悬空

fn pollute() -> int {
    vec(int) trash = [999, 888, 777]
    return trash[0]
}

fn main() {
    Summer f = make_summer()
    int x = pollute()    // pollute 的栈帧覆盖 make_summer 遗留内存
    print(f())           // 实测输出 0（nums.length 被覆写），而非预期的 6
}
```

string/struct 之所以用 by-move，正是因为它们**更频繁**地出现在工厂模式里（函数参数 → 捕获后返回）；vec/map 在工厂模式里较少见，才"侥幸"不易出问题。这个区别是**经验性的**，不是结构性的——两者面对的底层风险完全相同。

**当前编译器不检查 by-ref 闭包是否逃逸。用户必须手动保证：捕获了 vec/map 的闭包，其生存期不超过被捕获变量所在的作用域。**

### 8.3 为什么不统一所有类型为 by-ref？

无生命期系统时无法在编译期区分"内联闭包（安全）"和"逃逸闭包（不安全）"，统一 by-ref 会让工厂模式产生无法检测的悬垂指针（string/struct 工厂模式是语言核心用例）。

### 8.4 未来演进路径

引入生命期标注后，可以改为：**默认 by-ref + `move` 关键字显式转移**（Rust `move |...| ...` 路线）——编译器用生命期分析强制 by-ref 闭包不逃逸，`move` 捕获的闭包可逃逸。在此之前，当前按类型静态决定策略是最稳妥的折中。

> 详细设计见 [docs/closures_plan.md §1.2](docs/closures_plan.md)
