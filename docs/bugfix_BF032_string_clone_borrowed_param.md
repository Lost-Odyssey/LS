# BF-032：`emit_string_clone_val` 对 borrowed 参数（cap=0）跳过克隆 → 悬垂指针

> **日期**：2026-05-26
> **严重度**：WRONG（数据腐败 / use-after-free）
> **影响范围**：所有通过函数参数传递 string 并存入 has_drop enum 的路径
> **修复文件**：`src/codegen.c` — `emit_enum_ctor` 中 string payload 处理

---

## 1. 症状

`json.stringify(obj)` 对包含 Str 值的 Object 产生腐败输出：

```
期望: {"name":"Bob"}
实际: {"name":"nam"}     ← "name" key 的前 3 字节
      {"name":"{\"n"}   ← 正在构建的输出前 3 字节
      {"name":"B        ← 截断
```

复现率约 ~15%（取决于 malloc 是否复用同一地址）。

### 最小复现

```ls
import std.json as json
fn main() {
    JsonValue obj = JsonValue.object_new()
    obj.set("name".copy(), JsonValue.str_val("Bob".copy()))
    string s = json.stringify(obj)
    if s == "{\"name\":\"Bob\"}" { print("PASS") }
    else { print(f"FAIL: {s}") }
}
```

### 隔离测试结果

| 测试 | 结果 | 含义 |
|------|------|------|
| `json_obj_num.ls`（Number 值） | 30/30 PASS | Number payload 无堆数据，不触发 |
| `json_obj_one.ls`（Str 值） | ~25/30 PASS | **Str payload 触发悬垂指针** |
| `json_str_only.ls`（纯 Str，无 Object） | 30/30 PASS | 不经过 Object 的 map.get/stringify 路径 |
| `enum_map_inline.ls`（本地 enum，同结构） | 30/30 PASS | 本地 enum ctor 内联在 main 中，参数未经 cap=0 |
| memcheck | 0 errors | memcheck 不检测 use-after-free **读取** |

---

## 2. 根因分析

### 2.1 背景：LsString 的三态

LS 的字符串类型 `LsString = {ptr data, i32 len, i32 cap}` 有三种状态：

| 状态 | cap 值 | 含义 |
|------|--------|------|
| Static | 0 | 指向 .rodata，永不释放 |
| Owned | > 0 | 指向 malloc 的堆内存，scope cleanup 释放 |
| Moved | -1 | 已转移所有权，scope cleanup 跳过 |

### 2.2 关键机制：函数参数 cap 置零

`codegen_fn_decl`（line 14762-14769）对所有 `TYPE_STRING` 参数在函数入口处将 cap 设为 0：

```c
/* String parameters: zero out cap so emit_string_free treats them as
   borrowed (cap=0 → skip). The caller owns the original data buffer. */
if (param_type && param_type->kind == TYPE_STRING)
{
    LLVMValueRef str_val = LLVMBuildLoad2(..., alloca, "param.str");
    LLVMValueRef zero32 = LLVMConstInt(..., 0, 0);
    str_val = LLVMBuildInsertValue(..., str_val, zero32, 2, "param.borrow");
    LLVMBuildStore(..., str_val, alloca);
}
```

这使得函数退出时 scope cleanup 跳过释放参数字符串（caller 拥有原始 buffer）。

### 2.3 `emit_string_clone_val` 的克隆条件

```c
LLVMValueRef is_owned = LLVMBuildICmp(..., LLVMIntSGT, cap, zero32, "sc.owned");
LLVMBuildCondBr(..., is_owned, copy_bb, skip_bb);
```

**只在 `cap > 0` 时克隆**。cap=0 的字符串（无论是真正的 .rodata 还是 borrowed 参数）都跳过。

### 2.4 bug 路径

以 `JsonValue.str_val(string s)` 为例：

```ls
// std/json.ls
fn str_val(string s) -> JsonValue {
    return Str(s)
}
```

调用链：

```
1. caller: JsonValue.str_val("Bob".copy())
   └─ "Bob".copy() 产生一个 owned string: {heap_ptr, 3, 16}
   └─ 按值传递给 str_val

2. str_val 入口：参数 s 的 cap 被置 0
   └─ s = {heap_ptr, 3, 0}   ← 看起来像 "static" 但实际指向 caller 堆

3. Str(s) → emit_enum_ctor → string payload 走 else 分支
   └─ emit_string_clone_val(ctx, v)
   └─ v.cap = 0 → is_owned = false → skip_bb → 不克隆！
   └─ enum payload 直接存储 {heap_ptr, 3, 0}

4. str_val 返回 enum 给 caller

5. caller 的 temp 系统释放 "Bob".copy() 的 heap buffer
   └─ heap_ptr 现在是悬垂指针

6. 后续 malloc 可能复用同一地址（分配给 key "name" 等）
   └─ 读取 enum 内的 string → 得到 "nam"（"name" 的前 3 字节）
```

### 2.5 为什么本地 enum 不触发？

```ls
// 本地测试 — 30/30 PASS
enum Val { Str(string s) }
fn main() {
    Val v = Str("Bob".copy())
    // ...
}
```

这里 `Str("Bob".copy())` 的 enum ctor 在 `main` 函数内直接内联。
`"Bob".copy()` 是 AST_CALL（rvalue transfer）→ 走 `is_rvalue_transfer = true` 路径 → 直接 store，不经过 `emit_string_clone_val`。

只有**跨函数调用**时（参数经过 cap=0 标记），bug 才暴露。

---

## 3. 修复方案

### 3.1 考虑过的方案

| 方案 | 优点 | 缺点 |
|------|------|------|
| A: 修改 `emit_string_clone_val` 条件为 `cap>0 \|\| len>0` | 一行改动，全局生效 | 静态字面量 `"Alice"` 也被克隆 → 传给 map.set 时原始 clone 无人释放 → **新增 leak** |
| B: `emit_enum_ctor` 中对所有 non-rvalue 无条件克隆 | 简单 | 同 A，静态字面量多余克隆导致 leak |
| **C: `emit_enum_ctor` 中仅对 AST_IDENT 无条件克隆** | 精确区分 borrowed 参数 vs 静态字面量 | 稍多代码 |

### 3.2 最终实施：方案 C

在 `emit_enum_ctor` 的 string payload 处理中，区分 AST_IDENT 和非 IDENT 源：

```c
else  /* non-rvalue string payload */
{
    AstNode *sarg = ast_unwrap_move(args[i]);
    if (sarg && sarg->kind == AST_IDENT)
    {
        /* BF-032: AST_IDENT may be a borrowed function parameter
           (cap=0 but pointing to caller's heap). Unconditional deep
           clone ensures the enum owns its own buffer. For local
           variables holding static literals, this is a harmless
           extra copy (the enum correctly frees it via drop). */
        // ... malloc + memcpy unconditional clone ...
    }
    else
    {
        /* Non-IDENT (string literal, field access, etc.) — use
           emit_string_clone_val. Static literals (cap=0) are
           safely shared with .rodata and don't need cloning. */
        LLVMValueRef cloned = emit_string_clone_val(ctx, v);
        LLVMBuildStore(ctx->builder, cloned, field_ptr);
    }
}
```

### 3.3 为什么方案 C 不引入 leak

| 场景 | 路径 | 结果 |
|------|------|------|
| `Str(s)` — s 是 borrowed 参数 | AST_IDENT → 无条件 clone → enum owns {heap, 3, 16} | ✅ 修复悬垂指针 |
| `Str(s)` — s 是普通 local | AST_IDENT → 无条件 clone → enum owns copy | ✅ 正确（多余但安全） |
| `Str("hello")` — 字面量 | 非 IDENT → emit_string_clone_val → cap=0 skip → enum 共享 .rodata | ✅ 无 leak |
| `Str("hello".copy())` — rvalue | AST_CALL → is_rvalue_transfer → 直接 store + pop temp | ✅ 无 leak |
| `Str(f"value {x}")` — rvalue | AST_CALL → is_rvalue_transfer → 直接 store + pop temp | ✅ 无 leak |

---

## 4. 影响范围

所有满足以下条件的路径：
- 函数接收 `string` 参数（cap 被置 0）
- 将该参数存入 has_drop enum 构造器

具体包括：

```ls
// std.json
fn str_val(string s) -> JsonValue { return Str(s) }

// Result 错误构造
fn err(string msg) -> Result(T, string) { return Err(msg) }

// 任何用户定义的 enum + string payload
fn make_label(string text) -> Widget { return Label(text) }
```

---

## 5. 验证

| 测试 | 修复前 | 修复后 |
|------|--------|--------|
| `json_obj_one.ls` × 50 | ~42/50 PASS | **50/50 PASS** |
| `json_obj_only.ls` × 30 | ~26/30 PASS | **30/30 PASS** |
| `json_infra_test.ls` memcheck | 0 errors | **0 errors** |
| `json_obj_one.ls` memcheck | 0 errors | **0 errors** |
| ctest 完整回归 | 59/59 (corruption undetected) | **59/59** |

---

## 6. 教训

1. **`cap == 0` 是重载语义**：既表示"静态 .rodata"又表示"borrowed 参数"。`emit_string_clone_val` 无法区分这两种情况。在需要获取所有权的场景（enum ctor、struct ctor），不能依赖 `emit_string_clone_val` 的 cap 判断。

2. **跨函数边界是关键**：enum ctor 在单函数内部使用时（本地变量直接构造），string 参数通常是 rvalue（AST_CALL/AST_TRY）走直接 store 路径。只有跨函数调用时，参数经过 cap=0 借用标记，才暴露此 bug。这解释了为什么大量单文件测试通过而 import 场景失败。

3. **memcheck 盲区**：LS 的 memcheck 只检测 double-free / invalid-free / leak-at-exit。**Use-after-free 读取**（本 bug 的核心模式）不被检测。只有 AddressSanitizer 能可靠捕获此类问题。

4. **flaky ≠ 非确定性**：本 bug 被描述为"~15% 概率失败"，但根因是 100% 确定的悬垂指针。flaky 表现只是因为 malloc 是否复用同一地址是非确定的。遇到"偶发腐败"应首先怀疑 use-after-free。

5. **对称审查法的扩展**：BF-004 教训是"为 struct 加的 clone 必须同步检查 enum"。本 bug 揭示了更深层的对称性——`emit_string_clone_val` 的 "skip static" 逻辑在**所有消费 string 获取所有权**的站点都可能出问题。应建立清单审查所有调用 `emit_string_clone_val` 后将结果存入 heap-owning 容器的路径。

---

## 7. 相关 Bug

| 编号 | 关联 |
|------|------|
| BF-005 | match binder string 共享 data 指针（同源：emit_string_clone_val skip） |
| BF-021 | enum ctor 对 has_drop enum payload 浅拷贝（同源：缺少 clone 分支） |
| BF-024 | vec.push borrowed match binder 浅拷贝（同源：borrowed 未 clone） |
| bugs/21.txt | 本 bug 的初始调查记录（跨 session 追踪） |
