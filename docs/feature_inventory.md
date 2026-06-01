# LS 功能清单与路线图

> **最后更新**：2026-05-31  
> 本文档记录 LS 语言已实现功能的完整清单，以及尚未实现功能的工作量/风险/价值评估。

---

## 一、已实现功能

### 1. 语言核心

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| 词法分析 (Scanner) | 完整 token 集，含关键字、运算符、字符串/数字/f-string 字面量 | `src/scanner.c`, `src/token.h` |
| Pratt Parser | 递归下降 + 运算符优先级，产出完整 AST | `src/parser.c`, `src/ast.h` |
| 静态类型系统 | C 风格类型前置 `int x = 42`，分号可选 | `src/types.c`, `docs/syntax_guide.md` |
| LLVM IR 代码生成 (AOT) | AST → LLVM IR → 原生可执行文件 | `src/codegen.c`, `docs/build.md` |
| LLJIT 增量编译 + REPL | `ls run file.ls` JIT 执行 / `ls repl` 交互 | `src/jit.c` |
| 函数 | `fn name(params) -> RetType { body }`，无返回值省略 `-> type` | `docs/syntax_guide.md` |
| struct + impl 块 | 字段声明、方法（`self`/`&self`/`&!self`/`static fn`） | `docs/syntax_guide.md` |
| enum (tagged union) | payload 变体、自递归（自动 box）、has_drop 自动 `__drop` 生成 | `docs/enum_design.md` |
| 内建 Option(T) / Result(T,E) | 按需单态化模板，上下文消歧构造器 | `docs/enum_design.md` |
| match 模式匹配 | enum 变体解构 `Some(v)` / `Node(v,l,r)`，强制穷尽性检查 | `docs/enum_design.md` |
| `try` 早返操作符 | Zig 风格 `int v = try fn_returning_result(...)` | `docs/phases.md` Phase 8.5 |
| for 循环 | C 风格 `for(init;cond;update)` + range `for i in 0..10` | `docs/syntax_guide.md` |
| while 循环 | `while cond { body }` | `docs/syntax_guide.md` |
| if / else | 标准条件分支 | `docs/syntax_guide.md` |
| 数值隐式扩展 | Zig 风规则：`int→i64`、`int→f64`、`f32→f64` 等安全扩展自动完成 | `docs/phases.md` |
| `as` 显式类型转换 | 收窄/跨符号/浮点→整数必须显式 `as` | `docs/syntax_guide.md` |
| `f"text {expr}"` 格式化字符串 | 运行时字符串插值 | `docs/syntax_guide.md` |
| `print()` 内建 intrinsic | 任意可打印类型 | — |
| `object` 类型擦除指针 | `void*` 语义 | — |
| 全局变量 | 顶层声明，跨函数/跨模块读写 | — |
| 操作符重载 | Ruby 风符号方法名 `fn +`/`==`/`<` + 内建 trait `Add/Sub/Mul/Div/Rem/Eq/Ord`（软保留）；checker 把 `a OP b` 降级为合成方法调用；泛型 `T: Add` 体内可用 | `docs/plan_operator_overload.md` |

### 2. 类型系统高级特性

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| string (RAII) | `LsString { data, len, cap }`，19+ 方法（upper/lower/split/join/trim/replace 等），作用域退出自动释放 | `docs/string_semantics.md` |
| vec(T) 动态数组 | push/pop/get/set/len + 函数式操作（见第 4 节），`[..]` 字面量 | `docs/syntax_guide.md` |
| map(K,V) 哈希映射 | set/get/contains_key/remove/clear/keys/values | `docs/syntax_guide.md` |
| array(T,N) 固定数组 | 索引读写、`.length`、for-in 迭代 | `docs/syntax_guide.md` |
| Move 语义 | string/struct(has_drop)/Block/vec/map 的所有权跟踪；MAYBE_MOVED 单调状态机 | `docs/move_semantics_plan.md` |
| 借用 `&T` / `&!T` | 函数参数位置的只读/可写借用；支持 string/vec/map/struct | `docs/move_semantics_plan.md` |
| `type` 别名 | `type F = Block(int) -> int`，透明结构别名 | `docs/phases.md` Phase 10 |

### 3. 泛型与 Trait

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| 泛型 struct (G1) | `struct Pair(A, B) { A first; B second }` 参数化结构体 | `docs/generics_v2_plan.md` |
| 泛型函数 (G2) | `fn map_val(T, U)(T x, Block(T) -> U f) -> U` 参数化函数 | `docs/generics_v2_plan.md` |
| Trait 定义与实现 | `trait Describable { fn describe(&self) -> string }` + `impl Trait for Type` | `docs/trait_plan.md` |
| Trait bounds (函数) | `fn show(T: Describable)(T item)` 编译期约束检查 | `docs/trait_plan.md` Step 8 |
| Trait bounds (struct) | `struct Wrapper(T: Describable) { T item }` | `docs/trait_plan.md` Step 13 |
| Self 关键字 | trait 签名中 `-> Self`，impl 时自动替换为具体类型 | `docs/trait_plan.md` Step 10 |
| 内建类型 impl | `impl Describable for int` / `f64` / `bool` / `string` / `char` | `docs/trait_plan.md` Step 11 |

### 4. 闭包系统

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| Block 类型 + Ruby 风字面量 | `Block(int) -> int` 类型 + `\|x\| x + 1` 语法 + trailing closure 糖 | `docs/closures_plan.md` Phase A |
| 无捕获闭包 codegen | lambda lifting + LsBlock 16B 胖指针 + 间接 call | `docs/closures_plan.md` Phase B |
| POD 捕获 (by-copy) | int/f64/bool/char/ptr/object/array(POD,N) 值拷贝进 env | `docs/closures_plan.md` Phase C/E.4 |
| string 捕获 (by-move) | env 接管堆内存，outer 标 MOVED (cap=-1) | `docs/closures_plan.md` Phase C.5 |
| struct(has_drop) 捕获 (by-move) | env 调 `__drop`，outer 标 moved_flag | `docs/closures_plan.md` Phase C.7 |
| vec/map 捕获 (by-ref) | env 存外层 alloca 指针，闭包可见外层 push/set | `docs/closures_plan.md` Phase E.1 |
| enum 捕获 | 非 has_drop by-copy；has_drop by-move | `docs/closures_plan.md` Phase F.5 |
| `[move v]` 显式捕获 | vec/map 显式 by-move（工厂模式安全） | `docs/closures_plan.md` Phase F.1 |
| Block 赋值 + 移动语义 | `F h = g` 后 g.env_ptr 置 NULL | `docs/closures_plan.md` Phase F.2 |
| Block 作为 struct 字段 | has_drop 扩展，env 所有权转移给 struct | `docs/closures_plan.md` Phase F.3 |
| vec(Block) / map(K, Block) | push/set 转移 env，drop 释放元素 env | `docs/closures_plan.md` Phase F.4 |

### 5. vec 函数式操作

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| filter / map_fn / reduce | `v.filter(\|x\| x > 0)` / `v.map_fn(\|x\| x*2)` / `v.reduce(0, \|a,b\| a+b)` | `docs/plan_vec_functional.md` |
| each / any / all | `v.each(\|x\| print(x))` / `v.any(\|x\| x>5)` / `v.all(\|x\| x>0)` | `docs/plan_vec_functional.md` |
| sort_by | `v.sort_by(\|a,b\| a-b)` — 含 qsort 无捕获路径 + 插入排序闭包路径 | `docs/known_limitations.md` L-001 |
| flat_map / find / count | 高阶集合操作完整集 | `docs/plan_vec_functional.md` |

### 6. 模块系统

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| module / import | `import math` / `import std.time as T`；支持 `as` 别名 | `docs/phases.md` Phase 7 |
| 跨模块全局变量 | 导出/导入全局状态；符号前缀化 `<mod>__var` + has_drop 全局 cleanup；`mod.VAR` 跨模块访问 | `docs/plan_module_namespace.md` |
| codegen 两阶段 | Pass A forward-declare → Pass B 生成 body，解决跨模块依赖排序 | — |
| 跨模块函数 mangling (L-009) | 模块自由函数 LLVM 符号前缀化 `<modpath>__<fn>`，消除同名崩溃/静默错值；含模块泛型实例化 `<mod>__fn(args)` (L-009.1/6.A) | `docs/plan_l009_mangling.md` |
| 模块命名空间 (B-full) | struct/enum LLVM 类型名 + impl 方法/drop/clone 前缀化（B-2/B-3）；同名类型多模块导入冲突检测（B-1）；类型位置限定语法 `mod.Type` / `alias.Type`（B-4）；带方法同名 struct/enum 跨模块共存（B-4.1）；跨模块同名 enum 变体（B-5）；综合 has_drop memcheck（B-6） | `docs/plan_module_namespace.md` |

### 7. 标准库模块

| 模块 | 位置 | 说明 | 参考文档 |
|------|------|------|----------|
| `math` | 编译器内建 | 23 个数学函数 + 5 常量 + abs/min/max 多态分派 | `docs/phases.md` Phase 9 |
| `io` | 编译器内建 | read_file/write_file/open/close/read_all/write/seek/tell/size/rewind/append_file/remove + File struct + OpenMode/SeekFrom enum | `docs/phases.md` Phase 9 |
| `std.time` | `std/time.ls` + C 后端 | DateTime struct，now_local/now_utc，format/parse/iso8601，add/diff_s，sleep_ms/sleep_us | `docs/phases.md` |
| `std.perf` | `std/perf.ls` + C 后端 | 高精度计时 now()/rdtsc() | `docs/plan_profiling.md` |
| `std.env` | `std/env.ls` | get/get_or/require/has/set/delete/all 环境变量操作 | `docs/plan_proc_env.md` |
| `std.fs` | `std/fs.ls` | list_dir/exists/is_dir/is_file/mkdir/mkdir_all/rmdir/rename/cwd/chdir | `docs/plan_stdlib.md` |
| `std.path` | `std/path.ls` | basename/dirname/ext/stem/join/is_absolute（纯 LS 字符串操作） | — |
| `std.proc` | `std/proc.ls` | args/program/pid/exit/run/exec/exec_full 进程管理 | `docs/plan_proc_env.md` |
| `std.regex` | `std/regex.ls` + C 后端 | matches/find/find_all/capture/capture_all/capture_named/replace/replace_all/split（Pike VM NFA，线性时间） | `docs/regex_plan.md` |
| `std.strconv` | `std/strconv.ls` | 字符串数值格式化 | `docs/plan_string_parse_format.md` |
| `std.c` | `std/c.ls` | C 底层绑定 (libc 函数直接调用) | — |
| `std.os` | `std/os.ls` + C 后端 | 平台抽象层 (`os_win32.c` / `os_posix.c`) | `docs/stdlib_os_plan.md` |
| `std.json` | `std/json.ls`（795 行纯 LS） | 递归下降 parser + stringify；`JsonValue` enum（null/bool/number/string/array/object）；解析/序列化/取值访问器；含 A/B/C/D 微优化（chunk scan / inline at） | `docs/plan_json.md` |
| `std.md` | `std/md.ls`（纯 LS，写 + 读） | Markdown 生成 + 解析。`struct MdDoc { vec(MdBlock) }` + `MdInline`/`MdBlock` enum 树（含嵌套 `vec(vec(MdInline))` lists、`vec(vec(string))` table）；builder（h1-h6/paragraph/code_block/ul/ol/blockquote/table/hr）+ `render` + `fmt_*`；`parse(string)->MdDoc` 块级解析（宽松、手写行扫描）、round-trip 一致；**行内解析**（`**bold**`/`_i_`/`` `c` ``/`***bi***`/`[t](u)`/`![a](u)` → MdInline）+ `extract_headings` / `extract_links` / `to_plain_text`。依赖 vec first-class（L-011a/b/c） | `docs/plan_std_md.md` |

### 8. 字符串方法

| 方法 | 说明 |
|------|------|
| `empty` `at` `find` `rfind` `count` `contains` | 查询 |
| `starts_with` `ends_with` `compare` | 比较 |
| `upper` `lower` `substr` `trim` | 变换 |
| `replace` `append` `copy` | 修改 / 复制 |
| `split` `join` | 分割 / 合并 |
| `to_int` `to_i64` `to_float` `to_bool` | 数值解析（返回 Result） |
| `lines` `repeat` `pad_left` `pad_right` `chars` | 高级操作 |

### 9. C FFI

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| 动态库加载 | `lib x = load("foo.dll")` + `extern fn` 声明 + `lib.call(...)` | `docs/phases.md` Phase 6 |
| Windows | `LoadLibrary` / `GetProcAddress` | `src/ffi.c` |
| 外部 struct 互操作 | `extern struct` + by-value/by-pointer 传递 | tests: `test_extern_struct*` |

### 10. 开发工具

| 功能 | 说明 | 参考文档 |
|------|------|----------|
| Memcheck (JIT) | `ls run --memcheck file.ls` — 泄漏/双释放/无效释放报告 | `docs/memcheck_plan.md` |
| Memcheck (AOT) | AOT 编译链接 `ls_memcheck.lib`，同等报告能力 | `docs/memcheck_plan.md` |
| 细粒度 site 标签 | alloc 带 kind (string.upper / io.slurp / ...) + LS 源码行/列号 | `docs/memcheck_plan.md` |
| 调用栈追踪 | `ls_mc_enter/leave` 注入，leak 报告含完整调用链 | `docs/memcheck_plan.md` |
| verbose / strict 模式 | `LS_MEMCHECK_VERBOSE=1` 实时 trace / `LS_MEMCHECK_STRICT=1` 有 violation 则 exit(2) | `docs/memcheck_plan.md` |
| CG_DEBUG | `#if CG_DEBUG` 编译期运行时内存跟踪 printf，`-DLS_CG_DEBUG=ON` | `docs/phases.md` Phase F.6 |
| REPL 行编辑器 | 自研零依赖（Win32 `_getch` / POSIX termios）：插入/退格/← →/Home/End/Delete/↑↓ 历史；非 TTY 自动 fgets 降级 | `src/repl_edit.c` / `src/repl_term.c` |
| REPL 多行 + import 持久化 | scanner 判定输入完整性（括号/字符串/续行运算符）；`import` 跨语句持续生效（真实 ModuleRegistry + 重放 + 跨 snippet 去重）；语法高亮已实现但暂关 | `src/jit.c` `jit_repl` |

### 11. 测试基础设施

| 项目 | 数量 | 说明 |
|------|------|------|
| ctest 注册测试 | **106 个** | 全部通过（2026-05-31；含 std.md 写/读/行内 + 12 个容器值语义矩阵 `test_cmatrix_*`；flaky AOT 用 `--repeat until-pass:2` 自愈） |
| 单元测试 | `test_scanner` / `test_parser` / `test_types` / `test_codegen` / `test_jit` / `test_ffi` / `test_module` / `test_memory` / `test_operator_overload` / `test_repl` | C 单元测试 |
| 端到端测试 | 大量 cmake 驱动（含 json / 模块命名空间 / 操作符重载 / REPL import 持久化 / BF-040~046 回归） | 每个测试覆盖 JIT + AOT 双路径，部分含 memcheck 验证 |

---

## 二、尚未实现功能

### 优先级排序：收益/工期比

#### ★★★★★ 高收益 · 低工期

| 功能 | 工期 | 风险 | 价值 | 说明 | 参考 |
|------|------|------|------|------|------|
| ~~**操作符重载**~~ | — | — | ✅ 完成 | 2026-05-31：Ruby 风符号方法名 `fn +`/`==`/`<` + 内建 trait `Add/Sub/Mul/Div/Rem/Eq/Ord`（软保留）；checker 把 `a OP b` 降级为合成方法调用复用方法分派；比较"推导默认+逐个可覆写"；泛型 `T: Add` 体内可用；JIT+AOT+memcheck，ctest 88/88 | `docs/plan_operator_overload.md` |
| ~~跨模块函数名 LLVM name mangling~~ | — | — | ✅ 完成 | 模块自由函数前缀化 `<modpath>__<fn>`，消除崩溃/静默错值（验证于 2026-05-29）；struct 方法+泛型跨模块同名留作 L-009.1 | `docs/plan_l009_mangling.md` |
| **Block env 深拷贝 (Phase G)** | 5–7 天 | 低 | ★★★☆☆ | `Block g = vec[i]` 当前被 checker 拒绝（checker.c F.4A 系列守卫）；需合成 `__env_clone_N`，POD+string capture 可克隆 | `docs/block_clone_plan.md` |
| ~~struct 深拷贝 (Phase H)~~ | — | — | ✅ 完成 | `MyStruct s = vec_of_struct[i]` 已自动深拷贝（验证于 2026-05-29，memcheck clean）；见 L-008 | `docs/plan_memory_lifetime.md` |

#### ★★★★ 高收益 · 中工期

| 功能 | 工期 | 风险 | 价值 | 说明 | 参考 |
|------|------|------|------|------|------|
| **DWARF 调试信息 (D.1)** | 10–14 天 | 中 | ★★★★☆ | LLVM DIBuilder API emit file/line/column，AOT only，`ls compile -g` | `docs/plan_future_ideas.md` §6 |
| ~~**JSON 模块**~~ | — | — | ✅ 完成 | 2026-05：`std/json.ls` 纯 LS 递归下降 parser + stringify，`JsonValue` enum，含微优化；解除阻塞项为 L-006 enum vec payload drop 修复 | `docs/plan_json.md` |
| ~~**REPL 改进**~~ | — | — | ✅ 大部完成 | 2026-05-31：自研零依赖行编辑器（Win32 `_getch`/POSIX termios，非 TTY 自动 fgets 降级）+ 通用多行续行（scanner 判定括号/字符串/续行运算符）+ **import 持久化修复**（传真实 ModuleRegistry + 重放 import + 跨 snippet 函数去重/外部链接化）+ 进程内历史。语法高亮已实现但**暂时关闭**（输入态体验未达标）；tab 补全/持久历史落盘未做 | `docs/plan_future_ideas.md` §8 |
| **comptime 编译期执行** | 14–21 天 | 中 | ★★★☆☆ | AST 解释器求值 → 结果嵌入 LLVM IR 全局常量 | `docs/plan_future_ideas.md` §5 |

#### ★★★ 中等收益 · 高工期

| 功能 | 工期 | 风险 | 价值 | 说明 | 参考 |
|------|------|------|------|------|------|
| **并发 (threads + channels)** | 21–30 天 | 高 | ★★★★☆ | OS threads + 消息传递；需 move/borrow 系统线程安全化 | `docs/plan_future_ideas.md` §2 |
| **包管理器 (`ls pkg`)** | 14–21 天 | 中 | ★★★★☆ | `ls.toml` + git-based 依赖；对生态建设至关重要 | `docs/plan_future_ideas.md` §4 |
| **生命期系统** | 21–30 天 | 高 | ★★★★★ | 借用作为返回类型/变量/struct 字段；解决 by-ref 闭包逃逸安全 | `docs/plan_memory_lifetime.md` |
| **VS Code DAP 调试器 (D.2)** | 14–21 天 | 中 | ★★★★☆ | 依赖 D.1 DWARF；Node.js 适配器包装 GDB/LLDB | `docs/plan_future_ideas.md` §6 |

#### ★★ 远期 / 研究性质

| 功能 | 工期 | 风险 | 价值 | 说明 | 参考 |
|------|------|------|------|------|------|
| **WebAssembly 目标** | 10–14 天 | 中 | ★★★☆☆ | LLVM WASM backend，需 WASI runtime 适配 | `docs/plan_future_ideas.md` §3 |
| **PGO / LTO** | 4–7 天 | 低 | ★★☆☆☆ | LLVM PassManager 配置变更，AOT 性能优化 | `docs/plan_future_ideas.md` §9 |
| **HPC / SIMD** | 30–60 天 | 高 | ★★★☆☆ | SIMD 向量类型、intrinsic、FFT/线性代数 | `docs/plan_hpc.md` |
| **GPU 目标** | 60–90 天 | 极高 | ★★★☆☆ | NVPTX/AMDGPU，`@gpu` 注解 + kernel 编译 | `docs/plan_future_ideas.md` §10 |

---

## 三、已知限制

| 编号 | 限制 | 影响 | 改进路径 | 参考 |
|------|------|------|----------|------|
| L-001 | `vec.sort_by` 闭包路径为插入排序 O(n²) | 大数组排序慢 | 短期 emit 归并排序；长期纯 LS 泛型排序 | `docs/known_limitations.md` |
| L-002 | by-ref 闭包无逃逸检查 | 工厂函数返回捕获了局部 vec/map 的闭包产生悬垂指针 | 需生命期系统 | `docs/closures_plan.md` §8.2 |
| L-003 | `File` 不参与自动 drop | 需手动 `io.close`；可多次 close (UB) | 未来 io 改进 | CLAUDE.md §6 |
| L-004 | `io.read_file`/`io.read_all` 受 `long` 限制 | Windows 上 ≤ 2GB（seek/tell 已升级 i64） | 完全迁移到 64-bit API | CLAUDE.md §6 |
| L-005 | 嵌套闭包不支持 | 闭包 body 内不能写 `\|y\| ...` | 需 capture 透传机制 | `docs/closures_plan.md` |
| ~~L-006~~ | ~~enum 含 vec/map payload 的 drop~~ | ✅ 已修复（2026-05-20）：ctor source move + clone vec/map + AOT main i32 | — | — |
| L-007 | Block env 不可克隆 | `Block g = vec[i]` 被 checker 拒绝 | Phase G (`docs/block_clone_plan.md`) | `docs/plan_memory_lifetime.md` |
| ~~L-008~~ | ~~struct 不可从容器深拷贝~~ | ✅ 已修复（验证于 2026-05-29）：`MyStruct s = vec[i]` 对 has_drop struct 自动深拷贝（含嵌套 struct + 函数返回 vec），memcheck clean | — | — |
| ~~L-009~~ | ~~LLVM 函数名不做模块 mangling~~ | ✅ 已修复（2026-05-29）：模块内自由函数 LLVM 符号前缀化为 `<modpath>__<fn>`（根/主文件函数保持不变）。两类后果均消除——① 本地 `read_file` + `import io` 不再崩溃；② 两模块同名 `helper` 各自正确返回。三重验证 AOT+JIT+memcheck（`test_l009_mangle`）+ json/stdlib 无回归 | — | `docs/plan_l009_mangling.md` |
| L-010 | REPL 跨多条语句传递 has_drop enum/struct 值会崩溃 | 在 `ls repl` 中 `import std.json` 后，跨**不同输入行**对同一类 has_drop 值（如 `JsonValue`）反复调用会析构的函数（`stringify` 等）→ 段错误。`ls run` 跑 `.ls` 文件**完全不受影响**；REPL 内 import 内建模块（math/io）、返回非 has_drop 值的 .ls 模块函数（strconv/path 等）、构造+立即析构 has_drop 值（不跨行）均正常 | 根因：REPL 每条 snippet 是独立 JIT 模块，imported 模块的 has_drop 自动 drop/clone 辅助函数被「strip 成声明跨模块解析」，与 RAII 析构语义在增量 JIT 下交互出错。修法方向：imported 模块在 REPL 只发射一次（declare-only on subsequent snippets），需 codegen 支持「跳过已发射模块 body」模式 | `src/jit.c` `jit_repl` |
| ~~L-011a~~ | ~~struct 含 vec/enum 字段不自动 drop~~ | ✅ 已修复（2026-05-31）：struct `has_drop` 判定纳入 vec/has_drop-enum 字段，auto drop fn 释放 vec 字段（递归 `emit_vec_drop_at`）。memcheck clean，ctest 91/91 | — | `src/codegen.c` / `src/checker.c` |
| ~~L-011b~~ | ~~嵌套 `vec(vec(...))` drop/clone 损坏~~ | ✅ 已修复（2026-05-31）：嵌套 vec 递归 drop + 统一 clone dispatcher（`emit_clone_value`）贯穿 get/first/last/`[i]`/copy/slice/extend/filter/find；vec rvalue 临时实参登记 drop。memcheck clean | — | `src/codegen.c` |
| ~~L-012 ①②~~ | ~~owned-temp enum scrutinee 不析构（含裸 `_` 臂 / 未用绑定）~~ | ✅ 已修复（2026-05-31）：match 检测「拥有的 rvalue 临时 enum 主体」（非 IDENT/借用），对其 has_drop 绑定全部 clone（独立）并把主体注册 temp-drop，于语句末/return 释放；借用主体（`match self`/命名变量）路径不变（零回归，json 等热路径不受影响）。回归测试 `test_cmatrix_t07_match_owned_temp` | — | `src/codegen.c` AST_MATCH |
| ~~L-012 ③~~ | ~~在 match 臂内**直接 `return f(binding)`**（f 返回堆值如 vec）会泄漏~~ | ✅ 已修复（2026-05-31）：根因不在主体 param——AST_RETURN 对「非 IDENT 表达式返回 vec」一律 `emit_vec_clone_val`，但函数/方法调用与 vec 字面量本就产出**独占临时**（无其他 owner、未登记 temp_drop），clone 后原临时被丢弃 → 泄漏。修法：仅对**别名表达式**（`*ptr` / `container[i]` / `obj.field`）clone，对 `AST_CALL`/`AST_ARRAY_LIT` 改为移动（不 clone）。主体 param 仍由正常 scope cleanup 析构。回归测试 `test_cmatrix_t08_match_return_call`；`std/md.ls` `_block_links` 恢复直接返回 | — | `src/codegen.c` AST_RETURN |
| ~~L-011c~~ | ~~vec 尚未完全 first-class（D/F/E）~~ | ✅ 已修复（2026-05-31，分支 `feat/vec-first-class`，ctest 104/104）：**D** Place 引擎——`codegen_lvalue_ptr` 支持 vec/map 索引 + `&!struct` 引用接收者，可变方法在真实地址原地操作并写回，字段赋值 drop 旧+move/clone 新；**F** 统一 `emit_drop_value` 权威，enum payload 嵌套 vec 经它递归释放；**E** vec rvalue 临时实参登记 drop。验收：std.md 升级为 `struct MdDoc { vec(MdBlock) }` + 嵌套 `vec(vec(MdInline))` lists + `vec(vec(string))` table，memcheck clean（JIT+AOT）。**C**（跨模块 type 别名命名）按计划取消——struct MdDoc 本就可跨模块命名。容器值语义测试矩阵见 `tests/samples/cmatrix/` | — | `docs/vec_first_class_plan.md` |

---

## 四、数据解析与文档生成路线图

> 详细设计：`docs/plan_json.md`（JSON）、`docs/plan_md_html.md`（Markdown/HTML）

### 实施顺序

```
Step 0  ✅ 修复 L-006：enum 含 vec/map payload 的 drop   已完成 2026-05-20
   │
Step 1  ✅ JSON 解析 + 输出（std.json）                 已完成 2026-05
   │    JsonValue enum 递归下降 parser + stringify
   │
Step 2  Markdown 解析 + 输出（std.md）                  5-7 天 ← 下一步候选
   │    MdNode 树结构依赖 enum + vec 自递归
   │
Step 3  HTML 解析 + 输出（std.html）                    5-7 天
   │    HtmlNode 树结构同上
   │
Step 4  Markdown ↔ HTML 互转                            2-3 天
        建立在 Step 2 + Step 3 基础上
```

### 三个解析器对比

| | JSON | Markdown | HTML |
|---|---|---|---|
| **工期** | 5-7 天 | 5-7 天 | 5-7 天 |
| **难度** | 低 | 中 | 中 |
| **规范** | 极清晰（RFC 8259） | 模糊（方言多，取 GFM 子集） | 清晰但庞大（取常用子集） |
| **算法** | 递归下降 | 块级分割 + 行内解析 | tokenizer + 标签栈 |
| **核心挑战** | 动态类型容器 `JsonValue` enum | 边界情况多 | 容错（未闭合标签） |
| **编译器前置** | **L-006 修复**（enum 含 vec payload） | 同左 | 同左 |

---

## 五、推荐实施顺序

```
已完成（截至 2026-05-31）
  ├── ✅ L-006：enum 含 vec/map payload drop
  ├── ✅ JSON 解析 + 输出（std.json）
  ├── ✅ 操作符重载（Add/Sub/.../Ord）
  ├── ✅ L-009 + L-009.1：跨模块函数/泛型 mangling
  ├── ✅ 模块命名空间 B-1~B-6（真命名空间 + mod.Type 限定语法）
  └── ✅ struct 深拷贝（L-008）+ BF-040~046 内存修复

近期（1-2 周）—— 推荐二选一
  ├── 【数据格式延续】Markdown 解析 + 输出（std.md，5-7 天）
  │     直接复用 json 验证过的 enum 自递归 + vec 树结构能力，风险低、产出可感知
  └── 【收尾闭包】Block env 深拷贝 Phase G（5-7 天）
        解除 L-007（`Block g = vec[i]` 被 checker 拒绝），补齐闭包系统最后短板

中期（3-6 周）
  ├── HTML 解析 + 输出（std.html，5-7 天）+ Markdown ↔ HTML 互转（2-3 天）
  ├── DWARF 调试信息 D.1（10-14 天）—— 开发体验质变，AOT `ls compile -g`
  └── REPL 改进（多行/补全/历史，5-8 天）

远期
  ├── CSV 解析（3-4 天）
  ├── 生命期系统（21-30 天）—— 解决 L-002 by-ref 闭包逃逸，前置依赖最多
  ├── 并发/线程模型（21-30 天）
  ├── 包管理器 `ls pkg`（14-21 天）
  └── WebAssembly / HPC / GPU
```

### 下一步建议（优先级）

1. **Markdown 模块（std.md）** — 最推荐。数据格式三件套的自然延续，json 已经证明
   `enum + vec 自递归 + 字符串方法` 这条技术路径成熟，无新编译器前置依赖，5-7 天可交付，
   用户可直接感知（文档处理）。
2. **Block env 深拷贝 Phase G** — 若优先补语言完整性而非库生态。这是闭包系统目前唯一
   明显缺口（L-007），影响"把闭包存进容器再取出"这类常见模式。工期同样 5-7 天。
3. **DWARF 调试信息 D.1** — 若想提升开发体验上限。投入更大（10-14 天）但属一次性基础设施，
   后续 VS Code DAP 调试器（D.2）依赖它。

> 倾向 **Markdown（路线明确、低风险、库生态连续）**；若更看重语言内核完整度则选 **Phase G**。
