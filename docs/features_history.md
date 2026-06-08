# LS 已完成特性实现记录

本文档记录各特性的实现细节，供日后参考。最新条目在最前。

---

## Phase 3：拆除内建 `vec(T)` — 2026-06-08

- **目标**：从编译器删除内建 `vec`（`TYPE_VECTOR`）的全部特殊实现，使 `std.vec` 的纯 LS
  `Vec(T)`（has_drop struct）成为唯一动态数组。不可逆，分小步保持「构建通过 + ctest 全绿」可二分。
- **施工蓝图**：[plan_phase3_remove_builtin_vec.md](plan_phase3_remove_builtin_vec.md)。前置：源码层（`std/*.ls`
  + 57 个测试样本）已 100% 迁移到 `Vec(T)`（[vec_replacement_tracking.md](vec_replacement_tracking.md)）。
- **排序原则**：先迁测试消除源码用法 → 前端停收 `vec(` 语法（使内部机制不可达）→ 自后向前删死代码
  （checker→codegen→types）→ 文档。每步删的都是**已不可达**的代码，故每步构建+ctest 绿、可二分。
- **P3-0a**（commit `1323c22`）：迁 `enum_borrow_b_test.ls` 末处真实内建 vec 用法（`vec(Jv)` enum
  payload + `= []`）→ `Vec(Jv)` + `= {}`；清理 `vec_literal_test.ls`/`closure_f7_stress_test.ls`/
  `std/plottl.ls`/`std/vec.ls` 的陈旧 `vec(` 注释。
- **P3-0b**（commit `025dff3`）：重写 `test_mem_m5_neg` 3 个负向样本。**关键定性**：`Vec(T)` by-value
  参数 = **CLONE**，已与所有用户 struct（含泛型）一致，Vec 不是特例；真正的不一致是内建 `vec.push`
  的 **MOVE 特例**。故否决「给 `Vec.push` 加 move 标记」（会破坏泛型 struct 一致性），改把 3 个样本的
  move 载体从「内建容器 move」换成**变量绑定 move**（`string b = s; print(s)` 等，语言里真实且与容器无关
  的 move 路径），断言文案不变。**不改语言语义**。
- **P3-1**（commit `26af24e`）：前端停收 `vec(` 语法。scanner 删 `{"vec",3,TOKEN_VEC}` 关键字 +
  调试名；token.h 删 `TOKEN_VEC`；parser 删 `vec(T)` 类型解析分支、字段名/import 路径段谓词回退
  （`vec` 现走 `TOKEN_IDENTIFIER` → `import std.vec` 仍合法）。新增 `vec(` 报错负向 smoke。写
  `vec(int) v = []` 现报「unknown type」。
- **P3-2**（commit `e5f20be`）：删 checker 26 处 `TYPE_VECTOR` 死分支（方法表、借用白名单、闭包捕获
  谓词、move 分支、字面量推断、for-in）。**保留** `__from_list` 用户容器路径 + 用户 `Vec` 的
  `Iterator(T)` 协议路径（与 vec 共用代码，只删内建 vec 专属分支）。
- **P3-3**（commit `fce7fd0`）：删 codegen 内建 vec 发射（最大块）。删函数体：`codegen_vec_method`
  （~2668 行）、`ls_vec_type`、`emit_vec_clone_val`、`emit_vec_drop_at`、`emit_vec_elem_drop_at`、
  `emit_vec_grow_inline`、`codegen_vec_string_borrow`、`is_vec_string_index`、`emit_global_vec_cleanup`；
  删调用点：`AST_INDEX`/字面量/借用/drop/clone/cleanup/capture/global 的 `TYPE_VECTOR` case。**保留**
  用户 `Vec(T)`（`TYPE_STRUCT`+泛型）/`__from_list`/`Iterator` 脱糖/`emit_struct_*`/map 路径。分
  3E-1/2/3 子步 + 每子步全量 memcheck。
- **P3-4**（commit `9ab2ef0`）：删 types 层 `TYPE_VECTOR` 定义本体——`types.h` 枚举值 + union 的
  `as.vec` 字段 + `type_vector` 声明；`types.c` 的 `type_vector` 构造函数 + `type_clone`/`type_free`/
  `type_equals`/`type_name` 的 `TYPE_VECTOR` 分支（共 5 处）。`grep TYPE_VECTOR/type_vector/TOKEN_VEC
  src/` 零命中。
- **P3-5**：文档收尾（CLAUDE.md §1.2/§7/§8 + 本文件 + tracking + plan_vec_replacement）。
- **结果**：内建 `vec(T)` 从语言彻底移除，`Vec(T)` 为唯一动态数组；字面量 `[..]` 仍走通用
  `AST_ARRAY_LIT`→`checker_tag_user_from_list_literal`→`Vec.__from_list`。ctest 170/170（含 P3-1 负向 smoke）。

---

## match OR-pattern + 整数 switch（bugs/18 修复）— 2026-05-24

- **问题**：`match c { 98 | 102 => {} }` 中 `98 | 102` 被解析为按位 OR（= 110），而非"98 或 102"模式
- **AST**：新增 `AST_MATCH_OR_PATTERN { left, right }` 节点（可嵌套表示 3+ 个备选）
- **Parser**：`prefix_match` 在 `PREC_BITXOR`（高于 `PREC_BITOR`）层级解析每个模式；while 循环收集 `|` 分隔的备选项，组装成右结合 `AST_MATCH_OR_PATTERN` 树
- **Checker**：非枚举 match 模式类型检查改用迭代式栈遍历展开 OR-pattern 树，每个叶节点独立做类型兼容检查
- **Codegen**：两条路径：
  - **(A) 整数 switch**：subject 为整数类型且所有非通配符模式均为整数字面量时，emit 单条 `LLVMBuildSwitch`；`match_collect_int_vals` 展开 OR-pattern 树收集所有 case 值，同一 arm 的多个值全部 `LLVMAddCase` 到同一 body block
  - **(B) CondBr + OR 链**：string/float/含变量模式走原 CondBr 逻辑；OR-pattern 各叶节点各自生成比较，用 `LLVMBuildOr` 串联后再 `CondBr`
- **json.ls 重构**：`_parse_string_raw` 的 8 层深嵌套 if-else 替换为 `match next { 34 => ... 98 | 102 => {} ... }`
- **测试**：`match_or_pattern_test.ls`（17 PASS）；`test_match_or_pattern.cmake` JIT + AOT + memcheck；ctest 54/54

---

## `std.json` 标准库模块（纯 LS 实现）— 2026-05-23

- **架构**：纯 LS 层 `std/json.ls`（无 C 后端），`import std.json as json` 使用
- **JsonValue enum（6 变体）**：`Null / Bool(bool) / Number(f64) / Str(string) / Array(vec(JsonValue)) / Object(vec(string), map(string,JsonValue))`
- **API**：构造（`null_val/bool_val/number/str_val/array_new/object_new`）+ 类型判断 + 访问器（`as_bool/number/string/int` 均返回 `Result`）+ `parse(string)->Result(JsonValue,string)` + `stringify`/`stringify_pretty`
- **导航 API**：`array_len/object_len/object_has/object_keys`；`array_get/object_get` 延迟（Phase H）
- **编译器 bug 修复（BF-020~026）**：map/vec/enum clone/drop 完善；AST_CALL TYPE_STRING 返回值注册 temp（BF-025）；match enum arm 退出前调 emit_scope_cleanup（BF-026）
- **memcheck**：✅ 0 leaks / 0 dfree
- **测试**：json_basic（14 PASS）/ json_infra（5 PASS）/ json_internal + json_file（13 PASS）；ctest 53/53

---

## L-006：enum 含 vec/map payload 的 drop + AOT main 退出码 — 2026-05-20

- **根因 1**：`emit_enum_ctor` 未将源 vec/map cap 置 0（move），scope cleanup 双 free。修复：ctor 为 vec/map payload 添加 move 标记（cap=0）
- **根因 2**：`emit_enum_clone_val` 缺 TYPE_VECTOR/TYPE_MAP，浅拷贝 → double-free。修复：clone 分支加 vec/map 处理
- **根因 3**：`fn main()` 编译为 `void @main()`，CRT 读取 EAX 垃圾值作退出码。修复：检测 main → 改为 `i32 @main()`，`ret i32 0`
- **测试**：`enum_vec_payload_test.ls`；ctest 52/52

---

## `std.time` 标准库模块 — 2026-05-17

- 纯 LS 层 `std/time.ls` + C 后端 `runtime/os_win32.c`（`ls_os_time_*` / `ls_os_sleep_*`）
- **DateTime struct**：10 字段（year/month/day/hour/minute/second/weekday/yday/utcoff/unix_s）
- **函数集**：`now_local/now_utc/now_unix_ns/ms/s`；`format/iso8601/parse`；`add/diff_s/duration_ns/us/ms/s`；`sleep_ms/sleep_us`
- **`import X as Y` 别名语法**：parser 解析 `TOKEN_AS`；AST `import_decl.alias` 字段
- **codegen 两阶段**：Pass A forward-declare 所有模块，Pass B 生成函数体（解决跨模块依赖排序）
- **AOT 静态库**：`ls_os_backend.lib`；JIT AbsoluteSymbols 扩展到 63 个
- **测试**：`time_basic_test.ls`（24 PASS）；ctest 32/32

---

## 闭包 Phase F.7 — Memcheck 压力测试 + 2 个 bug 修复 — 2026-05-14

- **压力测试**：1000 次迭代，S1~S6 六种捕获模式；JIT + AOT memcheck 四重验证
- **Bug 1**：`emit_enum_clone_val` — has_drop enum 函数参数未深拷贝（double-free）。新增完整 clone 函数，AST_CALL 加 `TYPE_ENUM && has_drop` 分支
- **Bug 2**：enum match arm string binder 独立所有权 — `Some(s) => return s` 共享 data 指针 → double-free。修复：binder 调 `emit_string_clone_val` 获得独立所有权
- ctest 30/30；memcheck 0 leaks / 0 dfree

---

## 闭包 Phase F.1 ~ F.6 — 2026-05-14

- **F.1**：`[move v]` 语法，vec/map 显式 by-move 捕获
- **F.2**：Block 赋值 + 移动语义（`type_is_movable` 加 TYPE_BLOCK，赋值后 source env_ptr 置 NULL）
- **F.3**：Block 作为 struct 字段（has_drop 扩展，emit_auto_drop_fn 加 TYPE_BLOCK）
- **F.4**：`vec(Block)` / `map(K,Block)` — push/set 转移 env 所有权；直接下标调用 `vec[i](args)`
- **F.5**：enum capture — 非 has_drop by-copy；has_drop by-move（env_drop 调 `enum.__drop`）；match codegen 终结符 bug 修复
- **F.6**：CG_DEBUG 全面铺开（4 个 helper，`-DLS_CG_DEBUG=ON`）
- ctest 29/29（F.6 后）；memcheck 全程 0 leaks

---

## 闭包 Phase C ~ E — 2026-05-09/10

- **C**：POD 捕获 + 堆 env + RAII（自由变量扫描 → env struct → `cg_emit_alloc("closure.env")` → body 还原）
- **C.5**：string by-move 捕获（env field 0 = drop_fn slot；outer cap=-1 marker；Block 形参 is_borrowed）
- **C.7**：vec/map/struct(has_drop) 捕获（双 ABI：borrow 语义 vs ownership transfer）
- **E.1**：by-ref capture 重构（env 存外层 alloca 指针而非值拷贝，post-capture push 可见）
- **E.2**：closure 参数 vec/map/Block 标 is_borrowed=true
- **E.4**：`array(POD,N)` by-value 捕获（snapshot 语义）
- ctest 23/23（E.1 后）

---

## 闭包 Phase A + B — 2026-05-06

- **A**：type 别名 + `Block(...)->R` 类型语法 + `|x| body` 字面量 + trailing closure 糖 + 强制别名规则
- **B**：无捕获闭包 codegen — lambda lifting（`__closure_N(env, ...)`）+ LsBlock 16B 胖指针 + 间接 call；类型推导从 callee `Block(...)` 形参反推
- ctest 19/19（B 后）

---

## Memcheck Phase A ~ D — 2026-05-03

- **A**：`ls run --memcheck`，open-addressing hash table 跟踪 malloc/free；atexit 报告
- **A.5**：细粒度 site 标签（`cg_emit_alloc(kind, line, col)`），7 类 string/io alloc 路径有名字
- **B**：vec/map/struct/enum 全跟踪 + 5 类真实 bug 修复（enum.box / try 泄漏 / struct double-free / calloc 未追踪 / sum_tree double-free）
- **C**：AOT 集成（`ls_memcheck.lib` 静态库，clang 链接命令注入）
- **D.1/2/3**：调用栈追踪（8 帧 backtrace）+ verbose（`LS_MEMCHECK_VERBOSE=1`）+ strict（`LS_MEMCHECK_STRICT=1` → `_Exit(2)`）
- ctest 9/9（D 后）

---

## 内建 `io` 模块 v1 + v2 — 2026-04~05

- **v1**（7 函数）：`read_file / write_file / exists / open / close / read_all / write`；`OpenMode` enum；`File{object, bool}` struct
- **v2**（6 函数）：`seek / tell / size / rewind / append_file / remove`；`SeekFrom` enum；64-bit positioning（`_fseeki64`）；binary-mode gate

---

## `math` 模块 + 数值隐式扩展 — 2026-04

- **math**：`import math`，23 个函数（sqrt/pow/sin/cos/...）+ 常量（PI/E/TAU/INF/NAN）；LLVM intrinsic 零开销
- **math 多态**：`abs/min/max` 按参数类型自动选 LLVM intrinsic（int/i64/f64）
- **数值隐式扩展（Zig 风）**：`iN→iM`（M≥N）/ `int→i64/f64` / `f32→f64`；`cg_widen()` + `type_widens_to()`

---

## Phase 8 + 8.5：enum + Option/Result + try — 2026-04

- **Phase 8**：tagged union enum（payload variants / 自递归 / has_drop）；`Option(T)` / `Result(T,E)` 按需单态化；match 穷尽性检查
- **Phase 8.5**：`try` 早返操作符（Zig 风，`try Result(T,E)` 要求当前函数返回同 E 类型）

---

## C 风格 for 循环

语法：`for (init; cond; update) { body }`，init 支持变量声明或赋值，三个子句均可为空（`for(;;)` 无限循环）。`continue` 跳转到 update。AST 节点：`AST_FOR_C`。涉及：ast.h/c, parser.c, checker.c, codegen.c。

## foreach / for-in 循环

语法：`for i in 0..10 { }`（range 迭代）、`for i in n { }`（等价 `for i in 0..n`）。`AST_RANGE` 节点，`TOKEN_DOTDOT`。循环变量自动推导为 int，作用域限于循环体。Codegen 基本块：`foreach.cond/body/update/end`。

## break / continue

在 while、for-c、foreach 三种循环中全部支持。`CodegenContext.loop_scope` 记录循环入口作用域，break/continue 前调用 `emit_cleanup_to()` 释放循环体内层 string。

## 固定大小数组 array(T, N)

语法：`array(int, 3) nums = [10, 20, 30]`。LLVM 映射：`[N x T]`。支持：索引读写（GEP2）、`.length`（编译期常量）、for-in 迭代、函数参数传递（按值）、`print(arr)` 输出 `[e0,e1,...]`。类型检查：元素类型一致性、大小匹配、索引必须为整数。

## 全局变量

顶层变量声明，Pass 1 用 `LLVMAddGlobal` 注册，`__ls_global_init()` 生成运行时初始化，main 入口注入调用。跨模块：导入模块的全局变量 forward-declare 为 LLVM external global，在 `__ls_global_init` 中先初始化导入模块。`math.MAGIC` 通过 `AST_FIELD + TYPE_MODULE` 解析。

## LsString 结构体

`LsString = { i8* data, i32 len, i32 cap }`，LLVM named struct `%LsString`。静态字面量 `cap=0`，动态字符串 `cap >= LS_MIN_STR_CAP(16)`，分配策略 `cap = max(16, next_pow2(len+1))`。`+` 拼接展开为 malloc+memcpy，`==`/`!=` 展开为 strcmp，`.length` O(1) 读 struct 第 1 字段。C FFI 调用自动提取 `.data`。

## String Batch 1（查询方法）

`empty()`, `at(i)`, `find(sub)`, `contains(sub)`, `starts_with(prefix)`, `ends_with(suffix)`, `compare(other)`。方法调度：`check_string_method()` + `codegen_string_method()`。依赖 C runtime：`strstr`, `strncmp`。

## String Batch 2（变换方法）

`upper()`, `lower()`, `substr(start, len)`, `trim()`, `replace(old, new)`。均返回新 malloc 字符串。`replace` 通过内嵌 `__ls_str_replace` helper 实现（两趟算法：先计数，再 malloc+拷贝替换）。

## String RAII 自动释放

`emit_scope_cleanup(ctx)`：block 退出前遍历作用域，对 `cap > 0` 的 string 条件分支 free data。赋值时先 free 旧值。`return` 前全量 cleanup（跳过返回值本身避免 use-after-free）。`break/continue` 前清理循环体内层 string。

## impl 隐式 self + static 方法

实例方法：codegen 自动注入首参 `*StructType`，绑定 `self` alloca。调用时编译器自动插入 `&obj` 作为首参。`self.field` 自动解引用。Static 方法：`static fn` 声明，无 self，通过类型名或实例调用（后者忽略 obj）。`TOKEN_STATIC` 关键字。Breaking change：旧语法 `fn method(Struct self, ...)` 不再支持。

## vec(T) 动态数组

内部：`LsVec { ptr data, i32 len, i32 cap }`。方法：`push(v)`, `pop()`, `get(i)`, `set(i,v)`, `len()`, `cap()`, `is_empty()`。自动扩容（cap×2）。RAII 自动 drop。String 元素 push 时触发 move（mark cap=-1）；static string push 不 move。Codegen 生成 `__ls_vec_T_*` helper 函数族。

## map(K, V) 哈希映射

内部：链式哈希表 `LsMap { ptr buckets, i32 len, i32 cap }`，节点 `LsMapNode_KK_VV { i64 hash, K key, V val, ptr next }`。方法 8 个：`set`, `get`, `contains_key`, `remove`, `clear`, `is_empty`, `keys`, `values`。FNV-1a 哈希（string key）。负载因子 >75% 自动 rehash。RAII drop 释放所有节点。String key/value 深拷贝。JIT 模式：`__ls_map_hash_s` 前向声明，从 builtins 模块解析。

## C FFI

架构：`ffi.h/c` 跨平台封装 + codegen 直接生成平台 API 调用（AOT 独立）。`__ls_ffi_init()` 在 main 入口注入。`extern fn` 做类型检查，支持变参 `...`。`lib.call` unsafe，默认返回 i32。自动后缀补全（`.dll`/`.so`/`.dylib`）。JIT 通过 `LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess` 注册进程符号。

## 模块系统

`module name` 声明；`import path` 映射到文件系统；循环导入检测（导入栈）；所有顶层符号默认公开。跨模块变量通过 external global forward-declare。`jit_run_file` 传递 `ModuleRegistry`。
