# Phase G1.5 (Generic Impl Blocks) — 失败分析与教训

> 日期：2026-05-16  
> 状态：**已放弃** — 回退到 G1 (泛型 struct 单态化) 稳定版本 (ctest 30/30)

## 1. 目标

在 G1 泛型 struct 的基础上，实现泛型 impl 块：

```ls
struct Stack(T) {
    vec(T) data
}

impl(T) Stack(T) {
    fn push(T item) { self.data.push(item) }
    fn len() -> int { return self.data.length }
}
```

使泛型 struct 可以拥有方法，方法在每个具体实例化（如 `Stack(int)`）时自动单态化。

## 2. 实现路径（已执行）

| 步骤 | 内容 | 完成 |
|------|------|------|
| Parser | 识别 `impl(T) Stack(T) { ... }` 语法 | ✅ |
| AST | impl_decl 加 type_param_count/type_params；struct_decl 加 instantiation_methods | ✅ |
| Checker | `instantiate_impl_methods`: 为每个具体实例克隆方法 AST + type-check | ✅ |
| Codegen | Pass 2a 预发射 + 延迟 emit (`cg_try_emit_generic_method`) | ❌ crash |

## 3. 根本原因：`ast_free` 的 double-free

### 3.1 问题描述

当泛型 impl 含有方法（即使方法体为空）时，编译成功但程序退出时 crash：
`STATUS_HEAP_CORRUPTION (0xC0000374)`。Exit code = -1073740940 (116)。

### 3.2 根因链

1. **Checker 创建合成 fn_decl 节点**（`instantiate_impl_methods`），其中多个字段**共享**原始模板方法的指针：
   ```c
   fn->as.fn_decl.name        = method->as.fn_decl.name;       // shared!
   fn->as.fn_decl.param_names = method->as.fn_decl.param_names; // shared!
   fn->as.fn_decl.param_types = method->as.fn_decl.param_types; // shared!
   fn->as.fn_decl.return_type = method->as.fn_decl.return_type; // shared!
   ```

2. **`ast_free(AST_FN_DECL)` 释放所有字段**：
   ```c
   free(node->as.fn_decl.name);              // ← 释放了 template 的 name
   free(node->as.fn_decl.param_names[i]);    // ← 释放了 template 的参数名
   type_node_free(node->as.fn_decl.param_types[i]); // ← 释放了 template 的 TypeNode
   type_node_free(node->as.fn_decl.return_type);    // ← 释放了 template 的 TypeNode
   ```

3. **后续释放原始 impl 模板时，这些指针已被释放** → double-free → 堆元数据损坏

4. **堆损坏是延迟检测的**：Windows 堆管理器在下一次 malloc/free 操作时才发现损坏，表现为"编译成功但运行 crash"的假象。实际上 IR 生成是正确的，问题完全在内存管理层。

### 3.3 次要问题

同样在 `ast_clone_node` 中：
```c
case AST_IDENT:   // break; — 不克隆 ident.name 字符串
case AST_STRING_LIT:  // break; — 不克隆 string_lit.value
```

克隆的 body 节点中 ident 和 string_lit 的 `char*` 与原始模板共享。`ast_free(body_clone)` 释放克隆 body 时也会 double-free 这些字符串。

## 4. 为什么这个问题难以发现

| 因素 | 影响 |
|------|------|
| Windows 堆延迟检测 | crash 位置远离实际损坏点，误导调查方向 |
| 编译成功 + IR 正确 | 让人以为是 codegen/runtime 问题 |
| 无 AddressSanitizer | Windows Release build 无 ASAN，难以定位 |
| Empty body 也 crash | 排除了方法体编译作为原因，但没指向 ast_free |
| 合成节点的"共享"注释 | 代码注释 `/* shared */` 暗示开发者认为这是安全的 |

## 5. 规划层面的问题

### 5.1 AST 所有权模型不清晰

LS 的 AST 隐含假设：**每个 AstNode 拥有其子指针的完整所有权**，`ast_free` 递归释放一切。但 G1.5 引入了"合成节点部分字段共享原始数据"的模式，违背了这一假设。

**教训**：在引入 AST 节点共享之前，必须先解决所有权问题。两种方案：
- (A) 合成节点必须深拷贝所有字段（包括 TypeNode 树和字符串）
- (B) 给 AST 节点加 `is_synthetic` 标志，ast_free 跳过共享字段的释放

### 5.2 `ast_clone_node` 的浅拷贝策略

`ast_clone_node` 对 `char*` 和 `TypeNode*` 做浅拷贝（共享），但 `ast_free` 会释放它们。这意味着：
- 克隆的 body 不能被 `ast_free` 释放（否则 double-free）
- 或者 `ast_clone_node` 必须做完全深拷贝（包括所有字符串和 TypeNode）

### 5.3 缺少 `type_node_clone` 基础设施

TypeNode 是复杂递归结构（含嵌套的 named/array/map 类型）。系统中没有 `type_node_clone` 函数。要正确深拷贝合成方法��点，需要先实现这个基础设施。

### 5.4 方法论问题

- **过早集成**：直接在 checker 中做合成+type-check+存储，没有先验证 ast_free 的兼容性
- **缺少增量测试**：应该先测试"创建合成节点→ast_free"的路径是否安全
- **Windows 调试困难**：没有配置 Debug build + ASAN，导致 heap corruption 定位困难

## 6. 重新实现时的建议

### 6.1 前置工作

1. **实现 `type_node_clone(TypeNode*) -> TypeNode*`**：完整深拷贝 TypeNode 树
2. **让 `ast_clone_node` 对所有 `char*` 字段做 `strdup`**：不再共享字符串
3. **或者**：给 AstNode 加一个 `uint8_t flags` 字段，包含 `AST_FLAG_SYNTHETIC` 位，让 `ast_free` 在遇到 synthetic 节点时只释放 body（owned）而跳过其他字段

### 6.2 实现策略

推荐方案 A（完全深拷贝）：
```c
// 合成节点必须拥有所有数据的独立副本
fn->as.fn_decl.name        = strdup(method->as.fn_decl.name);
fn->as.fn_decl.param_names = deep_copy_string_array(method->as.fn_decl.param_names, n);
fn->as.fn_decl.param_types = deep_copy_type_node_array(method->as.fn_decl.param_types, n);
fn->as.fn_decl.return_type = type_node_clone(method->as.fn_decl.return_type);
fn->as.fn_decl.body        = ast_clone_node(method->as.fn_decl.body);  // 也需要完全深拷贝
```

### 6.3 测试策略

在实现 codegen 之前，先验证：
```c
// 测试: 创建合成节点 → 立即 ast_free → 验证无 crash
// 测试: 克隆 body → ast_free(clone) → ast_free(original) → 验证无 double-free
```

### 6.4 开发环境

- 配置 CMake Debug build + AddressSanitizer (`-fsanitize=address`)
- 或使用 Windows 的 Application Verifier 的 heap 检查
- 每个小步骤都跑一次完整 ctest

## 7. 涉及的代码文件

| 文件 | G1.5 新增/修改 |
|------|----------------|
| `src/ast.h` | struct_decl 加 instantiation_methods/count；impl_decl 加 type_params |
| `src/ast.c` | ast_clone_node 函数；ast_free 加 instantiation_methods 释放 |
| `src/parser.c` | impl(T) 语法解析 |
| `src/checker.c` | instantiate_impl_methods 函数；struct_templates 加 impl_node |
| `src/checker.h` | struct_templates 结构体 |
| `src/codegen.c` | program_ast 字段；cg_try_emit_generic_method；Pass 2a 预发射 |
| `src/codegen.h` | program_ast / global_scope 字段 |

## 8. 结论

G1.5 的核心难点不在语义设计（泛型 impl 的类型推导逻辑是正确的），而在 **AST 所有权模型与内存管理**。LS 的 AST 假设"节点独占所有子数据"，但泛型单态化需要"合成节点共享模板数据"——这两个需求冲突。解决这个冲突需要先扩展 AST 基础设施（type_node_clone、完整深拷贝的 ast_clone_node），这是一个独立的前置任务。
