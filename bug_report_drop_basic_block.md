# Bug Report: User-defined __drop 函数生成悬空基本块

## 问题描述

当用户定义的 `__drop` 函数包含表达式语句（如 `print("message")`）时，生成的 LLVM IR 包含两个基本块：`entry` 和 `entry1`。`entry1` 基本块没有 predecessors（No predecessors!），永远不会被执行。

### 示例代码

```c
struct Foo {
    int x
}

impl Foo {
    fn __drop() {
        print("Dropped Foo")
    }
}
```

### 生成的 LLVM IR

```llvm
define void @Foo.__drop(ptr %self) {
entry:
  ret void

entry1:                                           ; No predecessors!
  %self2 = alloca ptr, align 8
  store ptr %self, ptr %self2, align 8
  %0 = call i32 (ptr, ...) @printf(ptr @fmt, ptr @str)
  ret void
}
```

### 影响

- **内存泄漏**：`print` 函数内部的字符串常量没有被正确释放
- **未定义行为**：悬空基本块可能导致程序行为不确定
- **Double-free**：如果 `__drop` 函数被多次调用，可能导致内存重复释放

## 根本原因分析

问题的根源在于 `codegen_fn_decl` 函数处理函数体的逻辑：

1. 当函数体是一个表达式语句时（第 3970-3972 行）：
   ```c
   } else {
       codegen_stmt(ctx, body);
   }
   ```

2. `codegen_stmt` 处理 `AST_EXPR_STMT`，调用 `codegen_expr`，但不使用返回值

3. `codegen_expr` 对于 `AST_CALL`（`print`）返回一个 `LLVMValueRef`（`printf` 的返回值）

4. **关键问题**：当函数体只有一个表达式语句时，`codegen_fn_decl` 的逻辑没有正确处理：
   - 它会调用 `codegen_stmt(ctx, body)`
   - `codegen_stmt` 调用 `codegen_expr`
   - `codegen_expr` 生成 IR，但返回值被忽略
   - 如果 `codegen_expr` 生成了一些中间值或使用了 `LLVMBuildGlobalStringPtr`，这些指令会被插入到新的基本块中

5. 然后 `codegen_fn_decl` 在第 3976 行检查 `entry` 基本块是否有 terminator：
   ```c
   if (LLVMGetBasicBlockTerminator(current_bb) == NULL)
   ```
   如果没有，它会调用 `emit_cleanup_to(ctx, NULL, NULL)`，然后调用 `emit_drop_field_cleanup(ctx)`（第 3980 行）

6. `emit_drop_field_cleanup` 会为每个 string 字段调用 `emit_string_free`，这会创建新的基本块，并将 builder 插入位置设置到 `cont_bb` 中

7. 然后 `codegen_fn_decl` 在第 3983 行调用 `LLVMBuildRetVoid(ctx->builder)`，但此时 builder 的插入位置已经不在 `entry` 基本块中了

8. 如果 `entry` 基本块没有 terminator，`LLVMBuildRetVoid` 可能会在一个新的基本块中插入 `ret` 指令，创建 `entry1` 基本块

## 修复方案

需要在 `codegen_fn_decl` 中修复 `__drop` 函数的处理逻辑：

1. **保存并恢复 builder 插入位置**：在调用 `emit_drop_field_cleanup` 之前保存插入位置，调用后恢复
2. **正确处理表达式语句**：对于表达式语句，应该检查 `codegen_expr` 的返回值是否需要存储到 alloca 中

## 已尝试的修复

1. **第一次修复**：在 `emit_struct_drop` 中递归释放嵌套 struct 字段
   - **结果**：部分修复，但未解决悬空基本块问题

2. **第二次修复**：在 `emit_struct_drop` 中恢复 builder 插入位置
   - **结果**：未解决问题

3. **第三次修复**：在 `codegen_fn_decl` 中保存并恢复 builder 插入位置
   - **结果**：未解决问题（因为问题更早发生）

## 进一步调查方向

需要深入调查 `codegen_expr` 处理 `AST_CALL` 时的行为，以及 `LLVMBuildGlobalStringPtr` 是否会在 `entry` 基本块没有 terminator 时创建新的基本块。

## 验证方法

- 编译不使用任何内置函数的 `__drop` 函数，检查是否仍然存在悬空基本块
- 检查 `codegen_expr` 是否在处理 `AST_CALL` 时创建新的基本块
- 查看 `LLVMBuilder` 的实现，了解 `LLVMBuildGlobalStringPtr` 和 `LLVMBuildRetVoid` 的行为
