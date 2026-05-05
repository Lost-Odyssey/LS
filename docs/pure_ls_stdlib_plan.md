# LS 纯 LS 标准库路线图

## 0. 目标与战略方向

让 LS 的标准库**最大限度用 LS 自身编写**，编译器只内建不可绕过的语言原语（string/vec/map 内存操作、LLVM intrinsic）。

战略路线：与 Zig 相同——**实现完整 C ABI 互操作**，让用户能直接 `extern` 任意 C 头文件里的 API。这样不仅解决 stdlib 问题，也解决用户调第三方 C 库（sqlite、libpng 等）的问题。

当前内建 C 代码（`builtin_*_cg.c`）是 FFI 能力不足的临时方案。随着 C 互操作能力建设，将逐步被 `stdlib/` 目录下的纯 LS 文件取代。

---

## 1. 当前 FFI 的限制

| 能力 | Phase E 前 | Phase E 后 |
|------|:----------:|:----------:|
| 传/返 `int` / `i64` / `f64` / `bool` | ✅ | ✅ |
| 传/返 `string`（作为 `i8*`） | ✅ | ✅ |
| 传/返 `object`（`void*`） | ✅ | ✅ |
| `extern struct`（C 布局） | ❌ | ✅ Phase E.1 |
| struct by-pointer（`&!struct`） | ❌ | ✅ Phase E.1 |
| struct by-value / return（`byval`/`sret`） | ❌ | ✅ Phase E.2 |
| `extern { }` 分组块 | ❌ | ✅ Phase E.1 |
| `from` 子句可选（直调 libc） | ❌ | ✅ Phase E.1 |

---

## 2. Phase E：C ABI 互操作（Zig 路线）

### Phase E.1：`extern struct` + C 布局（当前阶段）

**做什么**：

1. **`extern { }` 分组块语法**：将 C 声明组织成块，块内的 `struct` 和 `fn` 均遵循 C ABI 规则。

```ls
extern {
    struct tm {
        int tm_sec    int tm_min    int tm_hour
        int tm_mday   int tm_mon    int tm_year
        int tm_wday   int tm_yday   int tm_isdst
    }

    struct stat_t {
        i64 st_dev    i64 st_ino    u32 st_mode
        i64 st_size   i64 st_mtime
    }

    fn stat(string path, &!stat_t buf) -> int
    fn localtime(*i64 timer) -> *tm
    fn strftime(*i8 buf, i64 maxsize, *i8 fmt, *tm t) -> i64
    fn time(*i64 t) -> i64
}
```

2. **独立形式也支持**（向后兼容）：
```ls
extern struct Point { int x  int y }
extern fn fopen(string path, string mode) -> object   // from 子句现在可选
```

3. **C 布局规则**：字段偏移按自然对齐，LLVM 非打包 struct 与 C ABI 匹配。

4. **允许的字段类型**：`i8`/`i16`/`i32(int)`/`i64`/`u8`/`u16`/`u32`/`u64`/`f32`/`f64`/`bool`/`object`/`*T`（指向任意类型的指针）/其他 `extern struct`。LS 托管类型（`string`/`vec`/`map`）禁止。

5. **使用方式**：Phase E.1 的 `extern struct` 通过指针传递（`&!stat_t`），不做 by-value ABI lowering（那是 Phase E.2）。

**涉及文件**：`ast.h`/`ast.c`、`types.h`/`types.c`、`parser.c`、`checker.c`、`codegen.c`

**工作量**：普通程序员日 3–4 天；LLM 节奏 1 天。

---

### Phase E.2：Windows x64 ABI Lowering（by-value 传参/返回）

**做什么**：让 `extern struct` 可以 by-value 传给 C 函数（当前只支持 by-pointer）。

Windows x64 规则（比 System V 简单得多）：
- `struct` 大小 ≤ 8 字节 → bitcast 为整数，放寄存器传递
- `struct` 大小 > 8 字节 → 隐式指针（caller 复制到栈，传地址）
- 返回 `struct` > 8 字节 → 插入隐式第一参数 `sret`

LLVM IR 对应：
```llvm
; 大 struct 传参 — byval attribute
call void @stat(%stat_t* byval(%stat_t) align 8 %s)

; 大 struct 返回 — sret attribute（隐式第一参数）
%ret = alloca %tm
call void @localtime(%tm* sret(%tm) %ret, i64* %ts)
```

**工作量**：普通程序员日 3 天；LLM 节奏 1 天。风险：LLVM `byval` attribute 在 opaque pointer 模式下行为需验证。

---

### Phase E.3：System V AMD64 ABI（Linux/macOS）

**做什么**：实现 System V ABI 分类算法（字段按 INTEGER/SSE/MEMORY 分类，决定走寄存器还是栈）。

**工作量**：普通程序员日 5 天；LLM 节奏 1–2 天。由于 LS 主目标是 Windows，可延后。

---

### Phase E.4：translate-c 辅助工具（可选，长期）

**做什么**：从 C 头文件自动生成 `extern { }` 块，类似 Zig 的 `translate-c`。

**工作量**：普通程序员日 7–10 天；LLM 节奏 2–3 天。非必要，手写 extern 块完全可行。

---

## 3. 其余基础设施（与 Phase E 并行）

### 3.1 errno 暴露
内建 `errno() -> int` + `strerror(int) -> string`，让 LS stdlib 能从 libc 失败路径提取错误原因。

**工作量**：普通程序员日 0.5 天；LLM 节奏 30 分钟。

### 3.2 `from` 子句可选（Phase E.1 已含）
`extern fn fopen(string path, string mode) -> object` 无需 `from lib_name` 即可直调 libc。

### 3.3 条件编译（最简版）
`#if WINDOWS` / `#if LINUX` / `#if MACOS` 三个平台宏，scanner 层静态消除 dead block。

**工作量**：普通程序员日 2 天；LLM 节奏 半天。

### 3.4 string 胶水层
- `string.from_cstr(*i8) -> string`：`getenv`/`strerror` 返回的 `char*` 转 LS managed string。
- `string.to_cstr() -> *i8`：LsString.data 零成本暴露。

**工作量**：普通程序员日 0.5 天；LLM 节奏 30 分钟。

### 3.5 stdlib 路径加载机制
`import` 三级查找：用户文件 → `$LS_HOME/stdlib/` → 编译器内建。

**工作量**：普通程序员日 1 天；LLM 节奏 1–2 小时。

---

## 4. 完成 Phase E 后开启的纯 LS 标准库

### 4.1 零 FFI 依赖（现在就能写）

| 模块 | 主要 API | 工作量（LLM） |
|------|---------|:------------:|
| `json` | `parse` / `stringify` / `query` | 半天–1 天 |
| `csv` | `parse` / `write` / `with_header` | 1–2 小时 |
| `base64` | `encode` / `decode` | 15 分钟 |
| `hex` | `encode` / `decode` | 15 分钟 |
| `url` | `encode` / `decode` | 15 分钟 |
| `strings` | `pad_left` / `repeat` / `lines` / `to_int` / ... | 1–2 小时 |
| `math_ext` | `gcd` / `lerp` / `clamp` / `stats` | 1–2 小时 |

**json 是自举里程碑**：能用 LS 写 JSON 解析器 = 语言表达能力达到实用水平。

### 4.2 Phase E.1 完成后（by-pointer 传参）

```ls
// stdlib/fs.ls — 纯 LS
extern {
    struct stat_t { i64 st_dev  u32 st_mode  i64 st_size  i64 st_mtime }
    fn stat(string path, &!stat_t buf) -> int
    fn mkdir(string path, u32 mode) -> int
    fn rename(string old_path, string new_path) -> int
    fn remove(string path) -> int
}

fn file_size(string path) -> Result(i64, string) {
    stat_t s = stat_t { st_dev: 0, st_mode: 0, st_size: 0, st_mtime: 0 }
    int r = stat(path, &s)
    if r != 0 { return Err(strerror(errno())) }
    return Ok(s.st_size)
}
```

| 模块 | 需要 | 工作量（LLM） |
|------|------|:------------:|
| `time`（基础版） | E.1 + `errno` | 1–2 小时 |
| `env` | E.1 + `from_cstr` | 1 小时 |
| `process`（system/spawn） | E.1 | 1–2 小时 |
| `fs`（stat/mkdir/rename/remove） | E.1 | 半天 |

### 4.3 Phase E.2 完成后（by-value 传参/返回）

| 模块 | 说明 |
|------|------|
| `time`（完整版） | `localtime` 返回 `*tm`（by-pointer）+ `strftime` → 完整格式化 |
| `fs`（完整版） | `opendir`/`readdir` by-value struct |
| `net`（基础 TCP） | `sockaddr_in` by-value 构造 |

---

## 5. 工作量总览

| Phase | 内容 | 普通程序员日 | LLM 节奏 | 风险 |
|-------|------|:-----------:|:--------:|------|
| E.1 | `extern struct` + C 布局 + `extern { }` 块 | 3–4 天 | 1 天 | 低 |
| E.2 | Windows x64 byval/sret ABI | 3 天 | 1 天 | 中（LLVM byval 细节）|
| E.3 | System V AMD64 ABI | 5 天 | 1–2 天 | 中（分类算法）|
| E.4 | translate-c | 7–10 天 | 2–3 天 | 低（可选）|
| 3.1 | errno | 0.5 天 | 30 分钟 | 低 |
| 3.3 | 条件编译 | 2 天 | 半天 | 低 |
| 3.4 | string 胶水层 | 0.5 天 | 30 分钟 | 低 |
| 3.5 | stdlib 路径加载 | 1 天 | 1–2 小时 | 低 |
| **E.1+E.2 合计** | **Windows 完整 C 互操** | **6–7 天** | **~2 天** | |

---

## 6. 关键设计约束

### `extern { }` 块语义
- 块内所有 `struct` 遵循 C ABI 布局（自然对齐，不重排字段）
- 块内所有 `fn` 遵循 C 调用约定（Phase E.2 后走 byval/sret）
- `from` 子句可选：无 `from` = 绑定进程符号（libc / Windows CRT）
- 块外的 LS `struct` 仍是 LS 语义（has_drop/RAII），不受影响

### extern struct 约束
- 字段类型只能是 C 兼容类型（见 §2 Phase E.1）
- 没有 `impl` 块（不能定义方法）
- 没有自动 drop（C 管理内存）
- `is_extern_c = true` 标志在 TYPE_STRUCT 上，供 Phase E.2 ABI lowering 识别

### stdlib 模块三级查找顺序（不可更改）
```
import foo
  1. <源文件所在目录>/foo.ls          ← 用户完全控制（shadow）
  2. $LS_HOME/stdlib/foo.ls           ← stdlib 目录（新增）
  3. 编译器内建（math / io）           ← 保底
```

### stdlib 命名保留
`math` `io` `json` `csv` `base64` `hex` `url` `strings` `math_ext` `time` `env` `process` `fs` `net` `thread` `regex`

---

## 7. 成功标准

| 里程碑 | 验证方式 |
|--------|---------|
| Phase E.1 完成 | `extern struct stat_t { ... }` + `extern fn stat(...)` AOT+JIT 双路径通过 ctest |
| json 自举 | `stdlib/json.ls` 中 `import json; json.parse(...)` AOT+JIT 通过 10 个以上 spec 用例 |
| fs 纯 LS | `stdlib/fs.ls` 的 `file_size`/`exists`/`mkdir` 通过 ctest，无内建 C 代码 |
| 内建精简 | `math` / `io` 保留内建（性能敏感 intrinsic），新增模块全走 stdlib 目录 |
