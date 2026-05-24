# LS JSON 模块详细设计

> **日期**：2026-05-20（最后更新：2026-05-24）
> **模块位置**：`std/json.ls`（纯 LS 实现）  
> **前置依赖**：~~Step 0（修复 L-006）~~ ✅ 已完成 2026-05-20
>
> **当前进度**：Step 0~3 ✅ 完成；Step 4（可写 API）❌ 未实现；Step 5 ✅ 测试通过（含 e2e memcheck，bugs/20 已修复 2026-05-24）

---

## 0. ✅ 前置：修复 L-006（enum 含 vec/map payload）— 已完成 2026-05-20

### 0.1 现状诊断

`JsonValue` 的核心数据结构是一个 enum，其中 `Array` 变体持有 `vec(JsonValue)`，`Object` 变体持有 `map(string, JsonValue)`。这依赖 **enum 含 vec/map payload** 的正确 drop。

**当前状态**（2026-05-20 实测）：

- Checker：✅ `type_owns_heap_for_enum` 正确识别 vec/map payload → `has_drop = true`
- Codegen `emit_auto_enum_drop_fn`：✅ 已有 `TYPE_VECTOR` 和 `TYPE_MAP` 的 drop 分支代码（Phase F.5 加入）
- **运行时**：❌ 崩溃（exit code 116）

最简复现：
```ls
enum Data {
    Empty
    Numbers(vec(int) nums)
}
fn main() {
    vec(int) v = [10, 20, 30]
    Data d = Numbers(v)     // ← crash here or at scope drop
    print("done")
}
```

`Data d = Empty` 正常工作，说明 enum 基础设施没问题，**问题出在含 vec payload 的变体构造或 drop 路径**。

### 0.2 可能的根因

需要逐一排查（按可能性排序）：

1. **enum 构造器 ABI 不匹配**：`Numbers(v)` 构造时，vec 是 `{ptr, i32, i32}` 12 字节结构体，enum payload 区域可能没有足够空间或对齐错误
   - 检查：`build_variant_payload_struct` 对 `TYPE_VECTOR` 返回的 LLVM struct 是否正确
   - 检查：`emit_enum_ctor` 中 payload store 路径对 vec 的处理

2. **构造后 vec 被 scope cleanup 二次释放**：`vec(int) v = [...]` 的值 store 进 enum 后，v 仍在作用域内，scope cleanup 释放了 v 的 data buffer，而 enum 也持有同一个 buffer → 悬垂指针
   - 需要：构造 `Numbers(v)` 后标记 v 为 moved（如 string 的 `cap = -1` 模式）
   - 当前 string payload 有这个机制（`emit_enum_ctor` 内对 TYPE_STRING 做 memset/mark），但 vec/map 可能没有

3. **match binder ABI**：match arm 中 `Numbers(nums)` 从 payload 区域 GEP 取 vec struct 时类型不匹配

4. **drop 路径中的 vec 元素遍历 crash**：`emit_auto_enum_drop_fn` 的 vec 分支在 `Empty` 变体上不应该进入，但如果 switch 有误可能跑错分支

### 0.3 修复计划

| 步骤 | 内容 | 工期 |
|------|------|------|
| 0.3.1 | 用 `CG_DEBUG=1` 构建，跑最小复现，从 IR dump 定位 crash 点 | 0.5 天 |
| 0.3.2 | 检查 `emit_enum_ctor` 对 vec/map payload 的 store 路径 | 0.5 天 |
| 0.3.3 | 确保构造 `Numbers(v)` 后 v 被标 moved（vec 的 `cap=0` 或 `moved_flag`） | 1 天 |
| 0.3.4 | 检查 match binder GEP 取 vec 的类型是否匹配 | 0.5 天 |
| 0.3.5 | 端到端测试（JIT + AOT + memcheck） | 0.5 天 |
| **总计** | | **2-3 天** |

### 0.4 验证标准

通过以下测试（JIT + AOT + memcheck 三重）：
```ls
enum Data { Empty  Numbers(vec(int) nums)  Lookup(map(string,int) m) }

fn main() {
    // 构造 + match 解构
    Data d1 = Numbers([10, 20, 30])
    match d1 { Numbers(nums) => print(nums.length)  _ => {} }

    // map payload
    map(string, int) m = {}
    m.set("a", 1)
    Data d2 = Lookup(m)
    match d2 { Lookup(t) => print(t.contains_key("a"))  _ => {} }

    // scope drop 不崩溃
    Data d3 = Numbers([1, 2])
    // d3 离开作用域，自动 drop vec
}
```

---

## 1. JsonValue 数据类型

### 1.1 核心 enum ✅ 已实现（结构有调整）

```ls
// std/json.ls（实际实现）

enum JsonValue {
    Null
    Bool(bool val)
    Number(f64 val)
    Str(string val)
    Array(vec(JsonValue) items)
    Object(vec(string) keys, map(string, JsonValue) entries)  // ← 与计划不同：多了 keys vec
}
```

**设计决策**：
- 数字统一用 `f64`（JSON 规范不区分整数/浮点，`f64` 能精确表示 ≤ 2^53 的整数）
- `Str` 而非 `String` 避免与内建 `string` 类型名冲突
- `Object` 用 `map(string, JsonValue)` 查找 O(1)；**额外加 `vec(string) keys` 保留插入顺序**（stringify 按原始 key 顺序输出）

### 1.2 便捷构造函数 ✅ 已实现（函数名略有调整）

```ls
// 实际函数名（与计划略有出入）
fn null_val()             -> JsonValue   // 计划同名 ✅
fn bool_val(bool b)       -> JsonValue   // 计划同名 ✅
fn number(f64 n)          -> JsonValue   // 计划同名 ✅
fn number_int(int n)      -> JsonValue   // 计划同名 ✅
fn str_val(string s)      -> JsonValue   // 计划名为 str()，改为 str_val() 避免关键字冲突
fn array_new()            -> JsonValue   // 计划名为 array()
fn object_new()           -> JsonValue   // 计划名为 object()，内部用 keys+entries 两字段
```

### 1.3 访问 / 修改 API

```ls
// ---- 类型判断 ✅ 全部已实现 ----
fn is_null(JsonValue v) -> bool
fn is_bool(JsonValue v) -> bool
fn is_number(JsonValue v) -> bool
fn is_string(JsonValue v) -> bool
fn is_array(JsonValue v) -> bool
fn is_object(JsonValue v) -> bool

// ---- 取值 ✅ 全部已实现 ----
fn as_bool(JsonValue v) -> Result(bool, string)
fn as_number(JsonValue v) -> Result(f64, string)
fn as_string(JsonValue v) -> Result(string, string)
fn as_int(JsonValue v) -> Result(int, string)       // f64 → int 截断，溢出报错

// ---- Array 只读 ✅ / 可写 ❌ ----
fn array_len(JsonValue v) -> int                    // ✅ 已实现（按值传参，非借用）
fn array_push(&!JsonValue arr, JsonValue item)      // ❌ 未实现
fn array_get(&JsonValue arr, int index) -> Result(JsonValue, string)  // ❌ 未实现

// ---- Object 只读 ✅ / 可写 ❌ ----
fn object_len(JsonValue v) -> int                   // ✅ 已实现（额外加的）
fn object_has(JsonValue v, string key) -> bool      // ✅ 已实现（按值传参）
fn object_keys(JsonValue v) -> vec(string)          // ✅ 已实现（按值传参，返回深拷贝）
fn object_set(&!JsonValue obj, string key, JsonValue val)             // ❌ 未实现
fn object_get(&JsonValue obj, string key) -> Result(JsonValue, string) // ❌ 未实现
```

> **注意**：`&!JsonValue` 可写借用 —— `&!enum` 当前**未验证是否支持**。
> 只读函数暂以**按值传参**实现（会深拷贝），等可写 API 实现时统一改为借用 ABI。
> 降级方案：直接返回/操作内部 vec/map，不依赖 `&!enum` 借用。

---

## 2. JSON 解析器（parse）✅ 已完成

### 2.1 API

```ls
fn parse(string input) -> Result(JsonValue, string)
```

错误返回格式：`"json: unexpected 'x' at position 42, expected ':'"` 

### 2.2 实现：递归下降

```
parse_value → dispatch on first non-whitespace char:
  '"'  → parse_string
  '-' | digit → parse_number
  't'  → parse_true
  'f'  → parse_false
  'n'  → parse_null
  '['  → parse_array  → '[' parse_value (',' parse_value)* ']'
  '{'  → parse_object → '{' parse_pair (',' parse_pair)* '}'
  else → Err("unexpected character")
```

**状态**：用 `int pos` 游标扫描 input string（通过 `.at(pos)` 逐字符）。

### 2.3 字符串解析细节

```
parse_string:
  '"' → 扫描到下一个未转义的 '"'
  转义序列：\" \\ \/ \b \f \n \r \t
  Unicode 转义：\uXXXX → 解码为 UTF-8 字节序列
  （v1 先不做 \uXXXX，遇到时原样保留或报错）
```

### 2.4 数字解析

```
parse_number:
  optional '-'
  integer part: '0' | [1-9][0-9]*
  optional '.' [0-9]+
  optional [eE] [+-]? [0-9]+
  → 调 string.to_float() 转为 f64
```

### 2.5 错误恢复

v1 不做错误恢复——遇到第一个语法错误立即返回 `Err`。

---

## 3. JSON 输出（stringify）✅ 已完成

### 3.1 API

```ls
fn stringify(JsonValue val) -> string                  // compact，无空白
fn stringify_pretty(JsonValue val, int indent) -> string  // 缩进 indent 空格
```

### 3.2 实现

递归遍历 `JsonValue`：

```
emit_value(val, depth):
  Null    → "null"
  Bool(b) → "true" / "false"
  Number(n) → f"{n}"（需处理整数情况：42.0 → "42"）
  Str(s)  → '"' + escape(s) + '"'
  Array(items) →
    '[' + items.map(|v| emit_value(v, depth+1)).join(",") + ']'
    pretty 模式：每个元素独占一行，前缀 indent
  Object(entries) →
    '{' + entries.keys().map(|k| '"'+escape(k)+'":' + emit_value(entries.get(k), depth+1)).join(",") + '}'
```

### 3.3 字符串转义

```
escape(s):
  '"'  → \"
  '\\' → \\
  '\n' → \n
  '\r' → \r
  '\t' → \t
  控制字符 (0x00-0x1F) → \u00XX
```

---

## 4. 基础设施检查清单

在开始编写 `std/json.ls` 之前，需要确认以下编译器能力：

| 能力 | 需要 | 当前状态 | 阻塞？ |
|------|------|----------|--------|
| enum 含 vec payload 构造 + drop | ✅ | ✅ 已修复 | 否 |
| enum 含 map payload 构造 + drop | ✅ | ✅ 已修复 | 否 |
| enum 自递归（`vec(JsonValue)`） | ✅ | ✅ 自递归 enum 已支持（自动 box） | 否 |
| match 解构 vec payload binder | ✅ | ✅ 已验证 | 否 |
| `string.at(int)` 返回 char code | ✅ | ✅ 已有 | 否 |
| `string.substr(start, len)` | ✅ | ✅ 已有 | 否 |
| `string.to_float()` → Result | ✅ | ✅ 已有 | 否 |
| `&!enum` 可写借用 | 可选 | ❓ 未验证 | 否（有降级方案） |
| `vec(JsonValue)` 其中 JsonValue 是 has_drop enum | ✅ | ✅ 已支持 | 否 |
| `map(string, JsonValue)` 其中 value 是 has_drop enum | ✅ | ✅ 已支持 | 否 |

### 结论：**所有阻塞项已解除** ✅

L-006 已修复（2026-05-20），所有基础设施（string 方法、match、Result、enum vec/map payload、递归下降所需的控制流）全部就绪。可以立即开始编写 `std/json.ls`。

---

## 5. 完整使用示例

```ls
import std.json as json

fn main() {
    // ---- 解析 ----
    string raw = "{\"name\": \"Alice\", \"age\": 30, \"scores\": [95, 87, 72]}"
    JsonValue doc = try json.parse(raw)

    // ---- 读取 ----
    match json.object_get(doc, "name") {
        Ok(v) => {
            match v {
                Str(s) => print(f"Name: {s}")
                _ => {}
            }
        }
        Err(e) => print(e)
    }

    // ---- 构建 ----
    JsonValue obj = json.object()
    json.object_set(&!obj, "greeting", json.str("hello"))
    json.object_set(&!obj, "count", json.number_int(42))

    JsonValue arr = json.array()
    json.array_push(&!arr, json.number_int(1))
    json.array_push(&!arr, json.number_int(2))
    json.object_set(&!obj, "items", arr)

    // ---- 输出 ----
    string compact = json.stringify(obj)
    print(compact)
    // → {"greeting":"hello","count":42,"items":[1,2]}

    string pretty = json.stringify_pretty(obj, 2)
    print(pretty)
    // → {
    //     "greeting": "hello",
    //     "count": 42,
    //     "items": [
    //       1,
    //       2
    //     ]
    //   }
}
```

---

## 6. 实施计划

```
Step 0   ✅ 修复 L-006（已完成 2026-05-20）
Step 1   ✅ JsonValue enum + 构造函数 + is_*/as_* 访问器（已完成 2026-05-20）
           ⚠️  函数名略有调整（str_val/array_new/object_new）
           ⚠️  Object 多了 vec(string) keys 字段以保留插入顺序
Step 2   ✅ parse()：递归下降解析器（已完成 2026-05-20）
Step 3   ✅ stringify() / stringify_pretty()（已完成 2026-05-20）
Step 4   ❌ array_push/array_get/object_set/object_get 可写 API（未实现）
           前置问题：&!enum 借用是否支持 待验证；可考虑按值传参降级方案
Step 5   ✅ 测试通过（已完成 2026-05-24）
           ctest test_std_json A~E 全绿
           e2e memcheck：0 double-free / 0 leak（bugs/20 修复后，commit 6c25fa8）
```

### 测试计划（已完成项）

```
test_std_json（ctest）：
  A. 基本类型解析（null/bool/number/string）          ✅
  B. array / object / 嵌套结构解析                   ✅
  C. stringify 往返（round-trip）                    ✅
  D. 文件读取 + parse（json_file_test.ls）            ✅
  E. e2e 端到端（json_e2e_test.ls）+ --memcheck      ✅（bugs/20 修复后）
```

### 待办

- [ ] Step 4：实现 `array_push` / `array_get` / `object_set` / `object_get`
- [ ] 验证 `&!JsonValue` 可写借用是否可行，或采用降级按值方案
- [ ] 只读 API（`array_len` / `object_has` / `object_keys`）改为借用 ABI（`&JsonValue`）
