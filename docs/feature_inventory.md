# LS 功能清单与路线图

> **最后更新**：2026-05-20  
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
| 跨模块全局变量 | 导出/导入全局状态 | — |
| codegen 两阶段 | Pass A forward-declare → Pass B 生成 body，解决跨模块依赖排序 | — |

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

### 11. 测试基础设施

| 项目 | 数量 | 说明 |
|------|------|------|
| ctest 注册测试 | **51 个** | 全部通过（2026-05-20） |
| 单元测试 | `test_scanner` / `test_parser` / `test_types` / `test_codegen` / `test_jit` / `test_ffi` / `test_module` / `test_memory` | C 单元测试 |
| 端到端测试 | 43 个 cmake 驱动 | 每个测试覆盖 JIT + AOT 双路径，部分含 memcheck 验证 |

---

## 二、尚未实现功能

### 优先级排序：收益/工期比

#### ★★★★★ 高收益 · 低工期

| 功能 | 工期 | 风险 | 价值 | 说明 | 参考 |
|------|------|------|------|------|------|
| **操作符重载** | 5–7 天 | 低 | ★★★★☆ | checker 二元运算查 impl 方法 `+`/`-`/`==`/`<` 等；已有 trait 系统可配合 `trait Add` 模式 | `docs/plan_future_ideas.md` §7 |
| **Block env 深拷贝 (Phase G)** | 5–7 天 | 低 | ★★★☆☆ | `Block g = vec[i]` 当前被 checker 拒绝；需合成 `__env_clone_N`，POD+string capture 可克隆 | `docs/block_clone_plan.md` |
| **struct 深拷贝 (Phase H)** | 5–7 天 | 低 | ★★★☆☆ | `MyStruct s = vec_of_struct[i]` has_drop 时 double-free；与 Phase G 共用 clone 基础设施 | `docs/plan_memory_lifetime.md` |

#### ★★★★ 高收益 · 中工期

| 功能 | 工期 | 风险 | 价值 | 说明 | 参考 |
|------|------|------|------|------|------|
| **DWARF 调试信息 (D.1)** | 10–14 天 | 中 | ★★★★☆ | LLVM DIBuilder API emit file/line/column，AOT only，`ls compile -g` | `docs/plan_future_ideas.md` §6 |
| **JSON 模块** | 7–10 天 | 中 | ★★★☆☆ | 递归解析器 + 动态值表示（enum-based AST）；纯 LS 实现 | `docs/plan_stdlib.md` Phase S.5 |
| **REPL 改进** | 5–8 天 | 低 | ★★★☆☆ | 多行输入、tab 补全、持久历史（linenoise/editline） | `docs/plan_future_ideas.md` §8 |
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
| L-008 | struct 不可从容器深拷贝 | has_drop struct 从 vec 取出会 double-free | Phase H | `docs/plan_memory_lifetime.md` |

---

## 四、数据解析与文档生成路线图

> 详细设计：`docs/plan_json.md`（JSON）、`docs/plan_md_html.md`（Markdown/HTML）

### 实施顺序

```
Step 0  ✅ 修复 L-006：enum 含 vec/map payload 的 drop   已完成 2026-05-20
   │    三个根因：(1) ctor 未 move 源 vec/map (2) clone 缺 vec/map
   │    (3) AOT void main 返回垃圾退出码 → 改为 i32 main ret 0
   │
Step 1  JSON 解析 + 输出（std.json）                    5-7 天
   │    核心数据类型 JsonValue enum 依赖 Step 0 ✅
   │
Step 2  Markdown 解析 + 输出（std.md）                  5-7 天
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
近期（1-2 周）
  ├── ✅ L-006 修复：enum 含 vec/map payload（已完成 2026-05-20）
  └── JSON 解析 + 输出（5-7 天）—— 最通用的数据格式，阻塞项已解除

中期（3-6 周）
  ├── Markdown 解析 + 输出（5-7 天）
  ├── HTML 解析 + 输出（5-7 天）
  ├── Markdown ↔ HTML 互转（2-3 天）
  └── 操作符重载（5-7 天）—— 配合已完成的 trait

远期
  ├── CSV 解析（3-4 天）
  ├── DWARF 调试信息 D.1（10-14 天）
  ├── 并发/线程模型（21-30 天）
  ├── 包管理器（14-21 天）
  └── WebAssembly / HPC / GPU
```
