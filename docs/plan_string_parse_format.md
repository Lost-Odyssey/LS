# 字符串解析与格式化 — 实现说明书

## 0. 概述

为 LS 补全字符串数值解析（`to_int`/`to_float`）和高级格式化（`str.format`）能力，填补与 Rust `str::parse` / Go `strconv` / Ruby `to_i` / C++ `std::stoi` 的差距。

**实现策略**：
- `to_int()` / `to_float()` / `to_bool()` — 内建 string 方法（编译器内建，与 `upper`/`lower` 同级）
- `lines()` / `repeat()` / `pad_left()` / `pad_right()` / `chars()` — 内建 string 方法
- `str.format(fmt, ...)` — 纯 LS stdlib 模块 `std/strconv.ls`

**理由**：解析函数需要精确控制 LLVM IR（调用 libc `strtol`/`strtod`、构造 Result enum），适合内建；`format` 是纯字符串操作，适合用 LS 自身编写。

---

## 1. 目标 API

### 1.1 String 内建方法扩展

```ls
// --- 数值解析 ---
Result(int, string)  r = "42".to_int()          // Ok(42)
Result(int, string)  r = "0xFF".to_int()         // Ok(255) — 支持 0x/0b/0o 前缀
Result(int, string)  r = "abc".to_int()          // Err("invalid integer: 'abc'")

Result(i64, string)  r = "9999999999".to_i64()   // Ok(9999999999)

Result(f64, string)  r = "3.14".to_float()       // Ok(3.14)
Result(f64, string)  r = "1e-10".to_float()      // Ok(1e-10)
Result(f64, string)  r = "xyz".to_float()        // Err("invalid float: 'xyz'")

Result(bool, string) r = "true".to_bool()        // Ok(true)
Result(bool, string) r = "yes".to_bool()         // Ok(true)  — "true"/"yes"/"1" -> true
Result(bool, string) r = "false".to_bool()       // Ok(false) — "false"/"no"/"0" -> false
Result(bool, string) r = "maybe".to_bool()       // Err("invalid bool: 'maybe'")

// --- 分行 ---
vec(string) ls = "a\nb\nc".lines()               // ["a", "b", "c"]
vec(string) ls = "a\r\nb\r\n".lines()            // ["a", "b"] — 自动处理 CRLF

// --- 重复 ---
string s = "ha".repeat(3)                         // "hahaha"
string s = "".repeat(100)                         // ""

// --- 填充 ---
string s = "42".pad_left(6, '0')                  // "000042"
string s = "hi".pad_right(10, '.')                 // "hi........"

// --- 字符提取 ---
vec(int) cps = "hello".chars()                    // [104, 101, 108, 108, 111]
```

### 1.2 stdlib 格式化模块

```ls
import std.strconv

// 基础格式化（位置参数）
string s = strconv.format("{} + {} = {}", 1, 2, 3)    // "1 + 2 = 3"

// 数值精度
string s = strconv.format("{:.2f}", 3.14159)           // "3.14"
string s = strconv.format("{:.0f}", 3.7)               // "4"

// 整数进制
string s = strconv.format("{:x}", 255)                 // "ff"
string s = strconv.format("{:X}", 255)                 // "FF"
string s = strconv.format("{:o}", 255)                 // "377"
string s = strconv.format("{:b}", 255)                 // "11111111"

// 填充与对齐
string s = strconv.format("{:>10}", "hi")              // "        hi"
string s = strconv.format("{:<10}", "hi")              // "hi        "
string s = strconv.format("{:0>8d}", 42)               // "00000042"

// 显式索引（可选，v2）
string s = strconv.format("{1} then {0}", "B", "A")   // "A then B"
```

---

## 2. 内建 String 方法 — Checker 实现

### 2.1 修改位置

`src/checker.c` 的 `check_string_method()` 函数内添加新分支。

### 2.2 各方法类型检查

#### 2.2.1 `to_int`

```c
/* s.to_int() -> Result(int, string) */
if (strcmp(method, "to_int") == 0)
{
    if (argc != 0) {
        checker_error(c, node->line, node->column,
                      "string.to_int() takes no arguments, got %d", argc);
        return NULL;
    }
    return checker_instantiate_result(c, type_int(), type_string());
}
```

#### 2.2.2 `to_i64`

```c
/* s.to_i64() -> Result(i64, string) */
if (strcmp(method, "to_i64") == 0)
{
    if (argc != 0) { error; return NULL; }
    return checker_instantiate_result(c, type_i64(), type_string());
}
```

#### 2.2.3 `to_float`

```c
/* s.to_float() -> Result(f64, string) */
if (strcmp(method, "to_float") == 0)
{
    if (argc != 0) { error; return NULL; }
    return checker_instantiate_result(c, type_f64(), type_string());
}
```

#### 2.2.4 `to_bool`

```c
/* s.to_bool() -> Result(bool, string) */
if (strcmp(method, "to_bool") == 0)
{
    if (argc != 0) { error; return NULL; }
    return checker_instantiate_result(c, type_bool(), type_string());
}
```

#### 2.2.5 `lines`

```c
/* s.lines() -> vec(string) */
if (strcmp(method, "lines") == 0)
{
    if (argc != 0) { error; return NULL; }
    return type_vec(type_string());
}
```

#### 2.2.6 `repeat`

```c
/* s.repeat(n) -> string */
if (strcmp(method, "repeat") == 0)
{
    if (argc != 1) { error; return NULL; }
    Type *arg = check_expr(c, call_node->as.call.args[0]);
    if (arg && !type_is_integer(arg)) { error("expects integer"); return NULL; }
    return type_string();
}
```

#### 2.2.7 `pad_left` / `pad_right`

```c
/* s.pad_left(width, fill_char) -> string */
/* s.pad_right(width, fill_char) -> string */
if (strcmp(method, "pad_left") == 0 || strcmp(method, "pad_right") == 0)
{
    if (argc != 2) { error("%s() takes 2 arguments (width, fill_char)", method); return NULL; }
    Type *w = check_expr(c, call_node->as.call.args[0]);
    if (w && !type_is_integer(w)) { error("width must be integer"); return NULL; }
    Type *ch = check_expr(c, call_node->as.call.args[1]);
    if (ch && ch->kind != TYPE_CHAR && !type_is_integer(ch)) {
        error("fill_char must be char or integer"); return NULL;
    }
    return type_string();
}
```

#### 2.2.8 `chars`

```c
/* s.chars() -> vec(int) */
if (strcmp(method, "chars") == 0)
{
    if (argc != 0) { error; return NULL; }
    return type_vec(type_int());
}
```

---

## 3. 内建 String 方法 — Codegen 实现

### 3.1 修改位置

`src/codegen.c` 的 `codegen_string_method()` 函数内添加新分支。

### 3.2 各方法 Codegen 细节

#### 3.2.1 `to_int` — 调用 libc `strtol` + 构造 Result

```
IR 伪代码：
  // 获取 string 的 data 指针
  data = extractvalue string_val, 0          // i8*
  len  = extractvalue string_val, 1          // i32

  // 调用 strtol(data, &endptr, 0)   — base=0 自动检测 0x/0b/0o
  endptr_alloca = alloca ptr
  result = call @strtol(data, endptr_alloca, 0)
  endptr = load endptr_alloca

  // 判断是否成功：
  //   1. endptr != data（至少消费了一个字符）
  //   2. *endptr == '\0' 或 endptr == data+len（整个字符串被消费）
  consumed = ptrdiff(endptr, data)
  ok = (consumed > 0) && (consumed == len)

  if ok:
    // 构造 Ok(result as int)  → Result(int, string)
    trunc_val = trunc i64→i32 result    // strtol 返回 long
    alloca result_enum
    store disc=0 (Ok)
    GEP payload, store trunc_val
    return load result_enum
  else:
    // 构造 Err("invalid integer: '...'")
    // 构造错误消息字符串
    alloca result_enum
    store disc=1 (Err)
    GEP payload, store error_string
    return load result_enum
```

**平台差异**：
- Windows MSVC：`strtol` 返回 `long`（32-bit）→ 直接用于 int
- Linux/macOS：`strtol` 返回 `long`（64-bit on LP64）→ 需要 trunc

**简化方案**：统一使用 `strtoll` 返回 `long long`（64-bit），然后 trunc 到 i32（for `to_int`）或直接使用（for `to_i64`），并检查溢出。

```c
// Codegen 需要 declare 的 libc 函数：
extern i64 @strtoll(ptr, ptr*, i32)   // (str, endptr, base)
extern f64 @strtod(ptr, ptr*)          // (str, endptr)
```

#### 3.2.2 `to_i64`

与 `to_int` 相同，但不做 trunc，直接使用 `strtoll` 的 i64 返回值。Result 类型为 `Result(i64, string)`。

#### 3.2.3 `to_float` — 调用 libc `strtod`

```
IR 伪代码：
  data = extractvalue string_val, 0
  len  = extractvalue string_val, 1
  
  endptr_alloca = alloca ptr
  result = call @strtod(data, endptr_alloca)
  endptr = load endptr_alloca
  
  consumed = ptrdiff(endptr, data)
  ok = (consumed > 0) && (consumed == len)
  
  if ok:
    // 构造 Ok(result)  → Result(f64, string)
    store disc=0 + payload=result
  else:
    // 构造 Err("invalid float: '...'")
    store disc=1 + payload=error_string
```

#### 3.2.4 `to_bool` — 纯 IR 字符串比较

```
IR 伪代码：
  data = extractvalue string_val, 0
  len  = extractvalue string_val, 1
  
  // 转小写后比较（简化：直接比较原始值）
  // 匹配 "true" / "yes" / "1" → Ok(true)
  // 匹配 "false" / "no" / "0" → Ok(false)
  // 其他 → Err(...)
  
  // 实现：call strcmp 或 inline 比较
  is_true  = (strcmp(data,"true")==0) || (strcmp(data,"yes")==0) || (strcmp(data,"1")==0)
  is_false = (strcmp(data,"false")==0) || (strcmp(data,"no")==0) || (strcmp(data,"0")==0)
  
  if is_true:  return Ok(true)
  if is_false: return Ok(false)
  else:        return Err("invalid bool: '...'")
```

**实现方式**：为避免多次 `strcmp` 调用的 IR 膨胀，生成一个 C runtime helper 函数 `__ls_str_to_bool(data, len) -> int`（返回 0=false, 1=true, -1=error），从 codegen 调用。

#### 3.2.5 `lines` — 返回 vec(string)

```
IR 伪代码：
  data = extractvalue string_val, 0
  len  = extractvalue string_val, 1
  
  new_vec = empty vec(string)
  start = 0
  i = 0
  loop:
    if i >= len goto flush_last
    ch = load data[i]
    if ch == '\n':
      // 检查前一个字符是否为 '\r'（CRLF）
      line_end = i
      if i > start && load data[i-1] == '\r':
        line_end = i - 1
      substr = string.substr(data+start, line_end - start)
      vec.push(new_vec, substr)
      start = i + 1
    i++
    goto loop
  flush_last:
    if start < len:
      // 处理最后一行（无换行结尾）
      end = len
      if len > start && load data[len-1] == '\r': end = len-1
      substr = string.substr(data+start, end - start)
      vec.push(new_vec, substr)
    return new_vec
```

**实现方式**：由于 IR 循环逻辑复杂，建议实现为 C runtime helper：

```c
// runtime/builtins.c 新增：
void __ls_str_lines(const char *data, int len, /* vec(string)* */ void *out_vec);
```

codegen 调用此 helper，传入 string 的 data/len 和 output vec 的 alloca 指针。

#### 3.2.6 `repeat`

```
IR 伪代码：
  data = extractvalue string_val, 0
  len  = extractvalue string_val, 1
  n    = codegen_expr(args[0])
  
  if n <= 0 || len == 0:
    return empty string (data=global "", len=0, cap=0)
  
  new_len = len * n
  new_cap = new_len + 1
  buf = cg_emit_alloc(new_cap, "string.repeat", line, col)
  
  // memcpy 循环
  i = 0
  loop:
    if i >= n goto done
    memcpy(buf + i*len, data, len)
    i++
    goto loop
  done:
    store '\0' at buf[new_len]
    return {buf, new_len, new_cap}
```

#### 3.2.7 `pad_left` / `pad_right`

```
IR 伪代码 (pad_left):
  data = extractvalue string_val, 0
  len  = extractvalue string_val, 1
  width = codegen_expr(args[0])
  fill  = codegen_expr(args[1])     // char → i8
  
  if len >= width:
    return clone(string_val)        // 已满足宽度
  
  pad_n = width - len
  new_len = width
  new_cap = new_len + 1
  buf = cg_emit_alloc(new_cap, "string.pad", line, col)
  
  // pad_left: fill 在前，data 在后
  memset(buf, fill, pad_n)
  memcpy(buf + pad_n, data, len)
  store '\0' at buf[new_len]
  return {buf, new_len, new_cap}
```

`pad_right` 相反：data 在前，fill 在后。

#### 3.2.8 `chars`

```
IR 伪代码：
  data = extractvalue string_val, 0
  len  = extractvalue string_val, 1
  
  // v1: 字节级（非 Unicode aware），每个字节作为 int
  new_vec = vec(int), capacity = len
  i = 0
  loop:
    if i >= len goto end
    ch = zext (load data[i]) to i32
    vec.push(new_vec, ch)
    i++
    goto loop
  end:
    return new_vec
```

**注**：v1 是字节级实现。未来 Unicode 支持需要 UTF-8 解码循环（按 leading byte 判断字符宽度）。可在 v2 中用 C helper `__ls_str_chars_utf8` 替换。

---

## 4. Result 枚举构造的 Codegen 模式

`to_int`/`to_float`/`to_bool` 都需要在 IR 中构造 `Result(T, string)` 枚举值。这与现有 `try` 操作符（Phase 8.5）和 `io.ls` 中的 Result 构造是同一模式。

### 4.1 查找或实例化 Result 类型

```c
// 在 checker 中已经调用了 checker_instantiate_result(c, T, string)
// codegen 通过 resolved_type 拿到对应的 LLVM struct 类型
Type *result_type = node->resolved_type;  // Result(int, string)
LLVMTypeRef result_llvm = type_to_llvm(ctx, result_type);
```

### 4.2 构造 Ok(value)

```c
// alloca result struct
LLVMValueRef res = LLVMBuildAlloca(ctx->builder, result_llvm, "res");
// store disc = 0 (Ok)
LLVMValueRef disc_ptr = LLVMBuildStructGEP2(ctx->builder, result_llvm, res, 0, "disc");
LLVMBuildStore(ctx->builder, LLVMConstInt(i32_t, 0, 0), disc_ptr);
// store payload
LLVMValueRef payload_ptr = LLVMBuildStructGEP2(ctx->builder, result_llvm, res, 1, "payload");
// payload 是 union（足够大的 bytes），需要 bitcast 到正确类型
LLVMValueRef typed_ptr = LLVMBuildBitCast(ctx->builder, payload_ptr,
    LLVMPointerType(type_to_llvm(ctx, T), 0), "ok.ptr");
LLVMBuildStore(ctx->builder, value, typed_ptr);
// load and return
return LLVMBuildLoad2(ctx->builder, result_llvm, res, "result");
```

### 4.3 构造 Err(string)

```c
// 类似，disc = 1，payload 存 string struct
LLVMValueRef err_str = ls_string_from_literal(ctx, "invalid integer: '...'");
// store disc = 1
// store err_str into payload
```

### 4.4 错误消息构造

对于 `to_int`/`to_float`/`to_bool`，错误消息需要包含原始字符串值。两种方案：

**方案 A（推荐）**：固定错误消息 + 不含原值
- `"invalid integer"` / `"invalid float"` / `"invalid bool"`
- 简单、IR 体积小、无额外 string 分配

**方案 B**：错误消息包含原值
- `f"invalid integer: '{s}'"` — 需要运行时格式化字符串，增加复杂度
- 对调试更友好

建议 v1 走方案 A，v2 可升级为方案 B（利用已有 f-string codegen）。

---

## 5. C Runtime Helper 函数

以下 helper 函数添加到 `runtime/builtins.c`：

```c
/* --- string.to_bool helper --- */
/* Returns 0=false, 1=true, -1=error */
int __ls_str_to_bool(const char *data, int len);

/* --- string.lines helper --- */
/* Splits data by \n (handling \r\n), pushes substrings into out_vec.
   out_vec is a pointer to LsVec (same layout as vec(string)). */
void __ls_str_lines(const char *data, int len,
                    char **out_data, int *out_len, int *out_cap);
```

**注意**：`lines` helper 的接口设计需要与 vec(string) 的内存布局对齐。有两种方式：

1. **Helper 返回 `vec(string)` struct**：helper 内部 malloc buffer、构造 LsString 元素、返回完整 vec struct。codegen 直接使用返回值。
2. **codegen 侧循环 + 调用 substr**：不用 helper，在 IR 中 inline 整个逻辑。

推荐方案 1，因为 CRLF 处理逻辑在 IR 中太冗长。

JIT 符号绑定：在 `jit.c` 的 `AbsoluteSymbols` 注册 `__ls_str_to_bool` 和 `__ls_str_lines`。

---

## 6. stdlib `std/strconv.ls` — 格式化模块

### 6.1 依赖

- 内建 string 方法（`at`/`substr`/`append`/`length` 等）
- `to_int` / `to_float` / `to_bool`（本说明书 §2-4）
- 内建 `print` 的 `__ls_int_to_str` / `__ls_float_to_str` IR helper（已存在于 codegen.c，但未暴露为 LS 可调用函数）

### 6.2 实现策略

`strconv.format` 用纯 LS 编写，手动解析格式字符串中的 `{}` 占位符。

**核心挑战**：LS 没有可变参数（variadic）函数。`format("{} + {} = {}", 1, 2, 3)` 需要 3 个不同类型的参数。

**解决方案**：分两步实现。

#### 方案 A：类型特化版本（v1，无需语言改动）

提供固定参数数量的 format 变体：

```ls
fn format1(string fmt, string a1) -> string { ... }
fn format2(string fmt, string a1, string a2) -> string { ... }
fn format3(string fmt, string a1, string a2, string a3) -> string { ... }
fn format4(string fmt, string a1, string a2, string a3, string a4) -> string { ... }
```

调用方先用 `f"{value}"` 把值转成 string，再传入：

```ls
import std.strconv

string s = strconv.format2("{} + {} = ?", f"{a}", f"{b}")
```

**缺点**：用户需要手动 `f"..."` 转换，无法直接传 int/f64。

#### 方案 B：基于 vec(string) 的 format（v1.5，推荐）

```ls
fn format(string fmt, vec(string) args) -> string { ... }

// 调用：
string s = strconv.format("{} + {} = {}", [f"{1}", f"{2}", f"{3}"])
```

**优点**：任意参数数量；用户用 `f"..."` 转换为 string。
**缺点**：仍需手动转换。

#### 方案 C：编译器 intrinsic format（v2，最佳体验）

让 `strconv.format(fmt, ...)` 成为编译器 intrinsic，在 checker/codegen 中识别并展开。

```ls
string s = strconv.format("{} + {} = {}", 1, 2, 3)   // 编译器自动类型分派
```

**实现**：checker 识别 `strconv.format` 调用后，不做普通函数类型检查，而是：
1. 第一个参数必须是 string 字面量（编译期解析 `{}` 占位符）
2. 后续参数按位置匹配占位符，检查类型兼容性
3. Codegen：按占位符逐段拼接字符串

这本质上与现有 `f"..."` 格式化字符串的 codegen 相同，只是格式字符串是运行时值而非编译期字面量。如果格式字符串不是字面量，退化为运行时解析。

### 6.3 推荐路线

**v1（立即可做）**：方案 B，纯 LS 实现，不依赖编译器改动。

```ls
// std/strconv.ls

fn format(string fmt, vec(string) args) -> string {
    string result = ""
    int arg_idx = 0
    int i = 0
    int n = fmt.length
    int seg_start = 0
    while i < n {
        if i + 1 < n && fmt.at(i) == 123 && fmt.at(i + 1) == 125 {
            // 找到 "{}"（123='{'  125='}'）
            if i > seg_start {
                result.append(fmt.substr(seg_start, i - seg_start))
            }
            if arg_idx < args.length {
                result.append(args[arg_idx])
                arg_idx = arg_idx + 1
            }
            i = i + 2
            seg_start = i
        } else {
            i = i + 1
        }
    }
    if seg_start < n {
        result.append(fmt.substr(seg_start, n - seg_start))
    }
    return result
}
```

**v2（待 variadic 或 `object[]`）**：支持直接传 int/f64/string 混合参数。

### 6.4 精度控制与进制转换

这些在 v1 中作为独立函数提供，不依赖 format 基础设施：

```ls
// std/strconv.ls 额外函数

// 整数→字符串（指定进制）
fn int_to_hex(int n) -> string { ... }      // "ff"
fn int_to_oct(int n) -> string { ... }      // "377"
fn int_to_bin(int n) -> string { ... }      // "11111111"

// 浮点→字符串（指定小数位数）
fn float_fixed(f64 n, int digits) -> string { ... }  // "3.14"
fn float_sci(f64 n, int digits) -> string { ... }    // "3.14e+00"

// 数值→字符串（通用，复用 f"..." 机制）
fn to_string(int n) -> string { return f"{n}" }
fn to_string_f(f64 n) -> string { return f"{n}" }
```

**进制转换实现**：纯 LS 循环除法取余：

```ls
fn int_to_hex(int n) -> string {
    if n == 0 { return "0" }
    string digits = "0123456789abcdef"
    string result = ""
    bool neg = n < 0
    if neg { n = 0 - n }
    while n > 0 {
        int d = n % 16
        // 前置拼接：result = char + result
        string ch = digits.substr(d, 1)
        result = ch + result
        n = n / 16
    }
    if neg { result = "-" + result }
    return result
}
```

**浮点精度控制**：需要 C runtime helper（纯 LS 实现浮点→字符串精度控制太复杂）：

```c
// runtime/builtins.c 新增：
/* Format a double with N decimal places, returns malloc'd NUL-terminated buffer.
   Caller takes ownership (the LS string will wrap it via __string_take_buffer). */
char *__ls_float_to_fixed(double val, int digits, int *out_len);
```

codegen 中声明此 extern fn，`float_fixed` 调用它。

---

## 7. 实现步骤

### Phase S.P1：`to_int` / `to_i64` / `to_float`（核心解析）

1. `src/checker.c`：`check_string_method` 添加 3 个方法分支
2. `src/codegen.c`：`codegen_string_method` 添加 `strtoll`/`strtod` 调用 + Result 构造
3. `src/jit.c`：确认 `strtoll`/`strtod` 在 JIT process symbol 中可解析（libc 符号，应该自动可用）
4. 测试：`tests/samples/string_parse_test.ls`

### Phase S.P2：`to_bool` + `lines` + `repeat` + `pad_left` + `pad_right` + `chars`

1. `runtime/builtins.c`：添加 `__ls_str_to_bool` helper
2. `src/checker.c` + `src/codegen.c`：添加 6 个方法
3. 对于 `lines`：评估 IR inline vs C helper 方案，选择更简洁的
4. 测试：`tests/samples/string_utils_test.ls`

### Phase S.P3：`std/strconv.ls` 格式化模块

1. 编写 `std/strconv.ls`（纯 LS）
2. 实现 `format(fmt, vec(string) args)`
3. 实现 `int_to_hex` / `int_to_oct` / `int_to_bin`（纯 LS）
4. 实现 `float_fixed`（需 C helper `__ls_float_to_fixed`）
5. 测试：`tests/samples/strconv_test.ls`

---

## 8. 测试计划

### 8.1 测试文件

```
tests/samples/string_parse_test.ls    — to_int/to_i64/to_float
tests/samples/string_utils_test.ls    — to_bool/lines/repeat/pad/chars
tests/samples/strconv_test.ls         — format + 进制转换 + 浮点精度
```

### 8.2 关键测试用例

```ls
// === to_int ===
Result(int, string) r1 = "42".to_int()
match r1 { Ok(v) => { print(v) }  Err(e) => { print(e) } }   // 42

Result(int, string) r2 = "0xFF".to_int()
match r2 { Ok(v) => { print(v) }  Err(e) => { print(e) } }   // 255

Result(int, string) r3 = "abc".to_int()
match r3 { Ok(v) => { print(v) }  Err(e) => { print(e) } }   // invalid integer

Result(int, string) r4 = "".to_int()
match r4 { Ok(v) => { print(v) }  Err(e) => { print(e) } }   // invalid integer

Result(int, string) r5 = "-100".to_int()
match r5 { Ok(v) => { print(v) }  Err(e) => { print(e) } }   // -100

// 带空格（应该失败）
Result(int, string) r6 = " 42".to_int()
match r6 { Ok(v) => { print(v) }  Err(e) => { print(e) } }   // 依赖 strtol 行为

// === to_float ===
Result(f64, string) rf1 = "3.14".to_float()
match rf1 { Ok(v) => { print(v) }  Err(e) => { print(e) } }  // 3.14

Result(f64, string) rf2 = "1e-10".to_float()
Result(f64, string) rf3 = "inf".to_float()
Result(f64, string) rf4 = "not_a_number".to_float()

// === lines ===
vec(string) ls = "hello\nworld\n".lines()
print(ls.length)       // 2
print(ls[0])           // hello
print(ls[1])           // world

vec(string) ls2 = "a\r\nb\r\nc".lines()
print(ls2.length)      // 3
print(ls2[0])          // a

vec(string) ls3 = "single".lines()
print(ls3.length)      // 1
print(ls3[0])          // single

// === repeat ===
print("ha".repeat(3))          // hahaha
print("x".repeat(0))           // (empty)
print("".repeat(5))            // (empty)

// === pad ===
print("42".pad_left(6, 48))    // 000042  (48 = '0')
print("hi".pad_right(5, 46))   // hi...   (46 = '.')
print("long".pad_left(2, 48))  // long    (不截断)

// === chars ===
vec(int) cs = "ABC".chars()
print(cs.length)       // 3
print(cs[0])           // 65
print(cs[1])           // 66
print(cs[2])           // 67

// === strconv.format ===
import std.strconv
string s1 = strconv.format("{} + {} = {}", [f"{1}", f"{2}", f"{3}"])
print(s1)              // 1 + 2 = 3

string s2 = strconv.format("hello {}", [f"world"])
print(s2)              // hello world

string s3 = strconv.format("no placeholders", [])
print(s3)              // no placeholders

// === 进制转换 ===
print(strconv.int_to_hex(255))     // ff
print(strconv.int_to_hex(0))       // 0
print(strconv.int_to_bin(10))      // 1010
print(strconv.int_to_oct(8))       // 10
```

---

## 9. 与现有代码的交互

### 9.1 `f"..."` 的关系

`f"..."` 是编译期格式化（codegen 在编译时展开插值表达式），`strconv.format` 是运行时格式化。两者互补：
- `f"hello {name}"` — 变量插值，简洁
- `strconv.format(template, args)` — 模板来自运行时（如配置文件、用户输入）

### 9.2 现有 `string.find` / `string.substr` 的复用

`lines()` 和 `repeat()` 的纯 LS 实现可以直接复用现有 string 方法。但作为内建方法（codegen 实现），它们走 IR 路径而非 LS 调用路径。

### 9.3 `strtol`/`strtod` 的 JIT 可见性

这些是 libc 标准函数。LLJIT 的 process symbol resolver 默认能解析它们（与 `fopen`/`fclose` 一样）。AOT 路径由 clang 链接 CRT。无需额外注册。

---

## 10. 已知限制与未来扩展

### 10.1 当前限制

- **`to_int` 的空格处理**：`strtol` 会跳过前导空格，`" 42".to_int()` 可能返回 `Ok(42)` 而非 `Err`。如需严格模式（不允许空格），需要在调用 `strtol` 前手动检查首字符。建议 v1 遵循 `strtol` 行为（跳过前导空格），与 C/Go 一致。
- **Unicode**：`chars()` v1 是字节级。`lines()` 按 `\n`/`\r\n` 分割，对 UTF-8 安全（换行符是 ASCII 单字节）。
- **`format` 无类型分派**：v1 的 `format` 要求所有参数预转为 string。精度控制（`{:.2f}`）在 v1 中不支持，需要独立的 `float_fixed()` 调用。
- **溢出检查**：`to_int` 的 `strtol` 在溢出时返回 `LONG_MAX`/`LONG_MIN` 并设 errno。v1 不检查 errno，溢出值静默返回。v2 可加 errno 检查。

### 10.2 未来扩展

- `to_int_radix(base)` — 指定进制解析
- `format` 支持 `{:>10}` / `{:<10}` / `{:0>8}` 对齐语法
- `format` 编译器 intrinsic 版本（直接传 int/f64，无需手动转换）
- Unicode-aware `chars()` 返回 Unicode 码点
- `bytes()` → `vec(u8)` 原始字节
- `from_chars(vec(int)) -> string` 逆操作

---

## 11. 工作量估算

| Phase | 内容 | 估算 |
|-------|------|------|
| S.P1 | to_int / to_i64 / to_float | 1.5 天 |
| S.P2 | to_bool / lines / repeat / pad / chars | 1.5 天 |
| S.P3 | std/strconv.ls (format + 进制 + 精度) | 1 天 |
| 测试 | 3 个测试文件 + memcheck | 0.5 天 |
| **合计** | | **4.5 天** |
