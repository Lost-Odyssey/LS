# LS Memcheck 模式 — 完整实施计划

> 本文档是独立的实施指南，可在新 session 中直接参考执行。
> 核心设计决策同步至 `CLAUDE.md`（待 Phase A 完成后补入）。

---

## 一、背景与目标

### 1.1 为什么需要 memcheck

LS 当前内存模型涵盖：
- `string` 三态（cap=0 静态 / cap>0 owned / cap=-1 moved）
- `vec(T)` / `map(K,V)` 堆缓冲 + RAII drop
- `struct` 含 `__drop` 析构（自动 + 用户）
- enum 自递归 box（`Tree { Node(int, Tree, Tree) }`）+ has_drop 自动 drop
- `&T` / `&!T` 借用（不参与 cleanup）

但目前没有任何工具能验证这些路径**真的没泄漏 / 没 double-free**。CLAUDE.md 已知未覆盖：
- enum payload 含 vec / map 的 drop（明确未做）
- enum binder 的 move 跟踪（Phase B 未做）

未列出但**强烈怀疑**有问题的场景：
- `try` 早返失败路径与 inner Result 临时槽的交互
- `break` / `continue` 出循环时 inner scope 的 cleanup 范围
- 自递归 enum 深层 drop 链
- `&!self` 方法里给 string 字段重新赋值（旧值是否 free）
- f-string 多嵌入表达式的临时 string 释放
- `vec` 整体 move 后元素 drop 路径
- `map.get` 返回的 string 是 alias 还是 clone

### 1.2 设计目标

`ls run --memcheck file.ls` 跑完后产出**LS 源码级**的报告：

```
=== LS memcheck report ===
LEAK     32 bytes   tests/foo.ls:15:7   string.upper
LEAK    256 bytes   tests/foo.ls:88:3   vec.grow
DOUBLE FREE         tests/foo.ls:42:9   string.scope_drop
  originally allocated at tests/foo.ls:30:5 (string.concat)
  first freed at      tests/foo.ls:35:5 (string.scope_drop)
INVALID FREE        tests/foo.ls:55:2   ptr never allocated
SUMMARY: 2 leaks (288 bytes), 1 double-free, 1 invalid free
=== end ===
```

错误消息**映射回 LS 源码行号 + 操作种类**，比 ASan 的 IR 行号有用得多。

### 1.3 与 ASan 的关系

互补，不冲突：
- `--memcheck`：LS-aware 的源码级报告，能区分"设计意图的 leak"与真 leak
- `-fsanitize=address`（未来集成）：抓 buffer-overflow / stack-use-after-return 等 C 层深坑
- 同时启用没意义但不会冲突

---

## 二、整体架构

```
┌──────────────────────────────────────────────────┐
│  codegen.c   (memcheck_enabled == true)          │
│   ↓                                              │
│  cg_emit_alloc(size, kind, line, col)            │
│  cg_emit_free(ptr, kind, line, col)              │
│   ↓                                              │
│  call ptr  @ls_mc_alloc(i64 sz, ptr @site)       │
│  call void @ls_mc_free(ptr p, ptr @site)         │
└────────────────┬─────────────────────────────────┘
                 │
                 ↓
┌──────────────────────────────────────────────────┐
│  runtime/memcheck.c                              │
│   ↓                                              │
│  open-addressing hash table:                     │
│    ptr → { size, alloc_site, free_site, freed }  │
│  atexit(ls_mc_report)                            │
│   ↓                                              │
│  ls_mc_alloc:  malloc + insert                   │
│  ls_mc_free:   lookup + check + free + mark      │
│  ls_mc_report: 遍历表，未 freed 的全部 LEAK     │
└──────────────────────────────────────────────────┘
```

## 三、SiteInfo（call site 元信息）

每个 alloc / free call site 在 IR 里有一个静态全局：

```c
typedef struct {
    const char *file;   /* "tests/foo.ls" */
    int         line;
    int         col;
    const char *kind;   /* "string.upper" | "vec.grow" | "io.slurp" | ... */
} SiteInfo;
```

codegen 在 emit 时去重：相同 (file, line, col, kind) 共用同一个 global。每个 site ~32 字节。10000 个 site = 320 KB，无所谓。

### 3.1 kind 标签命名约定

按 codegen 各处 malloc/free 的语义命名：

**alloc 侧：**
| 来源 | kind 标签 |
|---|---|
| `s.upper()` / `s.lower()` / `s.trim()` 等返回 owned string | `string.upper` / `string.lower` / `string.trim` |
| `s = a + b` 字符串拼接 | `string.concat` |
| `f"text {x}"` 格式化 | `string.fstring` |
| `s = "lit".copy()` 静态字面量克隆 | `string.copy` |
| `s.clone()` | `string.clone` |
| `vec.push` 触发增长 | `vec.grow` |
| `vec(T) v = [1,2,3]` literal | `vec.literal` |
| `vec.copy` / `vec.slice` | `vec.copy` |
| `map.set` 节点分配 | `map.node` |
| `map` 桶数组初始化 / rehash | `map.bucket` |
| 自递归 enum box | `enum.box.<EnumName>` |
| `io.read_file` 文件 slurp 缓冲 | `io.slurp` |
| `io.read_all` 同上 | `io.slurp` |
| 未标 | `unknown` |

**free 侧（与 alloc 侧独立）：**
| 来源 | kind 标签 |
|---|---|
| 作用域退出 cleanup 释放 string data | `string.scope_drop` |
| vec 元素 drop 释放 string data | `string.vec_elem_drop` |
| struct 字段 drop 释放 string data | `string.struct_field_drop` |
| 变量重新赋值前释放旧值 | `string.reassign` |
| 临时 string 表达式释放 | `string.temp` |
| 作用域 cleanup 释放 vec.data | `vec.scope_drop` |
| 作用域 cleanup 释放 map.buckets/nodes | `map.scope_drop` |
| enum drop 释放 box | `enum.scope_drop` |
| 用户 `__drop` 内 free | `user.drop:<StructName>` |
| 未标（wrapper 兜底） | `unknown` |

> 所有 free site 也要标 kind，这样 double-free 报告能给两个位置（alloc + 第一次 free 的种类）。

---

## 四、Phase A：JIT-only 核心 ✅ 已完成

### 4.1 已完成项

- [x] runtime/memcheck.c 实现完成（267 行，含 hash table + alloc/free/report + realloc + Windows dllexport）
- [x] CMakeLists 把 memcheck.c 加入 ls 主目标
- [x] codegen.h 新增 `memcheck_enabled` 字段及 `mc_sites` 缓存
- [x] codegen.c 新增 cg_make_site / cg_emit_alloc / cg_emit_free
- [x] cg_install_memcheck_wrappers（透明拦截 malloc/free/realloc → ls_mc_*）
- [x] string 路径 alloc 端替换完成（cg_emit_alloc 用于 string.upper/lower/trim/substr/concat/fstring/clone/copy）
- [x] io.slurp 路径 alloc 端替换完成
- [x] main.c 接受 `--memcheck` flag（run + compile 两个子命令）
- [x] JIT 符号注册（jit.c AbsoluteSymbols: ls_mc_alloc/realloc/free/report）
- [x] memcheck_phase_a.ls 跑出 ✓ clean
- [x] memcheck_kinds.ls 跑出 ✓ clean
- [x] 全 ctest 通过（memcheck 默认关闭，不影响）

### 4.2 Phase A 实测发现的关键缺口

**`cg_emit_free` 从未被调用！** — 所有 free 路径（emit_string_free、vec/map/enum drop、scope cleanup）都直接使用 `LLVMGetNamedFunction("free")`，经 wrapper 走到 `ls_mc_free` 但使用默认 site（kind="unknown", line=0, col=0）。这意味着所有 free 端的报告都是 `unknown`，无法精确定位。

### 4.3 memcheck_edge.ls 实测发现的 4 类 Bug（2026-05-01）

跑 `ls run --memcheck memcheck_edge.ls` 结果：
- **19 leaks** (4510 bytes)
- **1 double-free**
- **1 invalid free**

详见下方 Phase A.6。

---

## 五、Phase A.5：完成 free 路径替换（当前）

### 5.1 目标

将所有 free 路径从原始 `free()` 调用替换为 `cg_emit_free(ctx, ptr, kind, line, col)`，确保 memcheck 报告能精确定位每个 free 操作。

### 5.2 核心改动：emit_string_free 加 kind 参数

当前签名：
```c
static void emit_string_free(CodegenContext *ctx, LLVMValueRef str_alloca);
```

改为：
```c
static void emit_string_free(CodegenContext *ctx, LLVMValueRef str_alloca,
                             const char *kind, int line, int col);
```

内部用 `cg_emit_free(ctx, data_ptr, kind, line, col)` 替代直接的 `LLVMBuildCall2(free_fn)`。

### 5.3 所有调用点需要更新

| 调用位置 | 当前调用 | 新 kind | line/col |
|---|---|---|---|
| 变量赋值前释放旧值 | `emit_string_free(...)` | `string.reassign` | CG_LINE/CG_COL |
| scope cleanup (block) | `emit_string_free(...)` | `string.scope_drop` | 变量声明的 line/col |
| vec 元素 drop | `emit_string_free(ctx, elem_ptr)` | `string.vec_elem_drop` | CG_LINE/CG_COL |
| struct 字段 drop | `emit_string_free(ctx, field_ptr)` | `string.struct_field_drop` | struct 定义的 line/col |
| 临时 string 释放 | `emit_string_free(ctx, temp_slots[i])` | `string.temp` | 表达式 line/col |
| break/continue 前 cleanup | `emit_string_free(...)` | `string.scope_drop` | 循环行号 |

### 5.4 非 string 的 free 路径替换

以下 raw `free()` 调用也需要替换为 `cg_emit_free`：

| 位置 | kind |
|---|---|
| vec.data free (scope cleanup) | `vec.scope_drop` |
| vec grow 时 free 旧 buffer | `vec.grow` |
| map.buckets free | `map.scope_drop` |
| map nodes free (单个/批量) | `map.scope_drop` |
| enum box free | `enum.scope_drop` |

### 5.5 验收标准

- [ ] `emit_string_free` 签名改为接收 kind/line/col 参数
- [ ] 所有调用点传入正确的 kind 标签
- [ ] vec/map/enum 的 raw free() 替换为 cg_emit_free
- [ ] memcheck_phase_a.ls 仍 ✓ clean
- [ ] memcheck_edge.ls 报告中不再有 kind="unknown" 的项（bug 产生的 leak 除外）
- [ ] 全 ctest 通过

---

## 六、Phase A.6：修复 memcheck 发现的 4 类内存 Bug

> 以下 Bug 由 `memcheck_edge.ls --memcheck` 于 2026-05-01 发现，均为真实内存错误。

### Bug 1 — try 早返回路径 string 泄漏（4 处，共 4144 bytes）

**报告**：
```
LEAK  4096 bytes  memcheck_edge.ls:28:15  (string.fstring)
LEAK    16 bytes  memcheck_edge.ls:33:17  (string.concat)
LEAK    16 bytes  memcheck_edge.ls:28:22  (string.clone)
LEAK    16 bytes  memcheck_edge.ls:27:43  (string.upper)
```

**涉及代码**：
```ls
fn try_inner(int x) -> Result(string, string) {
    if x < 0 { return Err("negative".upper()) }       // line 27
    return Ok(f"got {x}")                               // line 28
}
fn try_chain(int x) -> Result(string, string) {
    string s = try try_inner(x)                         // line 32
    return Ok(s + "!")                                  // line 33
}
```

**根因**：`try expr` 展开时，Result 内部的 string payload 没有被转移所有权：
- `try_inner` 返回 `Ok(f"got 7")` — fstring buffer (4096) + clone (16) 未释放
- `try_chain` 返回 `Ok(s + "!")` — concat 结果 (16) 未释放
- `try_inner` 返回 `Err("negative".upper())` — upper 结果 (16) 未释放

**修复方向**：在 try 展开的 codegen 中，从 Result payload 提取值后释放 Result 槽位内的 string。

### Bug 2 — struct 构造期间提前 free 导致 double-free

**报告**：
```
DOUBLE FREE  memcheck_edge.ls:0:0  (unknown)  ptr=F580
  originally allocated at memcheck_edge.ls:27:27 (string.clone)
  first freed at         memcheck_edge.ls:0:0 (unknown)
```

**事件序列**：
```
[cg] str.free   var=?  ptr=F580  "DIANA"       ← "diana".upper() 临时值被提前 free
[cg] str.clone         ptr=F5C0                 ← clone 到新地址
...
[cg] scope.drop  var=p  type=struct(Person)
[cg] str.free   var=?  ptr=F580  "NEGAT"       ← 再次 free 了 F580（已被复用）
```

**根因**：`Person p = Person { name: "diana".upper(), age: 28 }` 构造过程中：
1. `"diana".upper()` 产出 "DIANA" (F580)
2. struct 字面量 clone 到 F5C0
3. F580 的临时值被过早释放
4. 后来 Person 的 drop 路径又尝试释放 F580（已被其他分配复用为 "NEGATIVE"）
→ double-free + use-after-free

**修复方向**：检查 struct 字面量 codegen 中的临时值管理，确保在 struct 完全构造完成前不释放临时值。

### Bug 3 — INVALID FREE（野指针 free）

**报告**：
```
INVALID FREE  memcheck_edge.ls:0:0  ptr=F2FCF0 — never allocated by LS
```

**根因**：某个指针在没有被 `ls_mc_alloc` 追踪的情况下传给了 free。可能是一个静态字符串（cap=0）被错误地传给了 free，或某个路径在 memcheck 初始化前就调用了 free。

**修复方向**：找出 F2FCF0 的来源，确认是合法静态指针被误 free 还是真正的野指针。

### Bug 4 — 12 个 25-byte "unknown" 泄漏（wrapper 路径）

共 300 bytes，全部 kind="unknown" line=0:0，来源可能是：
- 递归 enum Tree Node 的 box 分配（build_tree(3) 创建约 7 个节点，每个 ~25 bytes）
- map 节点分配（3 个 set 操作，每个节点 ~25 bytes）
- 上述路径的 free 端走 wrapper 导致"未知"；完成 Phase A.5 后应能精确定位

**修复方向**：Phase A.5 完成后重新跑，确定实际泄漏来源再修。

### 6.2 验收标准

- [ ] memcheck_edge.ls --memcheck 跑出 ✓ clean
- [ ] memcheck_phase_a.ls 无回归
- [ ] memcheck_kinds.ls 无回归
- [ ] 全 ctest 通过

---

## 七、Phase B：验证 vec / map / enum / struct drop 正确性

### 7.1 范围调整

原计划 Phase B 要"替换 vec/map/enum/struct 路径的 alloc/free"。Phase A.5 已经完成了 free 端的替换。Phase B 现在的重点是：

1. 验证所有 vec/map/enum/struct 的 drop 路径正确（通过 memcheck 报告确认无泄漏/double-free）
2. 确认 `__drop` 用户析构函数的 memcheck 集成
3. 写 memcheck_phase_b.ls 覆盖这些路径

### 7.2 测试集

`tests/samples/memcheck_edge.ls` 已覆盖 vec/map/struct/enum/try/break 极端场景。
`tests/samples/memcheck_phase_b.ls` 待写（简化版的 edge，用于确认基础路径正确后再跑 edge）。

---

## 八、Phase C：AOT 集成 ✅ 已完成（2026-05-03）

### 8.1 把 memcheck.c 打成静态库 ✅

CMakeLists 新增（`runtime/memcheck.c` 仍**保留**在 ls.exe SOURCES 中，
这样 JIT 进程内的 `ls_mc_*` 符号仍能由 `LLVMOrcAbsoluteSymbols` 注册；
独立的 .lib 仅服务于 AOT 用户产物）：

```cmake
add_library(ls_memcheck STATIC runtime/memcheck.c)
target_include_directories(ls_memcheck PRIVATE src/ include/)

# 让 ls_memcheck.lib 与 ls.exe 落在同一目录（兼顾 single-config Ninja
# 与 multi-config Visual Studio 生成器），main.c 在运行期通过
# get_executable_dir() 定位。
set_target_properties(ls_memcheck PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY            "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG      "${CMAKE_BINARY_DIR}/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE    "${CMAKE_BINARY_DIR}/Release"
    ...
)
add_dependencies(ls ls_memcheck)
```

产出：`build/Debug/ls_memcheck.lib`（Windows）或 `build/libls_memcheck.a`（Linux/macOS）。

### 8.2 ls.exe AOT 链接 ✅

`src/main.c`：
- 新增 `get_executable_dir()` helper（Windows `GetModuleFileNameA`、macOS
  `_NSGetExecutablePath`、Linux `readlink("/proc/self/exe")` 三平台实现）
- 为避开 `<windows.h>` 中 `_TOKEN_INFORMATION_CLASS TokenType` 与 LS
  自身 `TokenType` 枚举的命名冲突，仅对 `GetModuleFileNameA` 做单点
  forward-declare，不引入 `windows.h`
- `cmd_compile` 在 `--memcheck` 为真时把 `<libdir>/ls_memcheck.lib`（或
  POSIX 的 `-L<dir> -lls_memcheck`）拼进 `clang` 链接命令；找不到 ls.exe
  目录时打印 warning，让用户能看到原因

### 8.3 fflush(stdout) 修复输出顺序 ✅

`runtime/memcheck.c` 的 `ls_mc_report` 开头加 `fflush(stdout)`。
原因：AOT 运行时报告由 `atexit` 触发，stdout 上残留的程序输出此时
还没 flush，stderr 上的报告会比 stdout 先到终端，导致看起来"报告
出现在程序输出之前"。JIT 不受影响（同进程顺序流）。

### 8.4 ctest 集成 ✅

新增 `tests/test_memcheck_aot.cmake`（cmake -P 驱动，跨平台）：
1. `ls compile --memcheck tests/samples/memcheck_phase_a.ls -o tmp.exe`
2. 运行 tmp.exe
3. 断言 stderr 包含 `[memcheck] OK clean` 与 `SUMMARY: 0 leak(s) ...`

注册为 ctest `test_memcheck_aot`，依赖 `test_memory`（先验 JIT 路径）。

### 8.5 验收记录（2026-05-03）

- [x] CMake 输出 `build/Debug/ls_memcheck.lib`（18 KB）与 `ls.exe` 同目录
- [x] `ls compile --memcheck memcheck_phase_a.ls -o phase_a.exe` → exe 跑出 `OK clean`
- [x] `memcheck_kinds.ls` AOT → `OK clean`
- [x] `memcheck_edge.ls` AOT → `OK clean`（与 JIT 一致：0 leak / 0 double-free / 0 invalid free）
- [x] AOT 与 JIT 报告字面一致
- [x] 全 ctest 9/9 通过（含 `test_memcheck_aot`），无回归
- [x] 无 `--memcheck` flag 时 AOT 路径零回退（默认仍是普通可执行文件）

---

## 九、Phase D：增强（可选，按需做）

### 9.1 LS 调用栈追踪
### 9.2 verbose 模式
### 9.3 strict 模式
### 9.4 与 LS testing 框架集成

---

## 十、实现要点 / 已知坑

### 10.1 site dedup 必须
### 10.2 free(NULL) 行为
### 10.3 程序退出路径
### 10.4 多次 import / 多次 init 防护
### 10.5 性能预算
### 10.6 hash 函数
### 10.7 thread safety

（详见原始文档第八节，内容不变）

---

## 十一、最终验收

完整跑通后应满足：

1. `ls run --memcheck tests/samples/memcheck_*.ls` 全部 ✓ clean
2. `ls compile --memcheck` 后的 exe 跑出 ✓ clean
3. memcheck 关闭时 zero overhead（IR 不变，对性能无影响）
4. 至少抓到 1 个真实 bug 并修复（✅ 已验证：memcheck 抓到 4 类真实 bug）
5. 文档：CLAUDE.md 补 memcheck 章节

---

## 十二、Phase A.5 实施记录（2026-05-01）

### 12.1 完成的改动

**free 端全部接入 cg_emit_free：**

| 改动 | 文件 | 说明 |
|------|------|------|
| `emit_string_free_with_cont` 加 kind/line/col 参数 | codegen.c | 核心函数签名变更 |
| `cg_emit_free` 替代 raw `free()` | codegen.c | emit_string_free 全系列 + emit_temp_string_cleanup |
| vec.data/truncate free | codegen.c | `vec.scope_drop` |
| map key/node/buckets free | codegen.c | `map.scope_drop` |
| enum box free | codegen.c | `enum.scope_drop` |
| global cleanup macro | codegen.c | `string.scope_drop` |
| inline scope cleanup string free | codegen.c | `string.scope_drop` |

**alloc 端补全：**

| 改动 | 文件 |
|------|------|
| enum box malloc → `cg_emit_alloc("enum.box")` | codegen.c |
| map node/key data malloc → `cg_emit_alloc("map.node")` | codegen.c |
| map new node malloc → `cg_emit_alloc("map.node")` | codegen.c |
| calloc wrapper → `ls_mc_alloc` | codegen.c |
| string append malloc → `cg_emit_alloc("string.append")` | codegen.c |

### 12.2 Phase A.6 修复的 Bug

**Bug 1 — enum.box 泄漏（12 → 0）：** ✅ 已修复
- checker.c: `instantiate_enum_template` 中自递归 enum 的 `has_drop` 循环依赖修复（`pt == et`）
- codegen.c: `emit_cleanup_to` 计数循环 + 发射循环添加 `TYPE_ENUM` 处理
- codegen.c: enum 构造器正确 load alloca 值再 store 到 heap box（修复存 alloca 指针而非值的 bug）
- codegen.c: 构造器 box 后 memset 源 alloca（move 语义，防止 scope cleanup double-free）

**Bug 3 — struct 构造 double-free：** ✅ 已修复
- codegen.c: `AST_NEW_EXPR` 字段初始化时先 `emit_string_clone_val` 再 store，防止 temp flush 和 struct drop 共享同一数据指针

**Bug 4 — calloc 未追踪：** ✅ 已修复
- codegen.c: `cg_install_memcheck_wrappers` 新增 `calloc` wrapper → `ls_mc_alloc` + `memset`

### 12.3 已知未修复问题

**✅ Bug 2 — try 早返路径 string 泄漏（已修复 2026-05-01）：**

- 根因：`string s = fn_returning_string()` 走 var_decl auto-clone 路径，由于 RHS 是 rvalue 函数返回值（已转移所有权），克隆会丢失原 heap
- 修复：在 var_decl 检测 `init_node->kind == AST_CALL || AST_TRY`，跳过 clone（直接 store 转移所有权）
- 同时：AST_FIELD 字段读取的 string clone 现在注册到 temp slot（之前漏注册导致字段读后泄漏）
- 验证：`mc_simple_chain.ls` / `mc_try_simpler.ls` / `memcheck_edge.ls` 全部 ✓ clean

**🗑 Bug 2 历史细节（修复前）：**

| 泄漏 | 行号 | kind | 推测根因 |
|------|------|------|----------|
| 4096 bytes | 28:15 | `string.fstring` | f-string sprintf buffer 作为 temp 未被 flush |
| 16 bytes | 33:17 | `string.concat` | `s + "!"` 结果在 try_chain 的 Result payload 中未转移所有权 |
| 16 bytes | 28:22 | `string.clone` | try_inner Ok 路径 clone 未释放 |
| 16 bytes | 27:43 | `string.upper` | try_inner Err 路径 upper 结果未释放 |
| 16 bytes | 65:12 | `string.clone` | Person name clone（与 Bug 3 不同路径） |

**根因分析**：`try` 表达式创建的 `inner_alloca`（临时 Result enum）不在 scope 系统中，其中的 string payload 既没有被转移所有权到 `result_alloca`（Ok 路径），也没有被释放。已写 `AST_RETURN` temp flush 修复，但 AST_TRY 内的 temp 管理需要更系统的改动。

**✅ Bug 5 — sum_tree match 绑定 box double-free（已修复 2026-05-01）：**

- 根因 1：自递归 enum 函数参数（`fn sum_tree(Tree t)`）的 scope cleanup 会递归 drop 共享 boxes
- 修复 1：codegen.c 在 fn 参数注册处检测 `param_type` 为 enum 且任一 variant payload 类型 == param_type 自身（自递归），标记 `psym->is_borrowed = true`
- 根因 2：match.subj 是参数 t 的 struct 拷贝，share box 指针；Phase A 的 match.subj 自动 drop 在自递归 enum 上会与原 owner 的 cleanup 双重释放
- 修复 2：codegen.c match.subj 自动 drop 在 subject 是 self-recursive enum 类型时跳过（is_self_recursive 检测：任一 variant payload == subj_type）
- 修复 3：subject identifier 是 borrowed sym 时也跳过（参数 t 现在是 borrowed）
- 验证：`memcheck_edge.ls` 中 `sum_tree(t)` 递归 + match double-free 全部归零

**🗑 Bug 5 历史细节（修复前）：**

**根因**：`Tree` enum 的 `Node(int, Tree, Tree)` 变体中，子节点通过 heap box 指针引用。`sum_tree` 的 match 绑定 `l`/`r` 为 box 指针值，递归调用 `sum_tree(l)` 时需要 dereference box 获取 Tree 值。这个临时 Tree 值的 scope cleanup 会递归释放子节点的 box → 与原树共享的 box 被 double-free。

**影响范围**：自递归 enum + match 遍历 + 递归函数调用的组合场景。简单的构建+丢弃（`build_tree` standalone）已经 clean。

**✅ Person struct 残留 double-free（已修复 2026-05-01）：**

随 Bug 2 fix 一同消失（field-access clone 现在注册到 temp slot，被正确 flush）。

**🗑 Person struct 历史细节：**

`Person p = Person { name: "diana".upper(), age: 28 }` 的 `"diana".upper()` 临时值仍有 double-free 风险，可能与 temp flush 和 struct literal 的交互有关。

---

## 十三、下一步实施计划

### 13.1 优先级排序

| 优先级 | 任务 | 预计工作量 | 风险 |
|--------|------|-----------|------|
| **P0** | 修复 AST_TRY temp 管理 | ~2h | 中 — 涉及 AST_TRY + AST_RETURN + temp flush 三处联动 |
| **P1** | enum match 绑定所有权语义 | ~3h | 高 — 需要设计 "borrow" vs "own" 的 match 绑定机制 |
| **P1** | 将 `enum.box` 和 `map.node` alloc 统一替换 `cg_emit_alloc`（完成剩余 raw malloc） | ~0.5h | 低 — 纯机械替换 |
| **P2** | AOT 集成（Phase C） | ~1h | 低 — 独立任务 |
| **P3** | memcheck 性能优化 + verbose 模式 | ~1h | 低 |

### 13.2 P0: 修复 AST_TRY temp 管理

**问题**：`try expr` 展开时创建的 `inner_alloca`（临时 Result enum）不在 scope 中，其中的 string payload 无所有权转移机制。

**方案 A（推荐）**：在 AST_TRY 的 Ok 路径中，将 `inner_alloca` payload 中的 string **move**（非 clone）到 `result_alloca`，然后立即 `emit_string_free` 清理 `inner_alloca` 中的 Err payload 残留。

**方案 B**：将 `inner_alloca` 注册到 scope 系统，让函数退出时的 scope cleanup 自动处理。

### 13.3 P1: enum match 绑定所有权

**问题**：match 绑定自递归 box 字段后，递归函数调用创建临时 Tree 值，其 scope cleanup 与原树共享 box 导致 double-free。

**方案**：match 绑定的 box 指针字段应标记为 "borrowed"（类似 `&T`），生成的临时值在 cleanup 时跳过这些字段。或者，在 match arm 中为 box 指针字段生成浅拷贝（不拥有 heap box）。

### 13.4 已完成验证（更新 2026-05-01）

- `memcheck_phase_a.ls` — ✅ 0 leak / 0 dfree / 0 ifree
- `memcheck_kinds.ls` — ✅ 0 leak / 0 dfree / 0 ifree
- `memcheck_edge.ls` — ✅ **0 leak / 0 dfree / 0 ifree**（Phase B 全部修复）
- `io_basic_test.ls` — ✅ 0 leak / 0 dfree / 0 ifree
- `io_seek_test.ls` — ✅ 0 leak / 0 dfree / 0 ifree
- 全 ctest 8/8 — ✅ 通过

### 13.5 kind 标签覆盖总览

| 类别 | alloc kind | free kind | 状态 |
|------|-----------|-----------|------|
| string 方法 | `string.upper/lower/trim/substr/concat/clone/copy/fstring/append/tostr` | `string.scope_drop/vec_elem_drop/struct_field_drop/reassign/temp` | ✅ |
| vec | `vec.grow/clone/literal` | `vec.scope_drop` | ✅ |
| map | `map.node` | `map.scope_drop` | ✅ |
| enum | `enum.box` | `enum.scope_drop` | ✅ |
| struct | `struct.heap` (via `malloc` wrapper 部分) | `string.struct_field_drop` | ⚠️ alloc 端待补 |
| io | `io.slurp` | — | ✅ |
| user | — | `user.free` (via wrapper) | ⚠️ 部分 |

---

## 十四、时间预算（更新）

| Phase | 工作量 | 状态 |
|---|---|---|
| A | ~2h | ✅ 已完成 |
| A.5 | ~1h | ✅ 已完成 |
| A.6 (Bug 1,3,4) | ~2h | ✅ 已完成 |
| A.6 (Bug 2,5) | ~5h | ✅ 已完成（见 §13.4） |
| B | ~1h | ✅ 通过 memcheck_edge.ls 全场景 clean 隐式覆盖 |
| C | ~1h | ✅ 已完成（2026-05-03，见 §八） |
| D | 按需 | — |
