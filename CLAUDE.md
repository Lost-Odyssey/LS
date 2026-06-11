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
- `match` 模式匹配（enum 变体解构 + OR-pattern `1 | 2 =>` + 强制穷尽性检查）
- `enum`（tagged union）：payload 变体、自递归 box、has_drop 自动 drop；`Option(T)` / `Result(T,E)` 模板
- `try` 早返操作符（Zig 风格）
- `for (init; cond; update)` / `for i in 0..10` range 迭代
- `string`（RAII）：20+ 方法；借用 `&string` / `&!string`
- `array(T,N)`、`map(K,V)`；动态数组 `Vec(T)`（纯 LS `std.vec`，含字面量 `[..]`→`__from_list`；内建 `vec(T)` 已于 Phase 3 拆除，不再是语言内建类型）
- `Block(...)->R` 闭包类型；`|x| body` 字面量；trailing closure 糖；完整捕获语义（POD/string/struct/vec/map/enum/Block）
- `print()`、`f"text {expr}"` 格式化字符串
- `module` / `import` / `import X as Y` 模块系统
- C FFI：`lib x = load("foo.dll")` + `extern fn` + `lib.call(...)`
- 全局变量；`object` 类型擦除指针
- 内建 stdlib：`math`（LLVM intrinsic）、`io`（v1/v2）、`std.time`、`std.json`
- 数值隐式扩展（Zig 风，`int→i64/f64` 等）
- `ls run --memcheck`：内存检查器（leak/dfree/ifree + backtrace + verbose/strict 模式）

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

runtime/builtins.c    内建函数运行时
runtime/memcheck.c    内存检查器
runtime/os_win32.c    平台 C 后端（time/sleep/...）
tests/test_*.c        单元测试
tests/samples/*.ls    端到端测试
std/                  纯 LS 标准库（json.ls, time.ls, ...）
docs/                 设计文档
```

---

## 3. 构建与运行

```powershell
# Windows (Visual Studio，推荐)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="C:\llvm\lib\cmake\llvm"
cmake --build build --config Release
```

```
# 使用
ls.exe compile input.ls -o output.exe   # AOT 编译
ls.exe run input.ls                     # JIT 执行
ls.exe run --memcheck input.ls          # JIT + 内存检查
ls.exe repl                             # REPL

# 测试（-j 5 并行留 1 核：~2 分钟 vs 串行 465s，2026-06-12 实测 -j6 205/205 无撞车；
# 测试间无共享文件，新增会写盘的测试时确认文件名唯一或加 RESOURCE_LOCK）
cd build && ctest -j 5 --output-on-failure -C Release
```

> ✅ **AOT flake 已根治**（2026-06-10）：曾经「全量跑随机某个 AOT 测试 fail、单独
> 重跑即过」的间歇失败，**真因不是 Defender**（实测关闭实时监控 + build/ 入排除项
> 仍复现），而是 **AOT exe 输出缓冲未 flush 的 CRT 竞态**：AOT exe 曾**混链 msvcrt +
> ucrt 两个 CRT**（见 [docs/crt_mismatch_bug.md](docs/crt_mismatch_bug.md)），stdout
> 重定向（管道/文件）时全缓冲，进程退出期负责 flush 的 CRT 可能与持有 printf/puts
> 缓冲的 CRT 不同 → **约 15% 的运行整段 stdout 丢失（rc=0、空输出）**。隔离复现：同一
> exe 连跑 200 次约 30 次空输出。
> **真正的修复 = 显式 fflush（exit() 自带的 flush 在此环境对 AOT exe 不可靠，实测同一
> exe 连跑约 10–15% 丢失，单一 CRT 下仍如此）**。两个退出口各补一次 `fflush(NULL)`
> （runtime `__ls_flush_out`，与 print 同 TU 同 CRT）：
> - **正常退出**：codegen 在 AOT entry main 每个 ret 前注入 `__ls_flush_out()`。
> - **panic/abort**：`__ls_proc_exit` 在 `exit(code)` 前 `fflush(NULL)`（vec 越界 / unwrap
>   None / abort 发散，不回 main，绕过上面的注入；此路径也曾 ~10% 丢失）。
>
> 另做一处**正确性清理（非 flake 修复）**：AOT 链接（`src/main.c`）删 `-lmsvcrt`、加
> `/NODEFAULTLIB:libucrt.lib /NODEFAULTLIB:libcmt.lib`，消除 msvcrt+ucrt 混链 → AOT exe
> 仅依赖 `VCRUNTIME140.dll`+`api-ms-win-crt-*`（→ ucrtbase.dll），不再依赖 msvcrt.dll。
> 注意：**去混链本身并不修复输出丢失**（panic 路径在去混链后仍 ~10% 丢，靠 fflush 才归零），
> 它只是消除一个 [docs/crt_mismatch_bug.md](docs/crt_mismatch_bug.md) 记录的潜在风险。
>
> **JIT（`ls run`）从不受影响**（ls.exe 单一 /MD CRT，实测 0/200），故 fflush 注入用
> `ctx->aot_entry` 守 AOT-only。验证：AOT 正常/​panic 各 300×/200× → 0 丢失，全量 ctest
> 188/188。**曾影响真实用户**（任何 AOT 程序输出被管道/重定向都 ~10–15% 丢全部输出）。
> （残留极罕见 flake 另有其因——真正的 Defender 锁刚落盘 .exe 致 compile/delete 偶发失败，
> 与输出无关，`--repeat until-pass:2` 可兜。）

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
| 8 | enum + Option/Result + match 穷尽性 | ✅ |
| 8.5 | `try` 早返操作符 | ✅ |
| 9 | math / io v1+v2 / memcheck A~D / std.time | ✅ |
| 10 | 闭包 Phase A~F（完整捕获 + drop + 移动语义） | ✅ |
| 11 | std.json（纯 LS，递归下降 parser + stringify） | ✅ |
| — | match OR-pattern + 整数 switch（bugs/18） | ✅ |
| — | L-009 跨模块函数名 mangling（`<modpath>__<fn>`） | ✅ |
| — | BF-040 array 元素字段读取 double-drop 修复 | ✅ |
| — | 模块全局变量命名空间（P1-1~P1-4）+ B-1 同名类型冲突检测 | ✅ |
| — | 操作符重载（Add/Sub/Mul/Div/Rem/Eq/Ord，Ruby 风 `fn +`，checker 降级） | ✅ |
| — | REPL 改进：自研行编辑器 + 通用多行 + import 持久化修复（语法高亮暂关；L-010 限制见下） | ✅ |
| — | 容器 drop 修复：struct 含 vec/enum 字段自动 drop（L-011a）+ 嵌套 `vec(vec(...))` drop/clone（L-011b）+ vec rvalue 临时实参 drop（E） | ✅ |
| — | std.md Markdown 模块 Phase A（写：builder + render + fmt，纯 LS，扁平 list/table 规避 L-011c） | ✅ |
| — | std.md Phase B（读：`parse(string)->MdDoc` 块级解析，宽松，round-trip 一致；行内拆分留 Phase C） | ✅ |
| — | vec first-class（分支 `feat/vec-first-class`）：D Place 引擎 + F 统一 `emit_drop_value` + E rvalue 临时 drop；std.md 升级 struct MdDoc + 嵌套 vec 验收（L-011a/b/c）。容器值语义矩阵 `tests/samples/cmatrix/` | ✅ |
| — | std.md Phase C（行内解析：`**bold**`/`_i_`/`` `c` ``/`[t](u)`/`![a](u)` → MdInline，round-trip；`extract_headings`/`extract_links`/`to_plain_text`） | ✅ |
| — | L-012 修复：match 拥有的 rvalue 临时 enum 主体现会析构（含裸 `_` 臂/未用绑定）；借用主体路径不变。`test_cmatrix_t07`。边界 ③（match 臂 `return f(binding)` 返回堆值临时被 clone 后泄漏）已修复：AST_RETURN 仅对别名表达式 clone vec，对 call/字面量改为移动。`test_cmatrix_t08` | ✅ |
| — | **move-elision 优化（Q4）**（2026-06-01）：checker 在真正转移所有权处给源 IDENT 打 `moved_out`；codegen 在 var_decl/assign/field-assign 的 string·struct·enum·vec·map clone 点改为「move + 失效源」（统一 `cg_invalidate_moved_source`），借用源（cap=-2 string 参数等）回退 clone。顺带修 `vec b = a`/`map b = a` 既有 double-free。`test_move_elision`（JIT+AOT 正确性 + memcheck）| ✅ |

| 10-G | **闭包 Phase G：Block env 深拷贝**（2026-06-01，解除 L-007）：`Block g = vec[i]` / `struct.field` / `map.get(k)` 现深拷贝 env——env 布局新增 `clone_fn` 槽（field 1，drop_fn 仍 field 0 不动），每闭包合成 `__env_clone_<id>`（by-ref vec/map 浅拷指针不双释，string/vec/map/struct/enum 经 `emit_*_clone_val` 深拷，POD 值拷）；copy-out 站点经 `cg_emit_block_env_clone` runtime 克隆（NULL env 安全）。checker 放开旧 F.3/F.4A 拒绝（保留 F.2 param-move）。工厂 `fn()->Block` 返回已拥有 env，不克隆。`test_phase_g_closure`（JIT+AOT+memcheck）| ✅ |

| — | **std.html**（纯 LS，HtmlNode 自递归 enum + `vec(Attr)` 属性 + `HtmlDoc{vec roots}` 森林）：H1 生成+render（bottom-up 构造器/escape/void/fmt_tag，commit eb805a8）；H2 递归下降容错解析器（`parse->HtmlDoc`：标签/嵌套/void/自闭合、属性双单无引号+布尔、注释、DOCTYPE、script/style RawText、实体解码命名+数字、错配/EOF 容错）+ 查询 `get_attr`/`to_text`/`extract_links`/`find_by_tag`；`test_std_html_parse`（JIT+AOT+memcheck，20 项）| ✅ |
| — | **std.md Phase H3：Markdown→HTML**（2026-06-02，放 std.md 零依赖 std.html）：`to_html`/`render_html(&MdDoc)`/`to_html_full`，MdBlock/MdInline→HTML 直出（含转义）。`test_md_to_html`（JIT+AOT+memcheck，17 项）。**std.html 全阶段完成** | ✅ |
| — | **BF：has_drop struct 字段所有权**（2026-06-02，commit 993b32c）：① 按值传 struct 实参时 `emit_struct_clone_val` 现深拷贝 vec/map/has_drop-enum 字段（原仅 string/嵌套 struct → 浅拷贝共享 buffer → 双释放）；② 读穿透中间 struct 字段（`o.inner.items.length` 的 `o.inner`）改为 `codegen_lvalue_ptr` 借址 GEP 而非深拷贝临时（消除瞬态泄漏 + __drop 重复触发），终端绑定 `Box b=o.inner` 仍深拷贝。`test_struct_byval_arg`/`test_struct_field_readthrough` | ✅ |
| — | **BF：模块函数内 struct 局部 drop 泄漏**（2026-06-02，commit 08cc07c）：模块函数体在主文件 Pass 2.5（生成 struct 自动 drop fn）**前**发射 → cleanup 时 `drop_fn==NULL` → 落到不释放 vec/map/enum 字段的内联 fallback → 泄漏（main 函数在 Pass 2.5 后发射故无碍；既有 std 模块从不在模块函数内拥有 struct 局部）。修复：drop_fn==NULL 时按需 `emit_auto_drop_fn` 惰性生成（scope cleanup + `emit_struct_drop_cond`/`_separate` 三处）| ✅ |

| — | **enum 借用 Phase A（&Enum 只读借用）**（2026-06-05）：checker 白名单加 `TYPE_ENUM`；codegen `&enum` 参数 pointer ABI；match 借用主体零拷贝路径（borrow 检测 → 直接 GEP 原始指针，box 子树 binder `sym->value=box_ptr` 零拷贝传递，auto-borrow 传调用时直传指针）；auto-borrow call fixup 加 `TYPE_ENUM`。treebench 454× → 1.3×（374 μs vs Rust 288 μs）。`test_enum_borrow`（JIT+AOT+memcheck，8 项）| ✅ |
| — | **enum 借用 Phase B（owned payload 借用绑定）**（2026-06-05）：checker 检测借用 match 主体（`subj_is_enum_borrow`），owned payload binder（string/vec/map/struct/has_drop-enum）标 `is_borrow=true`；codegen string binder 设 `cap=LS_CAP_BORROWED`，vec/map/struct/嵌套 enum binder `sym->value=field_ptr` 零拷贝；`vec[i] → &T` auto-borrow 改用 `codegen_lvalue_ptr` 取元素地址（绕过 `emit_clone_value` 泄漏）；rvalue owned 实参 temp alloca 注册 `cg_push_temp_drop`。`std.json._stringify_impl` 改为 `&JsonValue`。`test_enum_borrow_b`（JIT+AOT+memcheck，10 项）| ✅ |

| — | **for-in 迭代协议（用户容器）**（2026-06-07，[docs/plan_userdef_for_in.md](docs/plan_userdef_for_in.md)）：`for x in v`（v 为纯 LS `Vec(T)` 等 struct）走 `Iterator(T)` 协议——checker 检测 iter 类型为 struct 且其 impl 含 `iter()`/`next()` → `build_foreach_desugar` 在 AST 层合成 `{ Iter __it=v.iter(); while true { match __it.next(){ Some(x)=>{BODY} None=>break } } }`（lvalue 源借址迭代、rvalue 源物化到 `__src` 活过循环），存于 `for_stmt.desugared`，codegen 见之即直发；复用既有 while/match + L-012 成熟 drop 路径。配套 checker 支持 `var_type==NULL` 类型推断局部（仅脱糖器合成 `__it`/`__src` 用）。std/vec.ls 加 `struct VecIter(T)`（裸指针游标，非 has_drop）+ `next(&!self)->Option(T)` + `Vec.iter(&self)->VecIter(T)`。内建 int/array/vec(T) 三段专属 for-in 不变。`test_iter_protocol`（JIT+AOT+memcheck 0/0/0）| ✅ |
| — | **Vec(T) 元素值语义 §008/§009**（2026-06-07，[docs/plan_vec_ownership_drop.md](docs/plan_vec_ownership_drop.md)）：纯 LS `std.vec` Vec(T) 替换内建 vec 的所有权/clone/drop 基础设施。§008 修两处 codegen：① 嵌套 struct 字段 drop——`Vec(Person)` 方法惰性单态化使 `Person.__drop` 可能早于主文件 Pass 2.5 的 `Inner.__drop` 发射，成员 `__drop` 缺失时改为按需 `emit_auto_drop_fn` 惰性生成（原静默跳过 → `Inner.tag` 泄漏）；② 链式读穿透 `v[i].inner.tag` 的中间 owned-clone（`emit_struct_clone_val`）——field-access spill-temp-drop 注册条件加 `AST_FIELD`（进 else 分支即 owned rvalue，注册安全；命名变量链经借址不进此分支）。§009 验证既有 owned-param/move-into-container ABI 对用户方法 `push/insert/set(rvalue)` 已生效，无需改动。`test_vec_owndrop`（JIT+AOT+memcheck 0/0/0）| ✅ |

| — | **Vec(T) 越界检查（bounds checking）**（2026-06-09）：`std.vec` 落地后下标无任何越界检查（`v[100]` 静默踩内存）。引入**三层**安全模型：`v[i]`/`get(i)`/`set(i,x)` **bounds-checked**（越界打印 `Vec index out of bounds: len=N index=I` 后 abort，进程退出码 1），`get!(i)`/`set!(i,x)` **unchecked** 裸 load/store（`!`=unsafe 逃生舱，越界仍 UB）。实现：`std/vec.ls` 把原 `get` 裸读下沉为新 `get!`、新增 `set!`，`get`/`set` 加 `i<0\|\|i>=len` 检查;`__index`/`__index_set` 不动,经既有转调自动继承检查（故 `v[i].foo()` 链式语法零影响——`__index` 仍 `->T`，绝不转调返回 `Option` 的安全 API）。不破坏现有 253 处 `.get` 调用（仍返 `T`）；`Option(T)` 重构留待将来。新增编译器全局 builtin `abort()`（checker 注册 `()->void` + codegen 拦截 → 运行时 `__ls_proc_exit(1)`），解决模块别名 `c` 在泛型方法实例化到消费方时不可见的问题（无需 `import std.c`）。`test_vec_oob`（正向 JIT+AOT+memcheck 0/0/0 + 读/写越界负向 abort 断言）| ✅ |

| — | **后缀 `!` 强制解包（force-unwrap）**（2026-06-10，[docs/plan_container_access_safety.md](docs/plan_container_access_safety.md) §4）：`expr!` 对 `Option(T)`/`Result(T,E)` 强解——成功取 `T`，`None`/`Err` 打印 `[unwrap] line:col: ...` 后 `__ls_proc_exit(1)`。与前缀 `try`（传播）互补。**词法决策**：`!`/`?` 同为**贪婪名字后缀**（`value!`/`empty?` 无参无括号调用保留），解包 `!` 只在表达式以 `)`/`]` 结尾时由 parser 后缀 infix（`PREC_CALL`）触发；裸变量解包写 `(x)!`（scanner **不收紧**，与 `?` 对称、零特例）。**owned 成功类型 move**：`(opt)!` 取出 owned 载荷（string/Vec/Map/has_drop struct·enum）所有权转移给结果——checker 在 `AST_FORCE_UNWRAP` 内联标记源 IDENT `is_moved`/`moved_out`（`type_is_movable` 不含 enum，故复刻借用/static/已move 守卫内联标），codegen 成功臂搬载荷后按 `moved_out` 调 `cg_invalidate_moved_source` 失效源 enum（置 `moved_flag`，scope cleanup 跳过其 drop），统一覆盖各 owned 类型；POD `Option(int)` 不标记可重复解包，rvalue 操作数无命名源保持原样。判别式下标沿用 `AST_TRY` 既有约定（None=0/Some=1、Ok=0/Err=1）。`test_force_unwrap`（JIT+AOT+memcheck 0/0/0，含 owned string/Vec 变量+rvalue、`(a)!;(a)!` use-after-move 负向）| ✅ |

| — | **内存原语移入 std.c（删除裸 builtin）**（2026-06-10，[docs/plan_runtime_primitives.md](docs/plan_runtime_primitives.md)）：`malloc/free/realloc/abort` 不再是编译器注入根作用域的全局 builtin——裸 `malloc(...)` 现报「undefined」。改由 **`std.c`** 提供(extern fn malloc/realloc/free 真绑 CRT + `fn abort`=`__ls_proc_exit(1)`),够到方式:**完整规范路径** `std.c.malloc`(checker `match_stdc_prim` + codegen `cg_match_stdc_prim` 字面结构匹配 `AST_FIELD(AST_FIELD(IDENT"std","c"),{prim})`,复用既有 bare-name lowering:malloc/realloc→CRT+size SExt i64、free→drop+free、abort→`__ls_proc_exit(1)`;**无需通用 registry 解析/跨模块发射**,通用 `mod.fn` 留 B-full)或**别名** `c.malloc`(非泛型模块代码)。**A-1** 加拦截器(两路并存);**A-MIGRATE** 迁 std.vec/std.map→`std.c.*`(交付 bugs/27_vec #1);**A-FLIP**(原子)删 register_builtins 四项 + std.c 加 extern + io/proc→`c.malloc` + 12 用户样本/3 处 test_codegen 内嵌 LS→`std.c.*` + 负向测试。**动因**:施工审计发现裸原语是面向用户的裸堆 API(~15 样本直接用),收进 std.c 后命名空间干净、为未来 allocator/arena 抽象留位(普通函数可换后端,非 intrinsic)。`test_stdc_prim`(直接+泛型体两路 JIT+AOT+memcheck)+`test_malloc_builtin_reject`(裸 malloc 拒绝)。ctest 184/184（受影响测试全绿;全量偶发 Defender AOT 锁 flake——随机 std.c-无关 AOT 测试空 stdout,单跑即过,需 build/ 入 Defender 排除项治本）| ✅ |

| — | **Vec(T) 安全/性能收尾（bugs/27_vec #2 #4）**（2026-06-10）：**#4 越界检查补全**——`get/set/v[i]` 之外,给按下标的 mutator 也加越界 abort 与 get/set 一致:`insert(i)` i∈[0,len]（i==len 追加）、`remove(i)`/`swap(i,j)` i∈[0,len)、`truncate(n)`/`resize(n)` n<0 报错（原 n<0 会从负 slot `__drop_at`）。`get!/set!` 仍是 unchecked 逃生舱。`test_vec_oob` 扩充正向 in-range mutator + 5 个越界负向样本。**#2 sort_by O(n²)→O(n log n)**——旧插入排序换成**自底向上稳定归并排序**（scratch 经 `std.c.malloc`,元素用 `__take` 移动,has_drop 不双释放;比较仍按值克隆操作数=L-006）。否决 heapsort（不稳定,破坏 p3 测试「等长 pear 排在 kiwi/plum 前」的稳定性预期;且 `end` 是保留字）。`test_vec_sort`（大 n 多趟归并 + 显式稳定性 + has_drop string + 空/单元素边界,JIT+AOT+memcheck 0/0/0）。解决 known_limitations L-001。**#3 函数式方法 has_drop 读即克隆**记为 L-006 择期做（需借用 API）。ctest 185/185 | ✅ |

| — | **Map 索引协议 `m[k]` / `m[k]=v`**（2026-06-10，[docs/plan_container_access_safety.md](docs/plan_container_access_safety.md) §6.1）：Map 的 `get`/`remove` 早已返回 `Option`（缺键安全），真实缺口是缺**下标语法**——补上与 Vec `v[i]` 对齐的便捷·响亮访问器。纯 LS，**编译器零改动**（`__index`/`__index_set` 是既有保留方法协议，经 `rewrite_index_to_call`/`make_index_protocol_call` 按方法声明的参数类型派发，故键类型可非 int）：`std/map.ls` 加 `fn __index(&self,K)->V where K:Hash+Eq`（命中返 V 克隆，**缺键 `print("Map key not found")`+`std.c.abort()`**；必须 `->V` 绝不 `->Option` 否则 `m[k].foo()` 链断；缺键路径尾随语句运行时永不执行但仍类型检查通过，同 Vec.get fall-through）+ `fn __index_set(&!self,K,V)=self.set`（插入/更新，缺键不 panic）。`__index` 不进 `generic_method_is_eager`（经 `m[k]` 改写成方法调用触发惰性单态化，同 Vec `__index`）。已知边界：`m[k]=[literal]` 不 coerce array→Vec（`__from_list` 仅 var-decl/字段位触发），需先 `Vec v=[..]` 再赋。`test_map_index`（POD/string/Vec 值 get/set/覆盖/`m[k]`返V可运算+链式`.len()`/与`get()`一致；负向 `map_index_panic` 缺键 abort，JIT+AOT+memcheck 0/0/0）。ctest 186/186 | ✅ |

| — | **Option/Result 组合子 C1（编译器 lower）**（2026-06-10，[docs/plan_container_access_safety.md](docs/plan_container_access_safety.md) §5.4）：七个组合子 `unwrap`/`expect(msg)`/`unwrap_or(fb)`/`is_some?`/`is_none?`/`is_ok?`/`is_err?` 全部**编译器 lower**（同 `try`/`!`，非库方法）——`impl(T) Option(T)` 对 builtin enum 模板不通（spike 验证），泛型自由函数需显式类型参太啰嗦，故 checker 在 method-call 派发**之前**拦截 Option/Result 接收者，把 `recv.METHOD(args)` 原地改写 + 重检。**两类 lower**：会 panic 的 `unwrap`/`expect`→**AST_FORCE_UNWRAP**（`force_unwrap` 加 `message` 字段，expect 用；value-match 发散 abort 臂是 void 被 checker 拒，故不能脱糖成 match）；不 panic 的 `unwrap_or`/`is_*?`→**2-arm match 表达式**（`succ(x)=>x fail(_)=>fb` / `Some(_)/None=>bool`），**复用 match 成熟 drop/move/owned-rvalue（L-012/OPT-001）零新所有权代码**。语义：`unwrap`/`expect`/`unwrap_or` 消费 has_drop 接收者（take-self，owned 载荷 move 出 + 源失效，use-after-move 编译期拒）；`is_*?` 谓词借用（match `_` 通配臂，不 move，调用后仍可用）；`unwrap_or` 的 Result `Err(e)` 失败载荷由 match 臂正常 drop；接收者类型错配/arity 编译期报错。codegen：force-unwrap 失败臂 `message!=NULL` 打印 `[expect] L:C: msg`（`ls_string_data` 取 .data，仅 panic 路径求值），否则默认 `[unwrap]`。改 `src/checker.c`（`lower_opt_combinator`+`optc_mk_*`）/`ast.h`（message 字段）/`ast.c`（free/clone）/`codegen.c`（expect 消息）。`test_opt_combinator`（正向七组合子×Option/Result+owned string+`map.get(k).unwrap_or`+rvalue 链 JIT+AOT+memcheck 0/0/0；负向 unwrap-None abort + use-after-move 编译拒绝）。C2（`map`/`and_then`/`unwrap_or_else`/`ok_or` 带闭包+新类型参）待做。ctest 187/187 | ✅ |

| — | **Option/Result 组合子 C2a（Option↔Result 互转）**（2026-06-10，[docs/plan_container_access_safety.md](docs/plan_container_access_safety.md) §5.5）：C1 之后补三个**无闭包**互转，纯 match 脱糖复用 C1 框架——`res.ok()`（`Result(T,E)→Option(T)` 丢错）、`res.err()`（`→Option(E)` 留错）、`opt.ok_or(e)`（`Option(T)→Result(T,typeof(e))` 配错升级）。头号用例 `try cfg.get(k).ok_or("...")`（Option 一行汇入 Result 传播）+ 与 C1 相乘 `res.ok().unwrap_or(d)`。**关键坑**：脱糖 match 臂体是 bare `Some(x)`/`None`/`Err(e)` ctor，多实例化 + 无期望类型（如链式 `res.ok().unwrap_or(0)` 内层在接收者位）报 `ambiguous variant name`。两处修复：① `lower_opt_combinator` 用 `instantiate_template` 按接收者已知具体 T/E 构造结果类型，重检时压 `c->expected_type`（match case 不清 expected，自然流到臂体）；② **bare-ctor 消歧认 hint**（`disambig_variant_by_hint`，两处歧义点 AST_IDENT/AST_CALL）：报错前先认 `node->resolved_type`（幂等——外层 rewrite 把内层 combinator-match 当 subject 二次重检时复用首解），再认 `c->expected_type`；纯加法，顺带让任意位置歧义 ctor（`Result(int,string) r=Err("x")`）靠期望类型消歧。take-self 消费语义同 C1。`test_opt_combinator` 加 5 项（ok_or 双向/ok 链式/err 双向/ok_or+try 传播，JIT+AOT+memcheck 0/0/0）。C2b（map/and_then/unwrap_or_else 带闭包）待做。ctest 187/187 | ✅ |

| — | **L-013 match 结果所有权整改**（2026-06-10，[docs/plan_match_result_ownership.md](docs/plan_match_result_ownership.md)）：修复 match 臂直接 yield owned 堆载荷时的两类 bug——string `=> binder` 结果泄漏 + has_drop（struct/enum/Vec/Map）`=> 外层局部` double-free。根因统一：消费方只见 `AST_MATCH`，无法判定结果所有权（取决于命中臂 yield 什么），故必须在源头（match codegen）归一。三 helper（`src/codegen.c` AST_MATCH，接线全部 6 个臂体存储点 enum wildcard/变体 + int-switch wildcard/case + cond-chain wildcard/then）：**`cg_match_arm_own_tail`** 令 result 独占其值——**统一 clone 判据**：owned-heap 结果且臂体未产生新临时且非「移出的 binder」且 tail 是拥有堆的 IDENT → clone（一条同覆盖外层局部 + borrow-match binder 须 clone，排除已移出 owned binder + rvalue 临时 转移而非 clone）；**`cg_match_arm_encapsulate`** 臂末转移 tail 临时进 result、释放其余臂内临时（subject drop L-012 + 外层临时保留不误删）；**`cg_register_result_temp`** merge 点**只登记 string** result（消费侧经既有 `count>mark → mark_last_moved → flush(skip_last)` 转移，消除 binder 孤儿泄漏）。**关键否决**：has_drop **不登记**——其消费侧对非-IDENT 初始化器无条件 move，再登记 temp_drop 会 result 既被变量 move 又被 flush drop → double-free（test_enum_has_drop_vec J/K 实测 0xc0000374 据此定位）；has_drop 靠 own_tail 的 clone + encapsulate 转移即可。施工偏差：步骤 2+3 合并（正确性不可分割）、步骤 4（消费侧 transfer）不需要、§5.1 has_drop 登记否决。result_alloca 加 entry-block 零初始化（非穷尽 int-match default 跳 merge 不写 result 时 cap=0 → free 跳过）。`test_match_result_own`（JIT+AOT+memcheck 0/0/0：string binder var-decl/call-arg/return + binder+concat/static 混合 + has_drop struct·enum·Vec 外层局部/载荷移出 + 嵌套 + 丢弃）。解决 known_limitations L-013。ctest 188/188 | ✅ |

| — | **string→Str 下沉 P7 前半：std 模块逐个迁移**（2026-06-11，分支 `feat/string-to-stdlib`，清单 [docs/migration_str_modules.md](docs/migration_str_modules.md)）：B 桥（B-1~B-3 双向互转）就位后逐模块把 std 的 public API/内部从 builtin string 迁到纯 LS `Str`。**已完成 12/20**：hello/ring/vec/hash(+Eq)/time/path/env/fs/proc/io/strconv/plotfmt/plot/regex；os.ls/string.ls/c.ls 重新归类**冻结到 P5**（FFI 边界层收 string 是对的；impl string 本体 P5 整删）。模式定稿：纯 LS 模块全量 Str 重写；FFI 重模块「边界一次转换」（函数头 `string st = text` 过 var_decl 桥补 NUL，内部 string 工作流，结果经桥回 Str）；io 读路径 `Str{data,len,cap}` 零拷贝包 c.malloc buffer、写路径 as_ptr+len。**新增**：`impl Hash for Str`/`impl Eq for Str`（Str 可作 Map 键）、`impl Add for Str`（`+` 重载）、`LS_DEBUG_MODULES`/`LS_DEBUG_TEMPS` 诊断开关。**迁移暴露并修复 7 个编译器 bug**：① **ModuleInfo 悬垂 UAF（重大,非确定性根治）**——import 子检查递归加载使 registry realloc 搬家,持有的 mod 指针 UAF,症状随堆复用随机（unknown type 级联/no export/段错,~20-40%/run）,修=子检查后 module_find 重取；② has_user_clone 前向声明（模块函数克隆 owned Str 浅拷→double-free）；③ emit_struct_drop 模块上下文 drop_fn 未 stamp→内联 fallback 跳裸指针字段静默泄漏（has_user_drop 前向声明）；④ codegen_fn_decl 漏隔离 temp_drop_count（全臂早返 match 泄 temp 进下一函数→dominance 错）；⑤ 链式操作符接收者（lowered AST_BINARY）addr_of 不识→无 drop spill 泄漏；⑥ os_win32 listdir `"%s\*"` 无效转义（list_dir 返回目录自身,陈年 bug）；⑦ io.read_line take/len 顺序颠倒永远 Ok("")+runtime 缓冲改 peek+拷贝消 invalid-free（exec_full 同模式,新增 `*_ptr` 后端 API+jit REG 87-89）。**红线**：Str 绝不直传 extern fn（无 NUL+桥不盖）；backend-malloc 指针绝不让 LS take。**known issues**：`v[i] != "lit"` 操作符操作数误标 moved→用 .eq?()；`import io` vs `import std.io` 双路径双注册。ctest 203/203 | ✅ |

| — | **string→Str 下沉 P7 后半：四大模块收官**（2026-06-12，13-16/20 共 4 提交 fe01ab9/fa0d86b/036a4f2/3d26cae）：plottl(124)/html(85)/md(166)/json(74) 全量 Str 化——**std 真迁移 17/20 完成**，剩 c/os/string.ls 冻结到 P5（解冻先决=Str.c_str()）。**json 变体改名 Str→Text** 根治与 struct Str 的同名冲突（known issue 清账，str_val/as_string 等函数名不变）；html 是自递归 enum+Str 载荷首例；md 用「新旧输出逐字节 diff」验证等价（LS_HOME 环境变量指源树跑新 / build/std 跑旧并行对照）。**语言/库增强**：f-string 可作 `&Str` 实参与 `+` 右操作数（checker P2 触发条件放宽为 `str_target_of_expected`＝gap① 推广到 f-string，rvalue 经既有 struct-arg spill+temp_drop 借用，零新所有权代码）；Str 公开 `copy()`（坑：impl 方法体只能调用**更早定义**的兄弟方法，copy 须放 substr 之后）；Str.to_float 补 e/E 指数解析（json `1e6` 撞出缺口）。**5 个编译器修复（3 陈年 + 2 新引入当场修）**：① **cg_invalidate_moved_source 无 moved_flag 仍 return true（陈年双释）**——struct/enum 分支失效是 no-op 但调用方跳过 clone→owned-rvalue match 主体的零拷贝 binder 赋给外层局部（`Ok(s)=>{outer=s}`）双释 0xC0000374（15 行最小复现），修=无 moved_flag 返 false 落回 clone；② has_drop struct/enum **变量赋值**分支只重置 temp_string_count 不 flush temp_drop（var_decl 早有全量 flush）→循环内 `s=s+f"..."` 的 spill 临时每迭代泄漏（entry alloca 复用，作用域末只释放最后一个）——**坑：不能用 mark 基准 cg_flush_temps 补**（string 计数不动时与语句前注册的 match owned-rvalue 主体同 mark→把主体一并 drop 双释），必须 temp_drop_count 语句前快照（assign_drop_floor）精确 flush；③ f-string 内插 Str 的 drop 白名单只认 CALL/INDEX→**AST_FIELD 终端读克隆**（`f"{e.color}"`）与 **lowered AST_BINARY**（`f"{a+b}"`）的 owned 克隆每次求值泄漏（print 内联 + codegen_format_string 两站点；复现须 owned 值——static Str 克隆零分配掩蔽）。**坑**：部分 cmake 驱动（test_std_json 等）setenv LS_HOME=源树→源 std 编辑立即影响 ctest，中途态须样本同步改齐；每条 PowerShell 是新 shell，忘设 LS_HOME 会静默用 build/std 旧件（勿误判为非确定性）。**剩余**：P4/P5 编译器原子手术（字面量默认翻 Str + 删 141 处 TYPE_STRING + &string ABI 拆除 + Str.c_str() 解冻 c/os + ~298 样本迁移）。std 侧已无阻塞。ctest 203/203 | ✅ |

| — | **string→Str P4+P5-0：&string 拆除 + Str.c_str()**（2026-06-12，commit 32c7fde/fdfc42a，[docs/plan_p5_remove_builtin_string.md](docs/plan_p5_remove_builtin_string.md)）：**P4** 拆用户面 `&string`/`&!string`——checker TYPE_NODE_REFERENCE 白名单删 TYPE_STRING（报错指引 &Str）、`&!x` 表达式操作数限 struct；codegen type_to_llvm TYPE_REFERENCE **统一 pointer ABI**（删唯一 by-value 特例＝只读 &string，`&!string` 本就是 pointer）+ fn 参数注册删 unwrap fallthrough；删 35 个**未注册**遗留样本 borrow_*/mutref_*（M6-0 删 mapref 先例）；std/hash.ls `fxhash_str(&string)→(string)`（plain by-value 本就是 cap=-2 运行时借用，零拷贝不变）；负向 smoke `test_string_borrow_reject`。**LS_CAP_BORROWED 与 plain string 参数借用 ABI 不动（P5-4 才删）**。**P5-0** `Str.c_str(&!self)->*u8`：静态字面量（cap==0&&len>0）零拷贝直返——字面量经 LLVMBuildGlobalStringPtr 发射，.rodata **天然 NUL 结尾**（读码确认）；owned/空/nil-data 走 reserve(len+1)+data[len]=0（len 不变，摊还 O(1)）；解冻 c/os 先决就此解除；`test_str_cstr` 经 CRT strlen 回读五态 0/0/0。**P5 顺序定稿：P5-1 样本迁移先行（实测 296 个/1869 处；A 类＝string 机制专属测试→改写成 Str 版或随 P5-4 删，B 类＝捎带用 string→机械迁移），P5-2 字面量翻默认殿后**——无 expected 的操作数/接收者位（`"lit"==s`、`"a,b".split(",")`）桥不覆盖，先翻必裂。ctest 205/205 | ✅ |

**当前测试**：ctest 205/205（分支 `feat/string-to-stdlib`：string→Str 下沉——P7 std 模块迁移 17/20+3 冻结、P4 &string 拆除、P5-0 Str.c_str() 完成；P5-1 样本批量迁移进行中：试点批 8 样本（e894a7b）+ plot 族批 10 样本（69a7432，含 **std/plot.ls 完整 Str 化**）+ **closure 族批 11 样本**（phase_c5/c7、e1、e2_e4、f1~f5、f7_stress、g）+ **第 14 个编译器修复**：闭包 by-move 捕获的 `Option(Str)` 作 match 主体 → `Some(s)=>{return s}` 臂内早返时 AST_RETURN 对 borrowed has_drop struct/enum binder 走 transfer 而非 clone → 返回别名 + env_drop 双释（0xC0000374；builtin string 靠 cap=BORROWED 幸存）；修复＝`borrowed_string` 泛化为 `borrowed_heap`（borrowed has_drop 返回也深拷，[src/codegen.c](src/codegen.c) AST_RETURN）。**否决**更宽的 bind-站点 binder-clone（破坏自递归 JsonValue，已回退）+ **enum 族批 13 样本**（9f764be：enum_string/e1/var_decl/nested_vec/_repro/has_drop_vec/user_vec/vec_payload/method_basic/static/has_drop/borrow/borrow_b）——**自递归 borrow-match 零修复**（enum_borrow_b 的 Jv=JArr(Vec(Jv))，借用 Str binder 经 f-string 内插返回新 owned 不触发第 14 修）；**新友点**：bare match-expr 臂返回字面量（`match self {Red=>"red"}` 作 method 体）不 plumb expected_type=Str → 须改显式 `{ return "..." }`（第三个无桥位）；static `match s {"red"=>...}` string-switch 对 Str 不适用→if/eq? 链；owned 载荷加 `owned(Str)` helper；见 [docs/plan_p5_remove_builtin_string.md](docs/plan_p5_remove_builtin_string.md)）+ **struct/destructor 族批 14 样本**（d4f6be9：struct_field_defaults×3/byval_arg/field_readthrough/return×2/string_copy/string_e2e/string_field×2 + destructor v3/nested/simple）——**零编译器修复**。实证：字段默认/struct字面量字段/字段赋值/嵌套字段赋值的 literal→Str **自动 coerce**（plumb expected_type=field_type，非无桥位）；`Map(string,int)→Map(Str,int)` 键（Str 已 Hash+Eq）。**新坑（待 P5-2/P6）**：`print(整 struct 含 Str 字段)` 渲染成 `Str{data=指针,len,cap}` 非文本（含指针→非确定），逐字段打印规避，翻默认后普遍撞、需 struct auto-print 识别 Str 字段。**跳过**：struct_assign_test（move 后用源，既存 move-error）/complex_destructor(+v2)（函数内嵌套 fn/struct 定义→codegen no-terminator）/nested_destructor_e2e（无括号单行 if parse 错）——均与 string 正交既存损坏；test_mem_m* 族（A 类 string 机制专属，P5-4 删候选）+ **io/fs/map 族批 11 样本**（ceb37ee：io/fs/loop 4＝fs_test/string_loop_test/io_readfile_test/io_step_test + map 族 7＝map_test/map_keys/map_literal_test/map_iter_test/map_index_test/map_compose_test/map_option_payload_test）——**零编译器修复**。**批选逻辑**：聚焦 string 在可桥接位（var_decl/struct 字段/enum payload/call-arg）的样本；string 方法测试族（test/parse/batch3/utils）大量是裸字面量接收者 `"x".method()` 无桥位 → 留 P5-2 翻默认自动迁移，本批不碰。**新实证**：`Map(string,*)→Map(Str,*)` 键 + `m.set("lit",v)`/`get_si(m,"lit")` 字面量参数（方法位 & 自由函数位）均自动 coerce 到 Str；`m[k] == "lit"` 索引结果比较 → `m[k].eq?("lit")`；`e.key.length`/`v.length`→`.len()`；自递归 has_drop enum `JV=Obj(Map(Str,JV))` 的 OPTPAY 回归零 escape-clone。**A 类跳过**（P5-4 删候选）：string_test(LsString 机制 e2e)/string_ord_test(Str 无 Ord impl)/string_append_test(builtin += 优化)/string_memory_e2e/test_mem_m*。**发现** `tests/test_fs.cmake` 未在 CMakeLists 注册（孤儿驱动），fs_test 实为未注册，手动验证 ALL PASS + memcheck 0/0/0。+ **impl Ord for Str + string_ord 迁移**（4784cea）：Str 补 `impl Ord for Str { fn <(...)=compare(...)<0 }`（`> <= >=` 编译器自动派生），P5-2 翻默认前置（`"a"<"b"`/`Vec(Str).sort()`/`min_ord(T:Ord)(Str)` 届时变 Str 比较）；string_ord_test 随之解除 A 类阻塞迁 Str（裸字面量操作数 `"a"<"b"` 落 Str 局部）。+ **map_owndrop/strconv/json 族批 3 样本**（aa12c21：map_owndrop_test/strconv_test/json_file_test）——**零编译器修复**。map_owndrop＝has_drop K/V 所有权（Map(Str,*)/Map(*,Str)/嵌套+struct 字段，f-string 键值经 set/get/remove/rehash 全程 0/0/0）；strconv/json＝FFI/库消费方，全 `Str x = mod.fn(...)` var-decl + `.compare("lit")` 字面量 coerce + json 大数组 `big=big+","`/`big=big+f"{idx}"`（Str+字面量/f-string 拼接经 impl Add）。**A 类跳过新增**：glue_funcs_test(FFI string↔C ABI to_cstr/from_cstr/extern strlen(string)，红线禁 Str 直传 extern)/to_string_test(builtin 全局 to_string/from_* 自身待 P5)/vec_string_test(裸字面量接收者 `"x".upper()` 重灾，留 P5-2)。+ **Option/Result 族批 3 样本**（5bfc522：option_result_test/try_basic_test/force_unwrap_test，均未注册 diff+memcheck 0/0/0）——零编译器修复。`Result(int,string)→Result(int,Str)`/`Option(string)→Option(Str)`/`Err("..")`·`Some("..")` payload 字面量 coerce/force-unwrap owned Str move（`Str su=(os)!`）/`.length`→`.len()`/`"FAIL: "+name`→f-string。**撤回不纳入**：opt_combinator_test（撞真缺口——编译器 lower 的组合子 arg 字面量 `ok_or("missing")`/`unwrap_or("clean")` 不 plumb expected_type=Str → 默认 string 与 Str 载荷不符，同裸字面量接收者留 P5-2）；regex_test（先撤回——**发现预存非确定性 bug**，下条已修）。+ **修 std.regex 非确定 bug + 假绿根治 + regex_test 迁 Str**（cb6f914）：根因＝std/regex.ls 构建返回 Vec(Str)/Map(Str,Str) 时 `string X=st.substr(); push/set(X)` 走 **B-2 call-arg string→Str 桥进入移动型消费者**（Vec.push/Map.set owned-param ABI 移入容器）→ 所有权双重处理 UAF（非确定比较失败）+ find 的 `string m; Str ms=m` IDENT 中间变量克隆源未释放（16字节泄漏）。修＝7 处全改模块内直接 `Str`（var_decl/rvalue 桥，避开进移动消费者；FFI 边界仍 string）。**latent 编译器 bug 记账**：B-2 string→Str call-arg 桥 + 移动消费者不安全（任何 `vec_of_str.push(string_var)` 撞），P5 删 string 后消失。**假绿根治**：test_regex.cmake 原只 grep "ALL PASS"、无 memcheck、不查 FAIL → 加 FAIL 否决断言 + memcheck 0/0/0 断言（教训：审计其它 std 测试驱动同类假绿）。+ **vec/stack 族批 6 样本**（42cb407：vec_batch_b/c/d/e 未注册 + vec_sort_test/stack_test 注册）——零编译器修复。`Vec(string)→Vec(Str)`/`Stack(string)→Stack(Str)`，push/insert/has?/index_of/resize 字面量参数 coerce，`v[i]==lit`/`top==lit`→`.eq?()`。**实测新 impl Ord for Str**：vec_sort/vec_batch_e 的 `Vec(Str).sort()` + sort_by 闭包 `a<b` 走新 Ord 稳定排序 0/0/0。区别于 regex 撞的「string 变量 push 进 Vec(Str)」（B-2 桥进移动消费者 UAF），本批元素全程 Str + 字面量直接 coerce 无中转。+ **vec-functional/rawvec 原语族批 21 样本 + rawvec_* 7 样本改名**（06deade）——零编译器修复。**改名（历史遗留清理）**：std/rawvec.ls 库本体早已被 std.vec 取代删除；7 个内容已是 `import std.vec` 的样本 rawvec_{api,functional_p3,kid_lazy,kid_missing_eq_fail,m2,map_reduce,parity_p1}→vec_*（与注册名 test_vec_* 对齐，6 个 .cmake 驱动同步）；手写原语 gate（m1/move/poc/ptr_index/realloc/sizeof）保留 rawvec_ 名（测 sizeof/realloc/*T 下标/__drop_at 编译器原语，是 std 容器地基，非死代码）。**迁移**：vec_functional_v1~v5（闭包 `s.len()`/`.eq?()`）+ vec_api/m2/kid×2/parity_p1/functional_p3/map_reduce + rawvec_m1/move/poc/ptr_index/sizeof + inferred_init/implicit_empty_init/fn_default_params/trait_basic（未注册 diff 基线）。**新实证（本批新 territory）**：`*Str` 裸缓冲容器全套内存安全（`std.c.realloc`+`sizeof(Str)`+`__drop_at(slot)`+`p[i]` 深拷经 Str.__clone+`__move`，rawvec_m1 三层嵌套 0/0/0）；方法级泛型 `map(Str)`/`reduce(Str)` 闭包体 f-string/字段读（`|x| f"v={x}"`/`|p| p.name`）plumb expected 正常；**默认参数值字面量位** `Str sep = "-"` 自动 coerce；trait 方法签名 `-> Str` + 受约束泛型返回 Str；`Map(Str,int)` 无 init（M-DEF 合成 `{}`）。坑：`f"" + s`（f-string 作 + 接收者位）不支持→改 `f"{s}"`。**defer P5-2 新增**：vec_owndrop/vec_global_drop/deep_copy_all/vec_literal/hash_test（`"x".upper()`/`"x".hash()` 裸字面量接收者作测试主体）。+ **P5-2 预研：实验开关 `LS_STR_DEFAULT=1` 翻字面量默认**（[docs/plan_p5_remove_builtin_string.md](docs/plan_p5_remove_builtin_string.md) §5.1）：4 文件 env 守卫（ast.c 根程序注入 `import std.str`、main.c/jit.c 四入口、checker AST_STRING_LIT/AST_FORMAT_STRING 默认翻 Str 仅 `expected==string` 保 builtin），默认 OFF 零行为变化。**ON 态实测：205→50 失败全部编译期 type error，零运行期失败**（零坏值/零 memcheck/零段错），翻转破坏面 100% 响亮。错误谱 270 条：150 compare + 40 init + 34 field（机械迁移）+ 19 builtin 方法 arg（不补 checker，样本迁完自然消失）+ 7 string-switch。**四个已记账缺口自动消解**：裸字面量接收者 `"x,y".split(",")`/组合子参数 `unwrap_or("fb")`·`ok_or("e")`/bare match 臂字面量/字面量 `+` 拼接（impl Add）。施工顺序定稿（§5.1）：定向收尾批（ON 错误清单驱动，双态验证）→ 12 机制测试改写 → A 类处置 → 正式翻转（prelude 注入+REPL+struct auto-print 识别 Str，保 `LS_STR_DEFAULT=0` 逃生舱）。+ **P5-2 Step 1：定向收尾批①~④ 共 29 样本**（4 提交）——ON 态失败 51→23（**OFF 态全程 205/205 零回归**，每批双态验证）。**批①** 9 样本（比较→`.eq?`/类型→`Str`/补 `import std.str`）：i64_literal/stdlib_path/modvar_access/hasdrop/owned_return/stack_qual/fstring_spec/shortcircuit_temp/ring。**批②** 11 样本（struct 字段/enum 载荷/泛型实参/`Vec(Str)` 元素，含 modstructlit 的 opt.ls 模块迁移）：generics_g1/g2/match_result_own/hash/bf044/bf046/mod_struct_literal/vec_owndrop/iter_protocol/global_vec_lit/move_elision。**批③** 6 样本：match_or_pattern（string-switch→if/eq? 链）/vec_get_unsafe/vec_oob/vec_global_drop/string_parse/bf045_string_param + **Str.to_int/to_i64 补 `0x` 十六进制 parity**（注释明言"matches the old builtin"，string_parse 撞缺口；**坑：`Str.to_bool` 故意严格只认 true/false**——曾补 yes/no/1/0 parity 却撞 str_methods3 断言「yes→Err」，已回退）。**批④** 5 样本：opt_combinator（组合子 `Result(_,Str)`；**`try o.ok_or("e")` / 链式 `.unwrap_or("fb").eq?()` 位不 plumb expected→Str 载荷与 string 字面量不符，须 Str 局部强制**，var_decl 位则自动 plumb）/modtype_memcheck（跨模块 mod_a/b 迁移）/html_write/md_parse（`.contains`→`.contains?`）。**机械规则三类无桥位**（须 Str 局部/eq? 规避）：①裸字面量接收者（`"42".to_int()`→`Str s="42"; s.to_int()`，string_parse/string_utils）②多参泛型字面量实参（`make_pair(int,Str)(99,"w")`→局部 w）③组合子 arg 在 try/链式位。**判 A 类不迁**（留 Step 2/3/P5-4）：impl_string（测 `impl string` builtin 扩展特性；`impl Str` 对导入结构体符号不带模块前缀→"Symbols not found"，需编译器修）+ string_utils（builtin std.string `to_bool` 宽松语义 yes/no/1/0，Str 故意严格不复刻）+ glue_funcs（FFI 红线禁 Str 传 extern，但仅裸 `"lit".to_cstr()` 受 flip 影响→pin string 局部屏蔽，已迁过）。**剩 23 ON 失败＝12 str_* 桥机制自测（Step 2 改写）+ 8 memcheck A 类 + impl_string + string_utils + (m5_aot 偶发)**，全 builtin-string 机制专属。+ **P5-2 Step 2：12 个 str_*/桥机制测试改写双态绿**（3 提交）——ON 态失败 **23→9**（OFF 态 205/205 零回归）。这 12 个**与 B 类样本本质不同**：故意保留 `string` 类型值验证 string↔Str 双向桥，**不能无脑全迁 Str**（会删桥覆盖）。**批①** str_p0/p1/p2/p3：`X.to_string() == "lit"`（string LHS，被测的 Str→string 桥）→ 加 `fn seq(string,string)->bool { return a==b }` 助手，字面量经 string 参数位 coerce 回 builtin（保 to_string + string 相等覆盖，双态绿）。**批②** str_methods/methods2/lit_borrow/fstring_interp：`.to_string() == "lit"` → `.eq?("lit")`（无需捕获 LHS 更稳健，LHS 是 Str rvalue；坑：含方法实参空格的 LHS 用 seq-wrap 会捕获错位，故用 .eq?；to_string 覆盖由 str_p* 保留）；str_fstring_interp 的 `string bs = f"..."` 测 builtin f-string，钉 `string bs_exp` 局部保 builtin 比较。**批③** str_bridge_arg/ret/rev/s2s（最微妙）：Str-LHS `.to_string()==`→`.eq?`；**string-LHS 桥源/产物比较**（`sv=="hello"` 测桥后源存活、`a=="hello"` 测反向桥产物是 builtin string）→ seq(string,string) 保 string==string；**builtin string `ov.append("amic")`/`f + "!"` 的字面量实参**钉 string 局部（flip 下字面量变 Str → `string.append(Str)` 报错）。**红线守住**：各测试原本测的桥方向（B-1/B-2/B-2b/B-3）的桥调用本身不改，只改断言侧。**零编译器/零库修复**，全 memcheck 0/0/0。**剩 9 ON 失败＝7 memcheck A 类（mem_m3/m4/m4_5×2/overhaul×2/memcheck_edge）+ impl_string + string_utils**，全 builtin-string 机制专属，留 Step 3（A 类处置：改 Str 版或随 P5-4 删）。+ **P5-2 Step 3：A 类处置——ON 态实质归零**（2 提交）——ON 失败 **9→0 真失败**（残 2 个 memcheck_aot/mem_overhaul_aot 是已知 AOT Defender flake，`--repeat until-pass:2` 即过；OFF 态 205/205）。**两路处置**：① **5 个 memcheck 测试迁 Str**（mem_m3/m4/m4_5/overhaul/memcheck_edge）——经审计它们是**通用 has_drop 内存检查器测试**（用 string 作 owned 载荷测 leak/double-free），非 builtin-string 内部机制专属 → `string→Str` 语义等价迁移（脚本批量类型位替换 + `.length`→`.len()`；m3 的 `enum Value{Str(string)}` 变体名 `Str`→`Text` 避与类型 Str 同名，仿 json 先例）；builtin string 借用 ABI（cap=-2）覆盖仍由 str_bridge_* 四测试保留，**净覆盖不丢**；双态+memcheck 0/0/0。② **impl_string + string_utils 钉 escape-hatch**（`set_tests_properties(... ENVIRONMENT "LS_STR_DEFAULT=0")`）——它们测 frozen builtin-string 特性（`impl string` 扩展 / std.string 宽松 `to_bool` yes/no/1/0），Str 无等价（`impl Str` 对导入结构体符号缺模块前缀 / Str.to_bool 故意严格），故**保留 builtin-string 模式**直到 P5-4 随 builtin string 删除。ctest ENVIRONMENT 属性设入子进程环境覆盖继承的全局 flip，使其在默认翻转后仍测 builtin（实测全局 `LS_STR_DEFAULT=1` 下两测试通过）。**Step 1+2+3 累计：ON 态 51→0 真失败，OFF 态全程 205/205 零回归**；P5-2 翻转前样本/测试侧已全部就绪，剩 Step 4 正式翻转（默认 ON + prelude 注入 + REPL 接线 + struct auto-print 识别 Str）。前序 `feat/match-result-ownership`：L-013 match 结果所有权整改；`feat/opt-combinators-c1`：Option/Result 组合子 C1+C2a；`feat/map-index-protocol`：Map 索引协议）

> **std.map M-6：拆除内建 `map`**（2026-06-09，[docs/plan_m6_remove_builtin_map.md](docs/plan_m6_remove_builtin_map.md)，对标 Phase 3 拆 vec）：从编译器删除内建 `map(K,V)`（`TYPE_MAP` + codegen 分离链接实现），纯 LS `Map(K,V)` 成为唯一哈希表。立项依据：`benchmarks/mapbench` 显示 std.map（AOT）build 比内建快 ~3.6×、lookup 持平或更快，且 ≥ C++ `unordered_map` / Rust 默认 HashMap。**M6-0** 前置清理消费方：删 12 个 `mapref_*`（内建 `&map` 借用诊断，用户 `&Map` 诊断属未设计特性）+ 注册；`closure_phase_c7` 改为「Map by-move 捕获 + 返回值观测」、`map_iter.each()` 改为回调内校验键值对（内建 map 是唯一 by-ref 捕获容器,拆除后用全局/返回观测替代）；`std/html.ls`/`test_mem_m4_matrix` 仅注释/已是 Map。**M6-1** 前端停收 `map(`（scanner 删 `TOKEN_MAP` 关键字 → `map` 退化 IDENTIFIER 故 `import std.map`/`Map`/`.map()` 方法均合法；parser 删类型解析 + `prefix_map_lit` 的 `->` 分支，新增 `map(` 报错负向 smoke `map_builtin_syntax_reject`）。**M6-2** 删 checker ~20 处 `TYPE_MAP` 分支（保留 `__from_pairs`/`Iterator` 用户容器路径）。**M6-3** 删 codegen 内建 map 发射（`codegen_map_method` ~2700 行 + `emit_map_helpers_for`/`ls_map_type`/`map_type_id`/`__ls_map_*`，分子步 + 每步全量 memcheck）。**M6-4** 删 types 层 `TYPE_MAP` 本体（枚举 + union `as.map` + `type_map` + clone/free/print/equals 分支）。`grep TYPE_MAP/TOKEN_MAP/type_map/codegen_map_method/__ls_map_/ls_map_type src/` 零命中；`Map(K,V)`/`import std.map`/`.map()`/`{k:v}` 字面量全部正常；json/嵌套 Map memcheck 0/0/0。ctest 180/180。
>
> **（M-6 之前的链，CLAUDE 头未单列）**：M-5 把 ~25 个样本 + `std.regex`/`std.env`/`std.json` 迁到 `Map`（内建 map 曾并存）；M-5 期间修 4 个真 bug——**M5-003**（菱形 import 重复注册 Hash impl → trait-impl 注册去重前移）、**OPT-001**（owned-rvalue `Option` match 主体双释 → `emit_enum_drop` 幂等清零 slot）、**M5-004**（`Option(经容器递归 enum)` 漏生成 `__drop` → checker 加 `has_drop` 不动点传播 pass）、**M5-002**（`Block(&T)` 引用参数借用 ABI 三处协同修复）。详见 docs/plan_std_map.md §13。

> **std.map M-4：组合验证**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §10）：新增 `test_map_compose`，覆盖 `Map` 作为 struct 字段（`Holder h = {}` 零初始化后 `.set` grow）、全局变量（程序结束自动 `__drop`）、enum payload（`Cfg.Table(Map(string,int))`）和嵌套泛型 `Map(string, Map(int,int))`（按 v1 限制 imperative 构造，非嵌套 map 字面量）。`test_map_compose` 跑 JIT、`run --memcheck`（SUMMARY 0 leak / 0 double-free / 0 invalid free）和 AOT。未改 `std/map.ls` 或编译器；验证期间发现更深的 `Option(Map)` 绑定后继续调用内层方法会触发双释，未纳入 M-4 范围，样例采用已 clean 的嵌套长度读取路径。

> **std.map M-LIT：map 字面量 `{ k: v, ... }`**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §F1，前端特性）：`Map(K,V) m = {"a":1,"b":2}`。**parser** `prefix_map_lit` pair 分隔符接受 `:`（除既有 `->`），都建 `AST_MAP_LIT`；`{ IDENT : ... }` 仍是匿名 struct 字面量（故 map 键用非标识符表达式 string/int/…）。**checker** `checker_tag_user_from_pairs_literal`（仿 from_list）：LHS struct 且有 `__from_pairs(&!self,K,V)` → 逐对校验 K/V + 递归路由嵌套容器字面量；`AST_VAR_DECL` 加分支。**关键**：`__from_pairs` 须加入 `generic_method_is_eager`（否则惰性单态化不触发 → 符号缺失 → 静默零初始化）。**codegen** var_decl `TYPE_STRUCT+AST_MAP_LIT+pairs>0` → 零初始化 + 逐对 `__from_pairs(k,v)`，键值经 `cg_litelem_string_own` 走 owned-param ABI（仿 from_list）。std.map 加 `fn __from_pairs … { self.set(k,v) }`。v1 未做：嵌套 map 字面量做值（`{"g":{1:10}}`，需 AST_MAP_LIT 作通用表达式 codegen）；嵌套 Vec 值 `[..]` 已支持。`test_map_literal`（JIT+AOT+memcheck 0/0/0：string/int 键、POD/string/Vec 值、空 `{}`、尾逗号、匿名 struct 字面量回归）。

> **std.map M-3：迭代 API**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §7）：`struct Entry(K,V){ K key; V val }`（has_drop 随字段自动派生）+ `struct MapIter(K,V){ *u8 ctrl; *K keys; *V vals; int cap; int i }`（裸指针借用、非 has_drop，仿 VecIter）。`Map.iter()→MapIter`、`keys()→Vec(K)`/`values()→Vec(V)`（收集 clone）、`each(Block(K,V) f)`、`for e in m`（既有 Iterator 协议脱糖，产 `Entry`，用 `e.key`/`e.val`；Q4 不做 `for (k,v)` 解构）。`MapIter.next→Option(Entry)` 跳空槽。for-in 的 `__it.next()` 是拥有的 rvalue `Option(Entry)`，依赖 M-2 修好的 match owned-rvalue 双 drop（has_drop Entry 才不双释放）。`test_map_iter`（JIT+AOT+memcheck 0/0/0：POD/string 键/`Vec` 值，for-in/keys/values/each/空表）。

> **std.map M-2：has_drop K/V 所有权 + match 双 drop 修复**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §8）：`Map(K,V)` 支持 has_drop 键/值（string、`Vec`、嵌套 `Map`）。**`_insert_no_grow` 重写为 forward-shift**：原 Robin Hood「换出住户 + `k=rk` 续携」被 move checker 拒（循环内 move 后重赋 k）；改为 ① 只读扫描分类目标槽 ② 占用则整段前移腾位（`__take`+store，PSL+1）③ 仅终点对 k/v 各 move 一次（覆盖分支 `__drop_at` 旧值 + move 新值，旧键保留、新 k RAII drop）——只搬槽内条目、绝不循环内重赋 k。**编译器通用修复**：`match f(){Some(x)=>…}`（拥有的 rvalue enum 主体）此前 merge 块**显式 `emit_enum_drop`** 与 L-012 **`cg_push_temp_drop`** 两条析构机制都触发 → 双 drop；string 因 free 幂等（cap 清零）侥幸无恙，**Vec/Map 等 `__drop` 不清 cap 的 payload 双重释放**。修法：显式 drop 后 `cg_remove_temp_drop` 摘除主体（fall-through 只 drop 一次；早 return 臂仍由 temp-drop 表兜底）。`test_map_owndrop`（JIT+AOT+memcheck 0/0/0：string 键 200 条 rehash、string/Vec/嵌套 Map 值、覆盖、remove、clear、Map 作字段 auto-drop）。grow/rehash 已在 M-0。

> **std.map M-1：`remove`（backward-shift）+ `clear`**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §5.4）：`remove(&!self,K)->Option(V)`——`_find` 定位后 `__take` 出 val 返回、`__drop_at` 键，随后**回挪压缩**（无墓碑）：从删除槽起逐个把后继 entry 前移一格并 PSL 减 1，直到后继为空或已在 home（PSL 0），维持 Robin Hood 不变量长期不退化。`clear(&!self)`——逐槽 `__drop_at` 后置 EMPTY、`len=0`、保留 buffer（无 Hash/Eq bound）。`test_map_basic` 扩充：remove 返回值/缺键 None/删后 len/邻居仍可读/再插入，及「插 300 删全偶留全奇」backward-shift 压力（JIT+AOT+memcheck 0/0/0）。grow/rehash 已在 M-0；M-1 余下仅负载因子（沿用 7/8）。

> **std.map M-0：纯 LS `Map(K,V)` 核心**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §4-6）：新增 `std/map.ls`——开放寻址 + Robin Hood + Fibonacci 散射的哈希表，替换内建 `map`（本轮并存）。**SoA 布局** `struct Map(K,V){ *u8 ctrl; *K keys; *V vals; int len; int cap; int shift }`（`*K`/`*V` 同 vec 的 `*T`，绕开嵌套泛型指针风险）；`ctrl[i]` 1 字节存 PSL 或 255=EMPTY；`home=(h*0x9E37…)>>shift` 取高位。方法（POD K/V）：构造 `={}`、`set`（Robin Hood 回填 + 覆盖 drop 旧值）、`get→Option(V)`（命中 clone）、`has?`、`len/cap/empty?`、`_grow` 翻倍 rehash（负载因子 7/8，M-1 的 grow 已并入）、`__clone`（按布局深拷不 rehash，无 Hash/Eq bound）、`__drop`。施工三坑：① **行首 `*` 续接歧义**（`*K p` 被并到上行当乘法）→ 复用单 `*u8 z=nil` 当三 buffer 的 malloc + 连续 `*X old=self.X` 逐行加 `;`；② `while (cast)!=x` 解析失败 → 用临时变量；③ **跨模块 trait-bound 传递**（user→std.map→std.hash 传递依赖，M-H 仅解决直接 import）→ checker 加递归 `propagate_imported_traits`（提取 `register_one_imported_trait_decl`，对导入模块的 `AST_IMPORT_DECL` 递归 + `visited` 防环），否则 `Map(int,int).set` 单态化时 `where K: Hash` 对 int 不满足。`test_map_basic`（JIT+AOT+memcheck 0/0/0，含 500 条 grow/rehash + 覆盖）。

> **std.map M-H：`Hash` trait + FxHash**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §3，std.map 哈希前置）：新增 `std/hash.ls`——`trait Hash { fn hash(&self) -> u64 }` + `impl Hash for int/i64/char/bool/string`，FxHash 原语 `fx_mix(h,word)=rotl(h^word,5)*0x517cc1b727220a95`、`fxhash_str`（逐字节 `at_unsafe`）。**FxHash 低位弱**，分桶取高位（Map §5.1 Fibonacci 散射），勿 `% buckets`。`u64` 移位量须同宽（写 `x << (5 as u64)`）。**关键补洞：跨模块 trait-impl 可见性**——导入模块的 `trait` 与 `impl Trait for T`（含内建 T）此前对导入方完全不可见。① **checker** import 前向传递新增 `AST_TRAIT_DECL`（→`check_trait_decl`，`find_trait` 去重）与 `AST_IMPL_TRAIT_DECL` case：方法注册进 `impl_registry`（`x.hash()` 派发）+ 记录 `trait_impls` 对（`where K: Hash` 满足），内建目标 key 用裸名；② **codegen** `codegen_impl_trait_decl` 加 `is_builtin_impl` 判定：内建目标方法符号不加模块前缀（发 `int.hash` 而非 `std_hash__int.hash`），消除 JIT「Symbols not found」。`test_hash`（JIT+AOT+memcheck 0/0/0，正向 10 项 + 分布 + 泛型 `where T: Hash` 派发）+ `hash_neg_test`（缺 impl 编译期拒绝）。

> **std.map M-H：`Hash` trait + FxHash**（2026-06-09，[docs/plan_std_map.md](docs/plan_std_map.md) §3，std.map 哈希前置）：新增 `std/hash.ls`——`trait Hash { fn hash(&self) -> u64 }` + `impl Hash for int/i64/char/bool/string`，FxHash 原语 `fx_mix(h,word)=rotl(h^word,5)*0x517cc1b727220a95`、`fxhash_str`（逐字节 `at_unsafe`）。**FxHash 低位弱**，分桶取高位（Map §5.1 Fibonacci 散射），勿 `% buckets`。`u64` 移位量须同宽（写 `x << (5 as u64)`）。**关键补洞：跨模块 trait-impl 可见性**——导入模块的 `trait` 与 `impl Trait for T`（含内建 T）此前对导入方完全不可见。① **checker** import 前向传递新增 `AST_TRAIT_DECL`（→`check_trait_decl`，`find_trait` 去重）与 `AST_IMPL_TRAIT_DECL` case：方法注册进 `impl_registry`（`x.hash()` 派发）+ 记录 `trait_impls` 对（`where K: Hash` 满足），内建目标 key 用裸名；② **codegen** `codegen_impl_trait_decl` 加 `is_builtin_impl` 判定：内建目标方法符号不加模块前缀（发 `int.hash` 而非 `std_hash__int.hash`），消除 JIT「Symbols not found」。`test_hash`（JIT+AOT+memcheck 0/0/0，正向 10 项 + 分布 + 泛型 `where T: Hash` 派发）+ `hash_neg_test`（缺 impl 编译期拒绝）。

> **std.map M-DEF：隐式空/默认初始化**（2026-06-08，[docs/plan_std_map.md](docs/plan_std_map.md) §F2/M-DEF，std.map 首阶段前端前置）：`T v`（无初始化器）等效 `T v = {}`，前提是该类型 `= {}` 本就合法（用户容器 `Vec`/`Map`、`= {}` 零初始化 struct、内建 `map`）。**parser** `starts_var_decl` 放宽泛型/限定泛型两分支的 var-name 后随判定——除 `=`/`;`/EOF 外再接受 `}`（块尾）与「后随 token 与 var-name 不同行」（换行终结的无 init 声明）；同行 var-name 守卫保留，`print(a1) print(a2)` 仍解析为表达式语句。**checker** `AST_VAR_DECL` 在 `init==NULL && declared∈{TYPE_STRUCT,TYPE_MAP}` 时合成空 `AST_MAP_LIT`，复用既有 `= {}` 路径（struct→零初始化 `AST_NEW_EXPR`；map→空 map）；POD/string/enum 不合成（其 `{}` 非法），无 init 行为不变。codegen 无改动。`test_implicit_empty_init`（JIT+AOT+memcheck 0/0/0）。

> **Phase 3：拆除内建 `vec(T)`**（2026-06-08，[docs/plan_phase3_remove_builtin_vec.md](docs/plan_phase3_remove_builtin_vec.md)）：从编译器删除内建 `vec`（`TYPE_VECTOR`）全部特殊实现，`std.vec` 的纯 LS `Vec(T)` 成为唯一动态数组（不可逆，分小步保持「构建+ctest 绿」可二分）。**P3-0a** 迁 `enum_borrow_b_test.ls` 末处真实 `vec(Jv)`→`Vec(Jv)` + 清陈旧注释；**P3-0b** 重写 `test_mem_m5_neg`（move 载体从「内建 vec push-move 特例」换成「变量绑定 move」，不改语言语义——否决「给 `Vec.push` 加 move 标记」以保泛型 struct 一致性）；**P3-1** 前端停收 `vec(` 语法（scanner 删 `TOKEN_VEC` 关键字、parser 删类型解析分支，`vec` 现为 IDENTIFIER 故 `import std.vec` 仍合法，新增 `vec(` 报错负向 smoke）；**P3-2** 删 checker 26 处 `TYPE_VECTOR` 死分支（保留 `__from_list`/`Iterator` 用户容器路径）；**P3-3** 删 codegen 内建 vec 发射（`codegen_vec_method` ~2668 行 + `emit_vec_*`/`ls_vec_type`/`codegen_vec_string_borrow`/`is_vec_string_index`/`emit_global_vec_cleanup`，分 3E-1/2/3 子步 + 全量 memcheck）；**P3-4** 删 types 层 `TYPE_VECTOR` 定义本体（枚举 + union 字段 + `type_vector` + 4 处 switch 分支）；**P3-5** 文档收尾。字面量 `[..]` 仍走通用 `AST_ARRAY_LIT`→`checker_tag_user_from_list_literal`→`Vec.__from_list`。`grep TYPE_VECTOR/type_vector/TOKEN_VEC src/` 零命中。ctest 170/170（含 P3-1 负向 smoke）。

> **Phase 2.5：`impl` 内建类型（扩展方法）+ string 方法下沉**（2026-06-07，[docs/plan_impl_builtin_types.md](docs/plan_impl_builtin_types.md)）：新增语言特性「`impl string { ... }`」——parser 接受 `TOKEN_TYPE_STRING` 为 impl 目标（拒绝 `impl(T) string`）；checker `check_impl_decl` 用 `resolve_builtin_type_by_name` 注册 `self_type=string`，`impl_key_of_type(TYPE_STRING)="string"`（裸名全局，不模块前缀），存 `impl_registry["string"]`；调用解析「内建优先→回退用户 impl」（`check_string_method` 未命中置 `string_no_builtin_match` 标志，调用点回退 Step 11 复用 struct 方法校验，无 import 时清晰报错「did you forget `import std.string`」）；codegen 复用既有 Step 11 builtin-impl dispatch + `&string` by-value self ABI。三处通用修复：① import 路径段接受 `TOKEN_TYPE_STRING`（`import std.string`）；② pointer-self 方法的 rvalue 接收者（`"a,b".split(",")` 字面量）在 `codegen_addr_of` 失败时求值 spill 到 alloca（仅 `&self` 只读安全）；③ builtin-impl 方法符号跨模块前向声明（`std.string` 可能晚于调用方模块发射，按 `callee->resolved_type` 前向声明、body 后续复用）。`split`/`lines`/`chars`/`join` 迁出编译器到纯 LS `std/string.ls` 的 `impl string`（返回 `Vec(T)`），删除 checker + codegen 内建分支。迁移 `string_loop`/`string_utils`/`str_split_*`/`string_batch3`/`vec_get_test` + `std.md`/`std.plottl`（后两者用 boundary 拷贝进内建 vec + 显式 `clear()`/`shrink_to_fit()` 绕行 VR-LIM-002：导入模块函数内纯 LS Vec 局部不自动 drop）。`test_impl_string`（JIT+AOT+memcheck 0/0/0，24 项）。Phase 3（拆除内建 vec）前置完成。

> **方法级泛型 map/reduce**（2026-06-07）：`impl(T) RawVec(T)` 新增 `fn map(U)(&self, Block(T)->U f) -> RawVec(U)` 与 `fn reduce(U)(&self, U init, Block(U,T)->U f) -> U`（类型参 `U` 属方法而非 impl 块）。checker `try_instantiate_method_level_generic` 在调用点解析方法级类型实参→克隆方法 AST→带别名检查体→排队 codegen（三 bug 已修：① 克隆节点须置 `impl_struct_name` 否则 `is_instance_method=false` → self ABI 错位；② `&self` 作用域类型须注册裸 `Struct` 而非 `*Struct`，否则 `is_ptr_deref` 误 load；③ `type_function` 接管 `params` 所有权，调用后勿 free → UAF）。codegen 两修：① `codegen_addr_of` 新增 `AST_CALL` 分支——rvalue 接收者求值后 spill 到 alloca 并注册 has_drop 清理，支持链式调用 self；② `codegen_closure_literal` 隔离 `temp_drop_count`/`temp_block_env_count`（原仅隔离 `temp_string_count`）——否则父函数在闭包字面量前注册的 rvalue temp drop 会漏进闭包体 flush，引用别函数 alloca（LLVM "instruction does not dominate all uses"）。`test_rawvec_map_reduce`（JIT+AOT+memcheck，25 项 + 链式）

> ⚠️ **REPL 已知限制 L-010**：`ls repl` 中跨多条输入行对同一类 has_drop enum/struct 值（如 `import std.json` 的 `JsonValue`）反复调用会析构的函数（`stringify` 等）→ 段错误。`ls run` 跑 `.ls` 文件不受影响。根因：每条 REPL snippet 是独立 JIT 模块，imported 模块 drop/clone 辅助被跨模块 strip 与 RAII 析构交互出错。修法方向：imported 模块在 REPL 只发射一次。详见 [docs/feature_inventory.md](docs/feature_inventory.md) 三、L-010。

> 各阶段实现细节见 [docs/features_history.md](docs/features_history.md)

---

## 5. 编码规范（必须遵守）

### 5.1 命名规范

- 函数：`module_verb_noun`（如 `scanner_next_token`, `codegen_emit_binary`）
- 结构体：`PascalCase`；枚举值：`UPPER_SNAKE_CASE`；宏：`UPPER_SNAKE_CASE`；局部变量：`snake_case`
- 指针星号靠近类型：`int *ptr`（不是 `int* ptr`）

### 5.2 内存管理

- 所有 `malloc` 必须检查返回值；谁分配谁释放
- AST 节点用 `ast_free` 递归释放；LLVM 对象用 `LLVMDispose*` 释放
- 新特性开发期间用 `ls run --memcheck` 验证 0 leaks / 0 dfree

### 5.3 错误处理

- Scanner 错误 → `TOKEN_ERROR`（含行号列号）
- Parser 错误 → `had_error = true`，同步到语句边界后继续
- 类型错误一次性收集（最多 20 条），格式：`[error_type] file:line:col: message`

### 5.4 测试要求

每个新特性必须有：单元测试（`test_*.c`）+ `.ls` 端到端测试（AOT + JIT + memcheck 三重验证）。

```c
#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); exit(1); } \
} while(0)
```

### 5.5 CG_DEBUG 内存跟踪（必须遵守）

`CG_DEBUG` 定义在 `src/common.h`（默认 `0`）。启用：`cmake ... -DLS_CG_DEBUG=ON`。

**规则**：
1. 每个新的自动内存分配/释放/clone 点必须加 `#if CG_DEBUG` 块
2. 使用 `cg_emit_debug_printf(ctx, fmt, args, nargs)` 接口
3. 不要在 `#if CG_DEBUG` 块外调用 `cg_emit_debug_printf`
4. `common.h` 是 `CG_DEBUG` 的唯一权威来源

---

## 6. 待实现特性

- 内建 `io` v3+：`read_line` / `flush` / 目录列举
- `File` 自动 drop（当前需手动 `io.close`）
- 内建 stdlib 扩展：`fs` / `string` / `env` 模块
- `vec.get` / `map.get` 改返回 `Option(T)`（breaking change）
- 用户自定义泛型（`struct LinkedList(T)`）
- 借用作为返回类型 / 变量声明 / struct 字段（需生命期系统）
- ~~**Phase G**：Block env 深拷贝~~ ✅ 已完成（2026-06-01）：`Block g = ns[i]` / `struct.field` / `map.get(k)` 深拷贝 env（env 加 `clone_fn` 槽 + 每闭包 `__env_clone_<id>`），解除 L-007。见阶段表 10-G。
- ~~**Phase H**：struct 深拷贝~~ ✅ 已完成（验证 2026-05-29）：`MyStruct b = vec_of_struct[i]` 对 has_drop struct 自动深拷贝，memcheck clean（含嵌套 struct + 函数返回 vec）
- ~~**L-009**：跨模块函数名 LLVM mangling~~ ✅ 已完成（2026-05-29）：模块自由函数符号前缀化 `<modpath>__<fn>`，消除同名崩溃/静默错值；根/主文件函数不变。
- **L-009.1**：跨模块同名 mangling 收尾 → [docs/plan_l009_mangling.md](docs/plan_l009_mangling.md) §6
  - ~~6.A 模块泛型~~ ✅ 已完成（2026-05-30）：修 A1（模块泛型实例化丢弃→连单模块都不可用）+ A2（同名不同体泛型跨模块静默错值），符号 `<mod>__fn(args)`
- ~~**模块命名空间收尾 Part 1**（全局变量）~~ ✅ 已完成（2026-05-30）：模块全局变量符号前缀化（`<mod>__var`）+ scope 注册 + has_drop 全局 cleanup；`mod.VAR` 跨模块访问；ctest 76/76。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step P1-1~P1-4
- ~~**模块命名空间收尾 B-1**（B-safe struct/enum 冲突检测）~~ ✅ 已完成（2026-05-30）：同名 struct/enum 来自多个模块 → 清晰 checker 错误 "multiple imported modules"，消除 GEP 崩溃/静默损坏。ctest 76/76。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-1
- ~~**模块命名空间收尾 B-2**（struct/enum LLVM 类型名前缀化）~~ ✅ 已完成（2026-05-30）：`Type.strukt/enom.llvm_name` 字段存前缀名，codegen 所有 LLVM 命名点用前缀名，checker 注册表仍用裸名（零破坏）。ctest 79/79。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-2
- ~~**模块命名空间收尾 B-3**（impl 方法/drop/clone 名跟随前缀）~~ ✅ 已完成（2026-05-30）：`codegen_impl_decl`/`codegen_impl_trait_decl` 在 `current_emit_module != NULL` 时前缀化 struct 名，静态方法调用按 `llvm_name` 解析。ctest 79/79。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-3
- **模块命名空间收尾 B-4~B-6**（B-full 真命名空间）待做：类型位置限定语法 `mod.Type`（B-4）+ enum 跨模块变体（B-5）+ 综合 memcheck（B-6）。→ [docs/plan_module_namespace.md](docs/plan_module_namespace.md) Step B-4~B-6
- 正则表达式 builtin；f16 半精度浮点

> 已完成特性的详细实现记录见 [docs/features_history.md](docs/features_history.md)

---

## 7. 所有权与 Move 语义（摘要）

**LsString 三态**：`cap==0` Static（不 free）/ `cap>0` Owned（退出作用域 free）/ `cap==-1` Moved（跳过 free）

**核心规则**：变量一旦在任何路径被 move → 视为死亡，不可再用。`LIVE → MAYBE_MOVED → MOVED`（单调不可逆）

**借用（ABI 要点）**：
- `&string` by-value（cap=0 标记）；其他 `&T` / `&!T` 全部 pointer
- 只读借用支持 auto-borrow；可写借用必须显式 `f(&!x)`
- 支持类型：`string / map(K,V) / struct`（含纯 LS `Vec(T)`，按 struct 走 `&Vec`/`&!Vec` pointer ABI）；仅用于函数参数位置

**Move 类型**：`string` / `struct(has_drop)`（含 `Vec(T)`）/ `map(K,V)` / `Block(...)`
> ⚠️ `Vec(T)` 是 has_drop struct：**变量绑定 `b = a` 移动**，但 **by-value 参数 = clone**（与所有用户 struct 一致，非内建 vec 的 push-move 特例）。

> 完整借用规则表、运行时保护、实现计划见 [docs/ownership.md](docs/ownership.md)

> ⚠️ **改 `match` codegen（含脱糖到 match 的特性）前必读** [docs/match_codegen_guide.md](docs/match_codegen_guide.md)——6 个臂体存储点 / 三 helper（own_tail·encapsulate·register_result_temp）/ 坑位目录（has_drop 不可登记 temp_drop、subject drop 误删、多臂污染、binder 孤儿、borrow-match 零拷贝）。`match` 是历史上反复出 drop bug 的路径（L-012/OPT-001/BF-026/029/L-013）。

---

## 8. 闭包捕获策略（摘要）

| 捕获类型 | 策略 | outer 变量 |
|----------|------|------------|
| `int / f64 / bool / char / *T / object` | **by-copy** | 保持 live |
| `array(POD, N)` | **by-copy** | 保持 live（snapshot） |
| `string` | **by-move** | 标 MOVED（cap = −1） |
| `struct(has_drop)`（含 `Vec(T)`） | **by-move** | 标 MOVED（moved_flag） |
| `map(K,V)` | **by-ref** | 保持 live，可继续 set |
| `has_drop enum` | **by-move** | 标 MOVED（moved_flag） |
| `[move v]` 显式 | **by-move** | 标 MOVED（map 专用） |

⚠️ **by-ref 捕获的 map 闭包不能 outlive 外层变量**（编译器不检查，用户自行保证）。工厂函数返回 map 捕获的闭包会产生悬垂指针。string/struct（含 `Vec(T)`）使用 by-move 正是为了避免这个问题。

> 详细设计、悬垂风险示例、未来演进路径见 [docs/closures_plan.md](docs/closures_plan.md)
