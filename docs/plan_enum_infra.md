# Enum 基础设施完善计划

> **日期**：2026-05-24  
> **前置**：bugs/20 修复（has-drop enum double-free in match-assign + return）  
> **目标**：补齐 has-drop enum 在所有代码路径中的内存安全，为 Step 4（JSON 可写 API）扫清障碍  

---

## 1. Enum 内存模型完整分析

### 1.1 数据布局

```
enum JsonValue {          // has_drop = true
    Null                  // disc=0, payload=zeroed
    Bool(bool val)        // disc=1, payload={i1}
    Number(f64 val)       // disc=2, payload={f64}
    Str(string val)       // disc=3, payload={*i8, i32, i32}  ← heap-owning
    Array(vec(JsonValue)) // disc=4, payload={*i8, i32, i32}  ← heap-owning (递归)
    Object(vec(string) keys, map(string,JsonValue) entries)
                          // disc=5, payload={vec, map}       ← heap-owning (递归)
}

LLVM IR layout: { i8 disc, [N x i8] payload_union }
  - N = max(variant payload sizes), 按 ABI 对齐
  - 自递归 variant（payload 含 JsonValue）自动 box（指针化）
```

### 1.2 所有权状态机

```
变量状态:  ALIVE ──move──→ MOVED ──reassign──→ ALIVE
                                               ↑ (clear moved_flag)

跟踪机制:  moved_flag (i1 alloca)
  - 初始化: 0 (alive)
  - move 后: 1 (moved, scope cleanup 跳过 drop)
  - 重新赋值后: 0 (alive again)
```

### 1.3 六大操作的所有权规则

| 操作 | 规则 | 实现位置 |
|------|------|----------|
| **构造** `Ok(v)` | payload 按类型：rvalue 直接 store；IDENT owned → 标 moved_flag；IDENT borrowed → clone | `emit_enum_ctor` L15374 |
| **Drop** | 按 disc switch，逐 variant drop 所有 heap 字段 | `emit_auto_enum_drop_fn` L14866 |
| **Clone** | 按 disc switch，逐 variant deep-clone 所有 heap 字段 | `emit_auto_enum_clone_fn` L15140 |
| **Scope 清理** | 条件 drop（检查 moved_flag） | `emit_enum_drop_cond` L15138 |
| **参数传递** | 默认 clone（IDENT）；`__move()` 转移所有权 | call site L10534 |
| **返回** | 返回的 IDENT 加入 skip list，scope cleanup 跳过 | `AST_RETURN` L13130 |

---

## 2. 缺口清单（Bug Inventory）

### 2.1 🔴 P0: `elem_needs_drop` 漏检 TYPE_ENUM(has_drop)

以下 **8 处** `elem_needs_drop` 判断只检查了 `TYPE_STRING` 和 `TYPE_STRUCT(has_drop)`，
**漏掉了 `TYPE_ENUM(has_drop)`**：

| # | 行号 | 位置 | 影响 |
|---|------|------|------|
| 1 | 2688 | scope cleanup: vec 元素 drop | **LEAK**: 独立 `vec(MyEnum)` 离开作用域 → 元素不 drop |
| 2 | 6761 | `vec.clear()` | **LEAK**: 清空 `vec(MyEnum)` → 元素不 drop |
| 3 | 7180 | `vec.truncate()` | **LEAK**: 截断 `vec(MyEnum)` → 超出部分不 drop |
| 4 | 7550 | `vec.extend()` 判断 | **DOUBLE-FREE**: 走 memcpy 路径而非 clone |
| 5 | 8008 | `vec.resize()` 缩小时 | **LEAK**: 缩小 `vec(MyEnum)` → 超出元素不 drop |
| 6 | 8117 | `vec.copy()` 判断 | **DOUBLE-FREE**: 走 memcpy 路径而非 clone |
| 7 | 8500 | `vec.slice()` 判断 | **DOUBLE-FREE**: 走 memcpy 路径而非 clone |
| 8 | 14178 | closure env drop: vec 元素 | **LEAK**: 闭包 env 释放时 vec(MyEnum) 元素不 drop |

**注意**：`emit_vec_elem_drop_at()` (L6404) 本身**已正确处理** enum——问题出在**守卫条件**不让它被调用。

**修复方案**：在上述 8 处 `elem_needs_drop` 判断中加入：
```c
(elem_type->kind == TYPE_ENUM && elem_type->as.enom.has_drop)
```

### 2.2 🔴 P0: vec 方法 clone 路径漏检 TYPE_ENUM(has_drop)

以下 **4 处** vec 方法在"读取元素"时只 clone `string` 和 `struct(has_drop)`，
**不 clone `enum(has_drop)`**，导致浅拷贝 → **DOUBLE-FREE**：

| # | 行号 | 方法 | 修复 |
|---|------|------|------|
| 9 | 6928–6931 | `vec.first()` | 加 `emit_enum_clone_val` 分支 |
| 10 | 7016–7020 | `vec.last()` | 加 `emit_enum_clone_val` 分支 |
| 11 | 7112–7116 | `vec.get(i)` | 加 `emit_enum_clone_val` 分支 |
| 12 | 7612–7615 | `vec.extend()` clone loop | 错调 `emit_struct_clone_val`，应改为分支判断 |

### 2.3 🔴 P0: AST_VAR_DECL 缺少 has-drop enum 的 IDENT 克隆

| # | 行号 | 场景 | 影响 |
|---|------|------|------|
| 13 | 12507–12597 | `JsonValue b = a` (IDENT 初始化) | **DOUBLE-FREE**: 浅拷贝，两个变量共享 payload |

**修复**：在 `else` 分支 (L12507) 中，检测 `var_type->kind == TYPE_ENUM && has_drop`，
对 IDENT 源调用 `emit_enum_clone_val`。与 `TYPE_STRUCT` 的处理对称。

### 2.4 🟡 P1: 未初始化 has-drop enum 变量未零初始化

| # | 行号 | 场景 | 影响 |
|---|------|------|------|
| 14 | 12337 附近 | `JsonValue v;` (无 init) | **潜在 CRASH**: 垃圾 payload → drop 时访问野指针 |

**修复**：在 `AST_VAR_DECL` 中增加：
```c
if (var_type->kind == TYPE_ENUM && var_type->as.enom.has_drop)
    LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
```

### 2.5 🟡 P1: match 非 string 的 has-drop binder 共享所有权

**现状**：match binder 对非 string 类型（vec/map/struct/enum payload）设 `is_borrowed=true`。
binder 只能读取，不能 move/修改/返回 payload 的内部字段。

**这是设计决策，不是 bug**。但会影响 Step 4 API 设计——无法通过 match 解构后 `&!` 修改 vec/map payload。

---

## 3. Enum vs Vec 能力差异矩阵

| 能力维度 | vec(T) | has-drop enum | 差距 |
|----------|--------|---------------|------|
| **方法调用** | 31 个方法 | 0 个方法（仅值操作） | enum 无 method 体系 |
| **可变借用 `&!`** | ✅ 支持（函数参数） | ❌ 不支持 | 阻塞 Step 4 |
| **只读借用 `&`** | ✅ 支持 | ❌ 不支持（仅 match 内 binder） | — |
| **var_decl IDENT clone** | N/A（vec 不 clone） | ❌ 缺失 | Bug #13 |
| **scope cleanup 元素 drop** | ⚠️ 漏 enum | ✅ enum 自身 drop 正确 | Bug #1 |
| **vec[i] 读取 clone** | ✅ clone enum | ✅ | 正确 |
| **push clone** | ✅ clone borrowed enum | ✅ | 正确 |
| **pop drop** | ✅ via `emit_vec_elem_drop_at` | ✅ | 正确 |
| **remove drop** | ✅ via `emit_vec_elem_drop_at` | ✅ | 正确 |
| **clear/truncate/resize drop** | ❌ 漏 enum | — | Bug #2,3,5 |
| **copy/slice clone** | ❌ 漏 enum | — | Bug #6,7 |
| **extend clone** | ❌ 漏 enum | — | Bug #4 |
| **first/last/get clone** | ❌ 漏 enum | — | Bug #9,10,11 |
| **filter/find clone** | ✅ 正确处理 enum | — | 正确 |
| **零初始化** | ✅ | ❌ 缺失 | Bug #14 |

---

## 4. Step 4 对 Enum 实现的要求分析

### 4.1 Step 4 目标 API

```ls
fn array_push(&!JsonValue arr, JsonValue item)
fn array_get(&JsonValue arr, int index) -> Result(JsonValue, string)
fn object_set(&!JsonValue obj, string key, JsonValue val)
fn object_get(&JsonValue obj, string key) -> Result(JsonValue, string)
```

### 4.2 `&!enum` 可变借用需求分析

Step 4 的 `array_push` 和 `object_set` 需要 `&!JsonValue`（可变借用 enum）。
**当前 `&!enum` 完全不支持**：parser 不解析、checker 不验证、codegen 无 ABI。

**实现代价极高**：
- 需要让 enum 参数走指针 ABI（当前 enum 始终 by-value）
- 需要支持通过 `&!` 指针 match 并修改 payload
- 需要确保 match arm 内修改 payload 后 discriminant 不变（类型安全）
- 需要生命期检查（防止悬垂引用）

### 4.3 推荐降级方案

**不实现 `&!enum`**。改用纯函数式 API——消费旧值、返回新值：

```ls
// 方案 A：consume-and-return（推荐）
fn array_push(JsonValue arr, JsonValue item) -> JsonValue
fn array_get(JsonValue arr, int index) -> Result(JsonValue, string)
fn object_set(JsonValue obj, string key, JsonValue val) -> JsonValue
fn object_get(JsonValue obj, string key) -> Result(JsonValue, string)

// 用法：
JsonValue obj = json.object_new()
obj = json.object_set(obj, "name", json.str_val("hello"))
obj = json.object_set(obj, "age", json.number_int(30))
```

**优点**：
- 无需编译器改动（enum 基础设施足够）
- 所有权语义清晰——旧值 move 进去、新值 move 出来
- 与 LS 现有所有权系统一致

**缺点**：
- 每次 set 可能涉及 clone + drop（但 `__move()` 可优化）
- 语法略冗余（`obj = json.object_set(obj, ...)` vs `json.object_set(&!obj, ...)`）

### 4.4 方案 A 对 enum 基础设施的要求

| 需求 | 当前状态 | 需要修复 |
|------|----------|----------|
| has-drop enum 按值传参 + clone | ✅ 已有 | — |
| has-drop enum 按值返回 + skip cleanup | ✅ bugs/20 修复 | — |
| has-drop enum var_decl clone | ❌ 缺失 | Bug #13 |
| `vec(MyEnum)` scope drop | ❌ 缺失 | Bug #1 |
| `vec(MyEnum)` clear/truncate/resize | ❌ 缺失 | Bug #2,3,5 |
| `vec(MyEnum)` copy/slice/extend | ❌ 缺失 | Bug #4,6,7 |
| `vec(MyEnum)` first/last/get | ❌ 缺失 | Bug #9,10,11 |
| 零初始化 | ❌ 缺失 | Bug #14 |
| match binder 提取 vec/map payload → 操作 | ✅ 可通过 borrowed binder 读取 | — |

**结论**：需先修复 14 处 bug，才能安全实现 Step 4。

---

## 5. 实现计划

### Phase E-1：补齐 `elem_needs_drop`（8 处守卫条件）

**范围**：全部 `elem_needs_drop` 定义加入 `TYPE_ENUM(has_drop)`。

**改动点**（每处仅加一行）：
```c
// 修改前:
bool elem_needs_drop = (elem_type &&
    (elem_type->kind == TYPE_STRING ||
     (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop)));

// 修改后:
bool elem_needs_drop = (elem_type &&
    (elem_type->kind == TYPE_STRING ||
     (elem_type->kind == TYPE_STRUCT && elem_type->as.strukt.has_drop) ||
     (elem_type->kind == TYPE_ENUM   && elem_type->as.enom.has_drop)));
```

**影响行号**：2688, 6761, 7180, 7550, 8008, 8117, 8500, 14178

**工期**：0.5 天

### Phase E-2：补齐 vec 方法 clone 路径（4 处读取方法）

**改动**：`first()`, `last()`, `get(i)` 各加一个 `else if` 分支：
```c
else if (elem_type->kind == TYPE_ENUM && elem_type->as.enom.has_drop)
    elem = emit_enum_clone_val(ctx, elem, elem_type);
```

`extend()` 的 clone loop (L7612-7615) 改为三分支判断：
```c
if (elem_type->kind == TYPE_STRING)
    cloned = emit_string_clone_val(ctx, se);
else if (elem_type->kind == TYPE_ENUM && elem_type->as.enom.has_drop)
    cloned = emit_enum_clone_val(ctx, se, elem_type);
else
    cloned = emit_struct_clone_val(ctx, se, elem_llvm, elem_type);
```

**影响行号**：6928–6931, 7016–7020, 7112–7116, 7612–7615

**工期**：0.5 天

### Phase E-3：AST_VAR_DECL + 零初始化

**改动 1**：在 `else` 分支 (L12507 区域) 中，TYPE_STRUCT 判断之后增加：
```c
else if (var_type->kind == TYPE_ENUM && var_type->as.enom.has_drop &&
         ast_unwrap_move(node->as.var_decl.init)->kind == AST_IDENT)
{
    init = emit_enum_clone_val(ctx, init, var_type);
}
```

**改动 2**：在零初始化区域 (L12337 附近) 增加：
```c
if (var_type->kind == TYPE_ENUM && var_type->as.enom.has_drop)
{
    LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);
}
```

**工期**：0.5 天

### Phase E-4：Step 4 JSON API 实现

在 `std/json.ls` 中实现 consume-and-return 风格 API。

**工期**：1 天

### 总工期

```
Phase E-1  elem_needs_drop 补全            0.5 天
Phase E-2  vec 方法 clone 补全             0.5 天
Phase E-3  var_decl + 零初始化             0.5 天
Phase E-4  Step 4 JSON API                 1 天
                                          ─────────
总计                                       2.5 天
```

---

## 6. TDD 测试计划

### 6.1 新增测试文件

#### `tests/samples/enum_has_drop_vec_test.ls`

**目标**：验证 `vec(MyEnum)` 的所有内存操作正确（0 leak, 0 dfree）。

```ls
// 用一个简单的 has_drop enum 来测试
enum Data {
    Empty
    Text(string s)
    Items(vec(string) items)
}

fn main() {
    // ---- A: 基本构造 + scope drop ----
    vec(Data) v1 = [Text("a".copy()), Text("b".copy())]
    print("A: len =", v1.length)
    // v1 离开作用域 → 应 drop 每个 Data 的 string payload

    // ---- B: push + pop ----
    vec(Data) v2 = []
    v2.push(Text("hello".copy()))
    v2.push(Items(["x".copy()]))
    v2.pop()
    print("B: len =", v2.length)
    // v2 离开作用域 → drop 剩余元素

    // ---- C: clear ----
    vec(Data) v3 = [Text("c1".copy()), Text("c2".copy()), Text("c3".copy())]
    v3.clear()
    print("C: len =", v3.length)

    // ---- D: first / last / get (clone on read) ----
    vec(Data) v4 = [Text("d1".copy()), Text("d2".copy()), Text("d3".copy())]
    Data f = v4.first()
    Data l = v4.last()
    Data g = v4.get(1)
    print("D: first/last/get done")
    // f, l, g 各自独立 → drop 不影响 v4

    // ---- E: copy (deep clone) ----
    vec(Data) v5 = [Text("e1".copy()), Text("e2".copy())]
    vec(Data) v5_copy = v5.copy()
    print("E: copy len =", v5_copy.length)
    // v5 和 v5_copy 独立 → 各自 drop

    // ---- F: extend (deep clone from source) ----
    vec(Data) v6 = [Text("f1".copy())]
    vec(Data) v6_src = [Text("f2".copy()), Text("f3".copy())]
    v6.extend(&v6_src)
    print("F: extend len =", v6.length)

    // ---- G: truncate ----
    vec(Data) v7 = [Text("g1".copy()), Text("g2".copy()), Text("g3".copy())]
    v7.truncate(1)
    print("G: truncate len =", v7.length)

    // ---- H: remove ----
    vec(Data) v8 = [Text("h1".copy()), Text("h2".copy()), Text("h3".copy())]
    v8.remove(1)
    print("H: remove len =", v8.length)

    // ---- I: slice (deep clone) ----
    vec(Data) v9 = [Text("i1".copy()), Text("i2".copy()), Text("i3".copy())]
    vec(Data) v9_sl = v9.slice(0, 2)
    print("I: slice len =", v9_sl.length)

    // ---- J: resize (shrink drops, grow zero-fills) ----
    vec(Data) v10 = [Text("j1".copy()), Text("j2".copy()), Text("j3".copy())]
    v10.resize(1)
    print("J: resize len =", v10.length)

    print("all done")
}
```

**验证**：`ls run --memcheck enum_has_drop_vec_test.ls` → `0 leak, 0 double-free, 0 invalid free`

#### `tests/samples/enum_var_decl_test.ls`

**目标**：验证 has-drop enum 变量声明的所有权语义。

```ls
enum Value {
    None
    Num(f64 n)
    Txt(string s)
    Pair(string a, string b)
}

fn make_txt() -> Value {
    return Txt("hello".copy())
}

fn main() {
    // A: var_decl from rvalue (call) — no clone needed
    Value a = make_txt()
    print("A done")

    // B: var_decl from IDENT — must clone
    Value b = a
    print("B done")
    // a and b independent → both drop cleanly

    // C: reassignment
    Value c = Txt("old".copy())
    c = Txt("new".copy())
    print("C done")
    // old "old" dropped, c holds "new"

    // D: uninitialized var + later assign
    Value d
    d = Pair("x".copy(), "y".copy())
    print("D done")

    // E: match + re-wrap
    Value e = Txt("match_me".copy())
    match e {
        Txt(s) => { print("E:", s) }
        _ => {}
    }

    print("all done")
}
```

**验证**：`ls run --memcheck enum_var_decl_test.ls` → `0 leak, 0 double-free, 0 invalid free`

#### `tests/samples/enum_nested_vec_test.ls`

**目标**：验证嵌套结构 `vec(vec(MyEnum))` 和 `map(string, MyEnum)` 的正确 drop/clone。

```ls
enum JVal {
    JNull
    JStr(string s)
    JArr(vec(JVal) items)
}

fn main() {
    // A: vec of enum, each containing vec of enum (nested)
    vec(JVal) inner = [JStr("a".copy()), JStr("b".copy())]
    JVal arr = JArr(inner)
    vec(JVal) outer = []
    outer.push(arr)
    print("A: outer.length =", outer.length)
    // outer → JArr → vec → JStr → string  全链 drop

    // B: copy nested structure
    vec(JVal) outer2 = outer.copy()
    print("B: copy done, len =", outer2.length)
    // outer 和 outer2 完全独立

    // C: index read from nested
    JVal elem = outer[0]
    print("C: index read done")
    // elem 是 deep clone

    print("all done")
}
```

### 6.2 现有测试回归检查

每个 Phase 完成后必须通过：

```powershell
cd build && ctest --output-on-failure -C Release
```

**目标**：55/55 + 新增测试 = 全绿。

### 6.3 Memcheck 矩阵

| 测试文件 | JIT | memcheck | 预期 |
|----------|-----|----------|------|
| `enum_has_drop_vec_test.ls` | ✅ | ✅ 0/0/0 | 新增 |
| `enum_var_decl_test.ls` | ✅ | ✅ 0/0/0 | 新增 |
| `enum_nested_vec_test.ls` | ✅ | ✅ 0/0/0 | 新增 |
| `json_e2e_test.ls` | ✅ | ✅ 0/0/0 | 现有（不回归） |
| `enum_string_test.ls` | ✅ | ✅ 0/0/0 | 现有（不回归） |
| `enum_recursive_test.ls` | ✅ | ✅ 0/0/0 | 现有（不回归） |
| `enum_vec_payload_test.ls` | ✅ | ✅ 0/0/0 | 现有（不回归） |

### 6.4 CMake 集成

新增一个 ctest target `test_enum_has_drop_infra`，包含上述 3 个测试文件的 JIT + memcheck 验证。

---

## 7. 风险与注意事项

1. **`emit_vec_elem_drop_at` 已正确处理 enum**——只需修守卫条件，不需改 drop 逻辑本身
2. **`emit_enum_clone_val` 已正确实现**——只需在缺失位置调用即可
3. **Phase E-1 是最高优先级**——scope cleanup 的 vec 元素 drop 影响所有 `vec(MyEnum)` 场景
4. **修改 `elem_needs_drop` 后，需确认没有"多余 drop"**——对 non-has_drop enum 不应触发
5. **`vec.find()` 方法 (L8980) 已有 enum clone 支持 (L8950)**——不在缺口列表中
