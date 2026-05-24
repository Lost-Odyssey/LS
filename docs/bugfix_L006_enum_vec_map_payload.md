# L-006 修复报告：enum 含 vec/map payload 的 drop + AOT main 退出码

> **修复日期**：2026-05-20
> **影响范围**：`src/codegen.c`、`src/codegen.h`、`CMakeLists.txt`
> **测试**：`tests/test_enum_vec_map_payload.cmake`、`tests/samples/enum_vec_payload_test.ls`
> **ctest**：52/52 通过

---

## 1. 问题描述

以下 LS 代码在 JIT 和 AOT 路径下均会 crash 或返回非零退出码：

```ls
enum Data {
    Empty
    Numbers(vec(int) nums)
    Lookup(map(string, int) table)
}

fn process(Data d) {
    match d {
        Numbers(nums) => { print(f"len={nums.length}") }
        _ => { print("other") }
    }
}

fn main() {
    vec(int) v = [10, 20, 30]
    Data d = Numbers(v)
    process(d)      // ← crash 或 exit code 非零
}
```

**症状**：
- JIT 路径：segfault（双重释放同一 data buffer）
- AOT 路径：输出正确但退出码为 1（非零）

**已有的 enum drop 基础设施**仅覆盖 string / struct(has_drop) / 自递归 box payload。vec 和 map payload 在构造、克隆、析构三个环节均存在缺陷。

---

## 2. 根因分析

### 2.1 根因 1：`emit_enum_ctor` 未 move 源 vec/map

**位置**：`codegen.c` → `emit_enum_ctor()`

**机制**：enum 构造器 `Numbers(v)` 将 vec 值（`{data_ptr, len, cap}`）存入 enum payload struct 的对应字段。但存储后没有将源变量 `v` 的 `cap` 置 0（即执行 move 语义标记）。

**后果**：

```
构造后的状态：
  v     → { data_ptr=0x1000, len=3, cap=3 }   ← 仍然 "拥有" 数据
  d.payload → { data_ptr=0x1000, len=3, cap=3 }   ← 也 "拥有" 同一份数据

scope cleanup 时：
  1. Data.__drop(&d) → disc==1 → cap>0 → free(0x1000) ✓
  2. vec cleanup(v)  → cap>0 → free(0x1000) ← double-free!
```

**修复**：

在 `emit_enum_ctor` 中，对 vec/map payload 字段添加 move 标记分支：

```c
// vec/map: 存入 payload 后，将源变量的 cap 置 0
else if (pt && (pt->kind == TYPE_VECTOR || pt->kind == TYPE_MAP))
{
    LLVMBuildStore(ctx->builder, v, field_ptr);
    // 找到源变量的 alloca，load → insertvalue cap=0 → store 回去
    AstNode *arg_node = ast_unwrap_move(args[i]);
    if (arg_node && arg_node->kind == AST_IDENT)
    {
        CgSymbol *src_sym = cg_scope_resolve(..., arg_node->as.ident.name);
        if (src_sym && src_sym->value)
        {
            LLVMTypeRef cont_t = (pt->kind == TYPE_VECTOR)
                ? ls_vec_type(ctx) : ls_map_type(ctx);
            LLVMValueRef cur = LLVMBuildLoad2(..., cont_t, src_sym->value, "ecm.cur");
            LLVMValueRef zeroed = LLVMBuildInsertValue(..., cur,
                LLVMConstInt(i32, 0, 0), /*cap_index=*/2, "ecm.zc");
            LLVMBuildStore(..., zeroed, src_sym->value);
        }
    }
}
```

对 struct(has_drop) 走 moved_flag 路径（与闭包 capture 一致）。

### 2.2 根因 2：`emit_enum_clone_val` 缺少 vec/map 支持

**位置**：`codegen.c` → `emit_enum_clone_val()`

**机制**：当 has_drop enum 作为函数参数传递时，call site 调用 `emit_enum_clone_val` 进行深拷贝（避免 caller 和 callee 共享同一 payload 的堆内存）。但该函数的 `needs` 检查只覆盖了：

```c
// 修复前
if (pt->kind == TYPE_STRING ||
    (pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop) ||
    (pt->kind == TYPE_ENUM   && pt->as.enom.has_drop))
```

缺少 `TYPE_VECTOR` 和 `TYPE_MAP`。

**后果**：vec/map payload 被浅拷贝（bitwise copy），caller 和 callee 共享 data_ptr → double-free。

**修复**：

```c
// 修复后
if (pt && (pt->kind == TYPE_STRING ||
           pt->kind == TYPE_VECTOR ||
           pt->kind == TYPE_MAP ||
           (pt->kind == TYPE_STRUCT && pt->as.strukt.has_drop) ||
           (pt->kind == TYPE_ENUM   && pt->as.enom.has_drop)))
```

并在 per-field 循环中添加 vec/map 克隆分支：

```c
else if (pt->kind == TYPE_VECTOR)
{
    LLVMValueRef old_v = LLVMBuildLoad2(..., vec_t, field_ptr, "ec.oldv");
    LLVMValueRef new_v = emit_vec_clone_val(ctx, old_v, pt->as.vec.elem);
    LLVMBuildStore(..., new_v, field_ptr);
}
else if (pt->kind == TYPE_MAP)
{
    LLVMValueRef old_m = LLVMBuildLoad2(..., map_t, field_ptr, "ec.oldm");
    LLVMValueRef new_m = emit_map_clone_val(ctx, old_m, pt->as.map.key, pt->as.map.val);
    LLVMBuildStore(..., new_m, field_ptr);
}
```

### 2.3 根因 3：AOT `void @main()` 返回垃圾退出码

**位置**：`codegen.c` → `codegen_fn_decl()` + Pass 1 forward-declare

**机制**：LS 的 `fn main()` 返回 void，编译为 LLVM `define void @main()`。AOT 链接时 C 运行时（CRT）调用 `main` 并从 EAX/RAX 寄存器读取返回值。`ret void` 不设置 EAX，因此 CRT 读到的是上一条指令残留的垃圾值。

**为什么之前没暴露**：大多数 LS 程序的 main 函数以 `print(...)` 结尾，`printf`/`puts` 返回的值恰好在 EAX 中为 0（或小正数），不影响测试断言。但一旦 scope cleanup 执行了 `Data.__drop`（涉及 switch + 条件分支 + free），EAX 中残留的值变成非零 → exit code 1。

**关键观察**：
- `test_aot_simple.ls`（match 在 main 中，没有函数调用）→ exit 0
- `test_aot_param.ls`（match 在被调函数中）→ exit 1
- `test_aot_rc.ls`（调用 `fn foo() -> int { return 1 }`）→ exit 4
- memcheck AOT 版本 → exit 0（链接了 `ls_memcheck.lib`，free 行为不同导致 EAX 恰好为 0）

**修复**：

1. **Pass 1 forward-declare**：检测 `fn main()` + void return + 0 params → 覆盖 LLVM 函数类型为 `i32 @main()`

```c
if (strcmp(decl->as.fn_decl.name, "main") == 0 &&
    fn_type_ml->as.function.return_type->kind == TYPE_VOID &&
    decl->as.fn_decl.param_count == 0)
{
    LLVMTypeRef i32_t = LLVMInt32TypeInContext(ctx->context);
    fn_type = LLVMFunctionType(i32_t, NULL, 0, 0);
}
```

2. **`codegen_fn_decl`**：同样检测 + 覆盖；新增 `CodegenContext.is_main_void` flag

3. **隐式 return**：`ret void` → `ret i32 0`
4. **显式 `return`（AST_RETURN void 路径）**：同上

---

## 3. 改动文件清单

| 文件 | 改动 |
|------|------|
| `src/codegen.c` | `emit_enum_ctor`：vec/map/struct(has_drop) source move 标记 |
| `src/codegen.c` | `emit_enum_clone_val`：`needs` 检查加 TYPE_VECTOR/TYPE_MAP + clone 分支 |
| `src/codegen.c` | Pass 1 forward-declare：main void → i32 覆盖 |
| `src/codegen.c` | `codegen_fn_decl`：`is_main_void` flag + fn_type 覆盖 + `ret i32 0` |
| `src/codegen.c` | AST_RETURN void 路径：`is_main_void` 时 `ret i32 0` |
| `src/codegen.h` | `CodegenContext` 新增 `bool is_main_void` 字段 |
| `CMakeLists.txt` | 注册 `test_enum_vec_map_payload` 测试 |
| `tests/test_enum_vec_map_payload.cmake` | JIT + JIT-memcheck + AOT 三重验证脚本 |
| `tests/samples/enum_vec_payload_test.ls` | 完整测试（4 变体 + 工厂函数 + 参数传递） |
| `tests/samples/enum_vec_payload_param.ls` | 参数传递专项测试 |
| `tests/samples/enum_vec_payload_return.ls` | 函数返回专项测试 |

---

## 4. 测试矩阵

### 4.1 `enum_vec_payload_test.ls`

| 变体 | 构造 | match 解构 | 工厂函数 | 参数传递 |
|------|------|-----------|---------|---------|
| `Numbers(vec(int))` | ✅ | ✅ | ✅ `make_numbers()` | ✅ `process(d)` |
| `Lookup(map(string,int))` | ✅ | ✅ | ✅ `make_lookup()` | ✅ (同 process) |
| `Mixed(string, vec(int))` | ✅ | ✅ | ✅ `make_mixed()` | ✅ |
| `Empty`（无 payload） | ✅ | ✅ | — | ✅ |

### 4.2 验证路径

| 路径 | 结果 |
|------|------|
| JIT | ✅ 输出正确，exit 0 |
| JIT + `--memcheck` | ✅ 0 leak / 0 double-free / 0 invalid-free |
| AOT compile + run | ✅ 输出正确，exit 0 |

### 4.3 回归

全部 52 个 ctest 通过，无回归。

---

## 5. 调试过程记录

排查过程采用**逐步缩小范围**的方法论：

1. **最小复现**：`enum_vec_payload_mini.ls`（同函数内构造 + match）→ JIT crash
2. **定位 ctor move 缺失**：比较 string payload（有 move）和 vec payload（无 move）的 codegen
3. **修复根因 1 后**：JIT 本地 + return 路径 ✅，JIT 参数传递 ✅（memcheck clean）
4. **AOT 缩小**：
   - 无函数调用 → exit 0
   - 有函数调用但无 match → exit 0
   - 有函数调用 + match → exit 1
   - 本地构造 + match（非 main 函数） → exit 0
5. **AOT memcheck** → exit 0 + 0 leak → 排除内存错误，锁定为退出码问题
6. **验证垃圾退出码理论**：`fn foo() -> int { return 1 }` → exit 4（非 1，非 0）
7. **定位 void main 根因**：Pass 1 forward-declare 创建 `void @main()`，`ret void` 不设 EAX

---

## 6. 设计决策

### 6.1 为什么在 ctor 中 move 而非在 scope cleanup 中跳过？

**选择 move（cap=0）**：与 string 的行为一致——`string s = "hello".upper(); vec.push(s)` 之后 s.cap=-1，scope cleanup 跳过。enum ctor 是所有权转移的语义。

**替代方案（标记 is_borrowed）**：不合理——ctor 后源变量确实不再拥有数据。

### 6.2 为什么改 main 返回 i32 而非包装一层？

**选择覆盖类型**：最小改动，不引入额外函数调用。forward-declare 和 fn_decl 两处检测保证类型一致。

**替代方案（wrapper main）**：需要重命名用户 main 为 `__ls_main`，改动面更大，且 JIT 路径也需要调整。

### 6.3 `is_main_void` flag 的作用域

flag 存在 `CodegenContext` 中，由 `codegen_fn_decl` 在编译 main 函数体期间置 true，编译完毕后恢复。确保只有 main 函数内的 `return` 语句和隐式 return 受影响。嵌套函数（如 closure lambda lifting 的 `__closure_N`）不受影响。

---

## 7. 已知限制

- **`emit_enum_ctor` move 标记仅对 `AST_IDENT` 源有效**：如果构造器参数是函数调用返回值（`Numbers(make_vec())`），无需 move（rvalue 不需要标记），但也无法标记。当前实现正确处理了这种情况（`ast_unwrap_move` 返回非 IDENT 时跳过）
- **AOT main 覆盖仅影响 `fn main()` + void + 0 params**：如果用户写 `fn main() -> int`，不触发覆盖（用户自行管理返回值）
