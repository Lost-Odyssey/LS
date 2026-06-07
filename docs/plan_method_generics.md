# 计划：方法级类型参数（Method-Level Type Parameters）

> 状态：规划 2026-06-07。
> 目标：让 `impl(T) RawVec(T)` 中的方法能声明自己的类型参数 `<U>` / `<A>`，
> 从而在纯 LS 层面实现 `map<U>` / `reduce<A>` 等带独立类型参数的方法。
>
> 关联：[plan_rawvec.md](plan_rawvec.md) §9.5 KI-D、[plan_std_containers.md](plan_std_containers.md)、
> [features_history.md](features_history.md)。

---

## 0. 问题

当前 LS 支持三层泛型：

| 层级 | 语法 | 状态 |
|------|------|------|
| Struct 级 | `struct RawVec(T)` | ✅ |
| Impl 级 | `impl(T) RawVec(T)` | ✅ |
| 自由函数级 | `fn identity(T)(x: T) -> T` | ✅ |

但**不支持方法声明自己的类型参数**：

```ls
impl(T) RawVec(T) {
    // 当前合法：全部类型参数来自 struct/impl
    fn filter(&self, Block(T) -> bool) -> RawVec(T)    // ✅ T 已知

    // 当前不合法：U 无处声明
    fn map<U>(&self, Block(T) -> U) -> RawVec(U)       // ❌ U 未定义
    fn reduce<A>(&self, A init, Block(A, T) -> A) -> A // ❌ A 未定义
}
```

影响：`RawVec` 无法提供 `map` / `reduce`，**无法对齐内建 `vec`**。

---

## 1. 现状盘点

### 1.1 已有的基础设施

| 能力 | 位置 | 说明 |
|------|------|------|
| 泛型函数解析 | `parser.c:parse_fn_decl` ~2397 | `fn map(U)(...)` 已被正确解析为带 `type_params=["U"]` 的 AST |
| 自由泛型函数实例化 | `checker.c:4761-4956` | `identity(int)(42)` 完整路径：解析类型参数 → 构建 mangled 名 → 克隆 AST → 别名替换 → 类型检查 body → 入队 pending GM |
| 泛型 impl 方法惰性实例化 | `checker.c:instantiate_impl_method_types` 1224-1296 + `ensure_generic_method_instantiated` 1190-1218 | 泛型 struct 的方法签名实例化 + body 按需检查 |
| Pending GM 发射 | `codegen.c:21658-21711` Pass 2a | 发射所有已实例化的泛型函数/方法 body |
| Mangling 方案 | checker + codegen 一致 | 自由函数 `f(int)`、impl 方法 `S(int).m` |

### 1.2 限制

| 限制 | 位置 | 说明 |
|------|------|------|
| 类型参数检测仅限 `AST_IDENT` | `parser.c:1068` | `v.method(string)(args)` 中 `(string)` 不被识别为类型参数 |
| `instantiate_impl_method_types` 不处理方法自有类型参数 | `checker.c:1264-1275` | 方法有 `type_param_count > 0` 时，`resolve_type_node_with_substitution` 无法解析这些参数 |
| 方法调用时无 fallback 到「方法级泛型模板」 | `checker.c:5313-5319` | `find_method` 找不到就报错，不检查是否存在待实例化的泛型方法模板 |

---

## 2. 设计

### 2.1 语法

**声明**（parser 已支持）：

```ls
impl(T) RawVec(T) {
    fn map<U>(&self, Block(T) -> U f) -> RawVec(U)
    fn reduce<A>(&self, A init, Block(A, T) -> A f) -> A
}
```

**调用**（需要 parser 改动）：

```ls
// 自由函数风格：method(TYPE)(args...)
v.map(string)(|x| f"{x}")          // U = string
v.reduce(int)(0, |a,x| a + x)     // A = int
```

> 尾闭包 `{ |x| ... }` 与两段括号不兼容，需要用 `(|x| ...)` 内联闭包。

### 2.2 mangling 方案

扩展现有 mangling 模式，在 impl 方法名后附加方法级类型参数：

```
RawVec(int).map(string)
RawVec(int).reduce(int)
```

格式：`<ImplKey(types)>.<method_name>(method_types)`

与自由泛型函数的差异：

| 场景 | mangled 名称 |
|------|-------------|
| 自由泛型函数 | `identity(int)` |
| Impl 方法（无方法级泛型） | `RawVec(int).push` |
| Impl 方法（有方法级泛型） | `RawVec(int).map(string)` |

### 2.3 类型别名栈

实例化时，类型别名按**先 impl 级、后方法级**的顺序注册：

```
register_type_alias(c, "T", type_int())      // impl 级:  RawVec(int) 的 T
register_type_alias(c, "U", type_string())    // 方法级: map(string) 的 U
```

然后 `resolve_type_node` 解析方法体中的类型引用：

| 类型节点 | 别名匹配 | 解析结果 |
|---------|---------|---------|
| `T` | T→int | `type_int()` |
| `Block(T) -> U` | T→int, U→string | `Block(int) -> string` |
| `RawVec(U)` | U→string | `type_struct("RawVec(string)")` |

恢复顺序：回退 `saved_alias_count` 到注册前的状态。

---

## 3. 实现步骤

### Step 1：Parser — 调用点类型参数检测（~1 行）

**文件**：`src/parser.c`

**位置**：`parse_call_arguments` 函数，类型参数 peek 检测条件（~L1068）

**改动**：

```c
// 当前
if (left->kind == AST_IDENT) {

// 改为
if (left->kind == AST_IDENT || left->kind == AST_FIELD) {
```

**效果**：`v.map(string)(|x| f"{x}")` 被识别为：
- `AST_CALL` with `type_args = [string]`, `args = [closure]`

**不涉及**：尾闭包 `{|x| ...}` 不受影响（它在消费完 `)` 后才解析）。

### Step 2：Checker 数据结构（~10 行）

**文件**：`src/checker.h`

**新增**：

```c
typedef struct {
    char     *method_name;        // "map"
    char     *impl_key;           // "RawVec(int)"（strdup）
    AstNode  *method_ast;         // 指向 impl_decl.methods[i]（不拥有）
    char    **impl_tp_names;      // ["T"]（指向 struct template，不拥有）
    Type    **impl_tp_types;      // [int]（Type* 指针，不拥有）
    int       impl_tp_count;
} GenericImplMethodTemplate;
```

在 `Checker` 结构体中新增：

```c
GenericImplMethodTemplate *generic_impl_method_templates;
int generic_impl_mt_count;
int generic_impl_mt_cap;
```

### Step 3：Checker — 存储泛型方法模板（~15 行）

**文件**：`src/checker.c`

**位置**：`instantiate_impl_method_types`（~L1237），遍历方法的循环内

**改动**：当检测到 `method->as.fn_decl.type_param_count > 0` 时：

```c
if (method->as.fn_decl.type_param_count > 0) {
    if (c->generic_impl_mt_count >= c->generic_impl_mt_cap) {
        c->generic_impl_mt_cap = c->generic_impl_mt_cap < 8 ?
            8 : c->generic_impl_mt_cap * 2;
        c->generic_impl_method_templates = realloc_safe(...);
    }
    int idx = c->generic_impl_mt_count++;
    c->generic_impl_method_templates[idx].method_name = method->as.fn_decl.name;
    c->generic_impl_method_templates[idx].impl_key = strdup(mangled_name);
    c->generic_impl_method_templates[idx].method_ast = method;
    c->generic_impl_method_templates[idx].impl_tp_names = tp_names;
    c->generic_impl_method_templates[idx].impl_tp_types = type_args;
    c->generic_impl_method_templates[idx].impl_tp_count = tp_count;
    continue;   // 不向 impl_registry 注册
}
```

### Step 4：Checker — 按需实例化泛型方法（~120 行）

**文件**：`src/checker.c`

**位置 1**：方法调用路径（~L5313），`find_method` 返回 NULL 时

```c
if (callee_type == NULL) {
    callee_type = try_instantiate_generic_impl_method(c, node,
        method_struct, method_name);
    if (callee_type != NULL) {
        node->as.call.callee->resolved_type = callee_type;
        goto check_args;   // 或结构化跳转
    }
    checker_error(c, node->line, node->column,
                  "type '%s' has no method '%s'", method_struct, method_name);
    result = NULL;
    break;
}
```

**位置 2**：新建函数 `try_instantiate_generic_impl_method`

实现流程（对标 `checker.c:4761-4956` 自由泛型函数实例化）：

```
1. 按 impl_key + method_name 查找 GenericImplMethodTemplate
2. 验证 call_node->type_arg_count == method_ast->type_param_count
3. resolve_type_node 解析每个类型参数
4. 构建 mangled 名: "RawVec(int).map(string)"
5. 检查缓存（已实例化？按 mangled 名查 scope）
6. 首次实例化：
   a. 保存 alias_count
   b. 注册 impl 级别名：T → int
   c. 注册方法级别名：U → string
   d. 解析参数/返回类型
   e. 注册到 scope（mangled 名 → 函数类型）
   f. 深克隆方法 AST
   g. type_param_count = 0，type_param_bounds = NULL
   h. push_scope，注册参数，type-check body
   i. pop_scope，恢复 alias_count
   j. 入队 pending_generic_methods[]（struct_type 指向 impl struct）
7. 检查调用参数
8. 返回 resolved_type->as.function.return_type
```

**关键区别 vs 自由泛型函数路径**：
- 实现级别名（T）**和**方法级别名（U）都要注册
- `struct_type` 不为 NULL（指向 `RawVec(int)`），以便 codegen 正确生成方法符号
- 错误信息提示用户补类型参数而非"no method"

### Step 5：Checker — `pending_generic_methods` struct_type 确认

当前 `pending_generic_methods` 的 `struct_type` 字段在自由泛型函数入队时为 NULL，在 impl 方法入队时指向实例化后的 struct Type。

**确认**：map/reduce 的 pending 条目应将 `struct_type` 设为 `RawVec(int)` 的 Type，与现有 impl 方法一致。

### Step 6：Codegen — 方法名查找扩展（~30 行）

**文件**：`src/codegen.c`

**位置**：方法调用 codegen（~L11474-11504）

**改动**：构建 `qualified_name` 后，如果 `AST_CALL` 有 `type_arg_count > 0`：

```c
if (node->as.call.type_arg_count > 0) {
    // 构建完整 mangled 名: "RawVec(int).map(string)"
    char full_name[640];
    snprintf(full_name, sizeof(full_name), "%s(", qualified_name);
    for (int ti = 0; ti < node->as.call.type_arg_count; ti++) {
        TypeNode *tn = node->as.call.type_args[ti];
        // 追加类型名...
    }
    strcat(full_name, ")");
    callee = LLVMGetNamedFunction(ctx->module, full_name);
    if (callee == NULL) {
        // 在 pending GMs 中查找
        for (int gi = 0; gi < ctx->pending_gm_count; gi++) {
            if (strcmp(ctx->pending_generic_methods[gi].mangled_name, full_name) == 0) {
                LLVMTypeRef gft = type_to_llvm(ctx, ...);
                callee = LLVMAddFunction(ctx->module, full_name, gft);
                break;
            }
        }
    }
}
// 回退到原有路径（无类型参数的方法）
if (callee == NULL) {
    callee = LLVMGetNamedFunction(ctx->module, qualified_name);
}
```

### Step 7：RawVec 库添加 map/reduce（~30 行）

**文件**：`std/rawvec.ls`

在 `impl(T) RawVec(T)` 末尾添加：

```ls
// Transform every element T→U, producing a new RawVec(U).
// The closure receives a DEEP-CLONED element (matching the
// get() / v[i] read semantics).
fn map<U>(&self, Block(T) -> U f) -> RawVec(U) {
    RawVec(U) out = {}
    out.reserve(self.len)
    for (int i = 0; i < self.len; i = i + 1) {
        out.push(f(self.data[i]))
    }
    return out
}

// Left fold: reduce to a single value of type A.
fn reduce<A>(&self, A init, Block(A, T) -> A f) -> A {
    A acc = init
    for (int i = 0; i < self.len; i = i + 1) {
        acc = f(acc, self.data[i])
    }
    return acc
}
```

### Step 8：测试

**文件**：`tests/test_rawvec_parity_p2.cmake` + `tests/samples/rawvec_parity_p2_test.ls`

测试矩阵：

| 测试 | 元素 T | U / A | 验证 |
|------|--------|-------|------|
| map int→int | `RawVec(int)` | U=int | 值正确 |
| map int→string | `RawVec(int)` | U=string | 类型变换 |
| map int→Pt | `RawVec(int)` | U=Pt(struct) | struct 变换 |
| reduce 求和 | `RawVec(int)` | A=int | 值正确 |
| reduce 拼接 | `RawVec(string)` | A=string | has_drop 安全 |
| memcheck | 全部 | — | 0/0/0 |

加入 `CMakeLists.txt` 依赖链：`test_rawvec_parity_p2` DEPENDS `test_rawvec_api`，`test_rawvec_functional_p3` DEPENDS `test_rawvec_parity_p2`。

---

## 4. 变更汇总

| 层 | 文件 | 新增行 | 说明 |
|---|------|--------|------|
| Parser | `src/parser.c` | +1 | 类型参数检测放宽到 `AST_FIELD` |
| Checker 数据结构 | `src/checker.h` | +10 | `GenericImplMethodTemplate` + Checker 字段 |
| Checker 存储 | `src/checker.c` | +15 | `instantiate_impl_method_types` 中跳过+存储 |
| Checker 实例化 | `src/checker.c` | +~120 | `try_instantiate_generic_impl_method` 新函数 |
| Codegen | `src/codegen.c` | +30 | 方法名查找支持带类型参数的 mangled 名 |
| 库 | `std/rawvec.ls` | +30 | `map<U>` + `reduce<A>` |
| 构建 | `CMakeLists.txt` | +15 | 测试目标 + 依赖 |
| 测试 | `tests/` | +80 | P2 测试 + cmake 脚本 |
| **总计** | | **~300** | |

---

## 5. 验收标准

1. **`test_rawvec_parity_p2`** JIT + AOT + memcheck 全绿
2. `RawVec(int)` 上的 `map`/`reduce` 功能正确
3. `RawVec(string)` 上的 `map`/`reduce` memcheck 0/0/0
4. `RawVec(Pt)` without map 正常编译（不报虚假 Eq 错误）
5. 缺少类型参数时编译错误信息清晰
6. 全量 ctest 保持绿

---

## 6. 后续扩展（不在本计划）

| 特性 | 说明 |
|------|------|
| `where T: Eq` 在方法级参数上 | 已支持（现有 `fn_decl.where_bounds`） |
| 尾闭包 `{|x|}` + 类型参数 | 语法设计待定 |
| 类型参数推断（省略调用处类型参数） | 需统一类型推导引擎 |
| trait 方法中的泛型方法 | trait 本身不支持泛型参数 |
