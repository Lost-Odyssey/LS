# LS 无约束 struct/fn 泛型实现文档 v2

**日期**：2026-05-18  
**前置依赖**：无（独立于 interface/trait）  
**参考**：`docs/generics_plan.md`（v1 原始规范）、`docs/generics_impl_postmortem.md`（G1.5 失败分析）

---

## 0. G1.5 失败教训与本次核心策略

### 0.1 上次为什么失败

G1.5（泛型 impl 块）crash 的根因是 **AST 所有权冲突**：
- `instantiate_impl_methods` 创建的合成 `fn_decl` 节点共享了模板方法的 `name`、`param_types`、`return_type` 指针
- `ast_free` 递归释放时 double-free → 堆损坏

### 0.2 本次核心策略：**不克隆 AST，不合成 fn_decl 节点**

本次实现采用完全不同的路线：

| 原路线（G1.5，已失败） | 新路线（v2） |
|------------------------|-------------|
| Checker 阶段克隆方法 AST 节点 | **Checker 只注册类型签名，不碰 AST** |
| 合成 fn_decl 共享模板指针 | **Codegen 阶段从模板 AST + 替换表直接 emit** |
| `ast_free` 触发 double-free | **模板 AST 由 AST_PROGRAM 统一释放，零共享** |

关键原则：
1. **TYPE_PARAM 不进 codegen** — checker 阶段所有泛型使用点完成实例化
2. **AST 零修改** — 模板方法的 AST 原样保留，codegen 用替换表在 emit 时"即时替换"
3. **每步可测、可回退** — 10 个增量步骤，每步有独立 ctest 验证

### 0.3 前置基础设施：`type_node_clone`

上次失败的教训之一是缺少 `type_node_clone`。本次在 Step 0 先实现它，为后续所有步骤提供安全的深拷贝能力（即使本次主路线不依赖 AST 克隆，但作为防御性基础设施值得先做）。

---

## 1. 实现步骤总览

```
Phase G1: 泛型 struct（无方法）
  Step 0: type_node_clone 基础设施                    ← 独立可测
  Step 1: AST 扩展 struct_decl + ast_free             ← 独立可测
  Step 2: Parser 解析 struct Name(T, U) { ... }       ← 独立可测（parse-only）
  Step 3: Checker 注册 struct template                 ← 独立可测（template 注册）
  Step 4: resolve_type_node_with_substitution          ← 独立可测（单元测试）
  Step 5: checker_instantiate_struct + has_drop         ← 泛型 struct 端到端
  Step 6: Codegen 适配（new_expr 从 resolved_type 查） ← ctest 全过

Phase G1.5: 泛型 impl 块
  Step 7: AST + Parser 扩展 impl(T) Stack(T)           ← parse-only 测试
  Step 8: Checker 注册 + 方法签名实例化                 ← 类型检查通过
  Step 9: Codegen 从模板 AST + 替换表 emit 方法         ← 端到端

Phase G2: 泛型函数
  Step 10: fn identity(T)(T x) -> T                    ← 端到端
```

**每步增量改动，改动文件不超过 3 个，ctest 不减少。**

---

## 2. Step 0: `type_node_clone` 基础设施

### 2.1 目的

为 TypeNode 树提供完整深拷贝能力。即使 v2 主路线避免克隆 AST，这个函数仍有独立价值：
- 防御性基础设施，未来任何需要 TypeNode 拷贝的场景都可用
- `resolve_type_node_with_substitution` 内部可能需要构造新 TypeNode（替换后的嵌套类型）

### 2.2 实现

**文件**：`src/ast.h`（声明）+ `src/ast.c`（实现）

```c
/* ast.h — 在 type_node_free 声明之后添加 */
TypeNode *type_node_clone(const TypeNode *src);
```

```c
/* ast.c — 在 type_node_free 之后添加 */
TypeNode *type_node_clone(const TypeNode *src) {
    if (src == NULL) return NULL;
    TypeNode *dst = (TypeNode *)malloc_safe(sizeof(TypeNode));
    *dst = *src;  /* 浅拷贝所有标量字段（kind, line, column, is_mut, prim_type） */

    switch (src->kind) {
    case TYPE_NODE_PRIMITIVE:
        break;  /* 无堆字段 */
    case TYPE_NODE_POINTER:
    case TYPE_NODE_REFERENCE:
        dst->as.pointee = type_node_clone(src->as.pointee);
        break;
    case TYPE_NODE_ARRAY:
        dst->as.array.elem = type_node_clone(src->as.array.elem);
        /* .size 是 int，已由浅拷贝覆盖 */
        break;
    case TYPE_NODE_VECTOR:
        dst->as.vec.elem = type_node_clone(src->as.vec.elem);
        break;
    case TYPE_NODE_MAP:
        dst->as.map.key = type_node_clone(src->as.map.key);
        dst->as.map.val = type_node_clone(src->as.map.val);
        break;
    case TYPE_NODE_FN:
    case TYPE_NODE_BLOCK: {
        int n = src->as.fn.param_count;
        if (n > 0) {
            dst->as.fn.params = (TypeNode **)malloc_safe((size_t)n * sizeof(TypeNode *));
            for (int i = 0; i < n; i++)
                dst->as.fn.params[i] = type_node_clone(src->as.fn.params[i]);
        } else {
            dst->as.fn.params = NULL;
        }
        dst->as.fn.ret = type_node_clone(src->as.fn.ret);
        break;
    }
    case TYPE_NODE_NAMED: {
        dst->as.named.name = str_dup(src->as.named.name);
        int n = src->as.named.arg_count;
        if (n > 0) {
            dst->as.named.args = (TypeNode **)malloc_safe((size_t)n * sizeof(TypeNode *));
            for (int i = 0; i < n; i++)
                dst->as.named.args[i] = type_node_clone(src->as.named.args[i]);
        } else {
            dst->as.named.args = NULL;
        }
        break;
    }
    }
    return dst;
}
```

### 2.3 测试

验证方式：在 `test_ast.c`（或内联）做：
```c
TypeNode *orig = make_some_complex_type_node();
TypeNode *clone = type_node_clone(orig);
type_node_free(clone);   // 不能 crash
type_node_free(orig);    // 不能 crash（证明无共享）
```

**验收**：ctest 全过，无 crash。

### 2.4 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ast.h` | +1 行声明 |
| `src/ast.c` | +50 行实现 |

---

## 3. Step 1: AST 扩展 `struct_decl`

### 3.1 改动

**文件**：`src/ast.h`

```c
struct {
    char *name;
    char **type_params;      /* G1: ["T", "U"] for struct Pair(T, U); NULL if non-generic */
    int   type_param_count;  /* G1: 0 for non-generic */
    TypeNode **field_types;
    char **field_names;
    int field_count;
} struct_decl;
```

**文件**：`src/ast.c`，`ast_free` 的 `AST_STRUCT_DECL` 分支：

```c
case AST_STRUCT_DECL:
    free(node->as.struct_decl.name);
    /* G1: free type_params */
    for (int i = 0; i < node->as.struct_decl.type_param_count; i++)
        free(node->as.struct_decl.type_params[i]);
    free(node->as.struct_decl.type_params);
    /* existing field cleanup ... */
    break;
```

**文件**：`src/parser.c`，所有创建 `AST_STRUCT_DECL` 的地方初始化新字段：

```c
n->as.struct_decl.type_params = NULL;
n->as.struct_decl.type_param_count = 0;
```

### 3.2 测试

现有 ctest 全过（所有非泛型 struct 的 `type_param_count = 0`，行为不变）。

### 3.3 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ast.h` | +2 行字段 |
| `src/ast.c` | +3 行释放 |
| `src/parser.c` | +2 行初始化（找到 `AST_STRUCT_DECL` 创建点） |

---

## 4. Step 2: Parser 解析泛型 struct 声明

### 4.1 语法

```ls
struct Pair(T, U) {
    T first
    U second
}
```

类型参数必须大写首字母（与类型名约定一致，区分变量名）。

### 4.2 实现

**文件**：`src/parser.c`，`parse_struct_decl` 函数

在 consume struct name 之后、consume `{` 之前插入：

```c
/* G1: optional type parameter list */
char **type_params = NULL;
int   type_param_count = 0;
int   type_param_cap = 0;

if (check(p, TOKEN_LPAREN)) {
    advance(p); /* consume '(' */
    if (check(p, TOKEN_IDENTIFIER) && is_upper_ident(p->current)) {
        do {
            if (!check(p, TOKEN_IDENTIFIER) || !is_upper_ident(p->current)) {
                error_at_current(p, "type parameter names must start with uppercase");
                break;
            }
            advance(p);
            char *tpname = str_dup_n(p->previous.start, p->previous.length);
            PUSH_ARRAY(type_params, type_param_count, type_param_cap, tpname);
        } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after type parameters");
    } else {
        error_at_current(p, "expected uppercase type parameter name after '('");
    }
}

/* 赋给 AST 节点 */
n->as.struct_decl.type_params = type_params;
n->as.struct_decl.type_param_count = type_param_count;
```

**辅助函数**（parser.c 顶部）：

```c
static bool is_upper_ident(Token t) {
    return t.type == TOKEN_IDENTIFIER
        && t.length > 0
        && t.start[0] >= 'A' && t.start[0] <= 'Z';
}
```

### 4.3 测试

```ls
// tests/samples/generics_step2_parse_test.ls
struct Pair(T, U) {
    int placeholder
}

fn main() {
    print("parse ok")
}
```

此时 checker 遇到 `struct Pair(T, U)` 还不知道怎么处理 `type_param_count > 0`，**暂时让它跳过**（不注册到 struct_types）。

**验收**：parser 不 crash，能打印 AST（或至少编译到 checker 阶段不 segfault）。现有 ctest 全过。

### 4.4 涉及文件

| 文件 | 改动 |
|------|------|
| `src/parser.c` | +30 行（`is_upper_ident` + 类型参数解析） |

---

## 5. Step 3: Checker 注册 struct template

### 5.1 数据结构

**文件**：`src/checker.h`

在 `Checker` 结构体中，`enum_templates` 之后添加：

```c
/* G1: User-defined generic struct templates.
   Registered when check_struct_decl sees type_param_count > 0.
   Instantiated lazily in resolve_type_node. */
struct {
    const char *base_name;      /* "Pair", "Stack" — points into AST (not owned) */
    char      **type_params;    /* ["T", "U"] — points into AST (not owned) */
    int         type_param_count;
    AstNode    *decl_node;      /* AST_STRUCT_DECL (not owned) */
    AstNode    *impl_node;      /* AST_IMPL_DECL (not owned), NULL initially */
} *struct_templates;
int struct_template_count;
int struct_template_cap;
```

### 5.2 注册逻辑

**文件**：`src/checker.c`

`check_struct_decl` 函数入口处：

```c
static void check_struct_decl(Checker *c, AstNode *node) {
    const char *name = node->as.struct_decl.name;
    int tpc = node->as.struct_decl.type_param_count;

    if (tpc > 0) {
        /* G1: Generic struct → register as template, skip field checking */
        register_struct_template(c, name,
                                 node->as.struct_decl.type_params, tpc,
                                 node);
        return;
    }

    /* Non-generic: existing logic unchanged ... */
}
```

```c
static void register_struct_template(Checker *c, const char *base_name,
                                     char **type_params, int type_param_count,
                                     AstNode *decl_node) {
    /* 重复检查 */
    for (int i = 0; i < c->struct_template_count; i++) {
        if (strcmp(c->struct_templates[i].base_name, base_name) == 0) {
            checker_error(c, decl_node->line, decl_node->column,
                          "generic struct '%s' already declared", base_name);
            return;
        }
    }
    /* 也检查是否与非泛型 struct 同名 */
    if (find_struct_type(c, base_name)) {
        checker_error(c, decl_node->line, decl_node->column,
                      "struct '%s' already declared (non-generic)", base_name);
        return;
    }

    if (c->struct_template_count >= c->struct_template_cap) {
        c->struct_template_cap = c->struct_template_cap < 4 ? 4
                                 : c->struct_template_cap * 2;
        c->struct_templates = realloc_safe(c->struct_templates,
            (size_t)c->struct_template_cap * sizeof(c->struct_templates[0]));
    }
    int idx = c->struct_template_count++;
    c->struct_templates[idx].base_name        = base_name;
    c->struct_templates[idx].type_params      = type_params;
    c->struct_templates[idx].type_param_count = type_param_count;
    c->struct_templates[idx].decl_node        = decl_node;
    c->struct_templates[idx].impl_node        = NULL;
}
```

### 5.3 Checker 初始化/销毁

`checker_init`：
```c
c->struct_templates = NULL;
c->struct_template_count = 0;
c->struct_template_cap = 0;
```

`checker_destroy`：
```c
free(c->struct_templates);  /* 元素中的指针都指向 AST，不 own */
```

### 5.4 测试

```ls
// tests/samples/generics_step3_register_test.ls
struct Pair(T, U) {
    int placeholder
}

fn main() {
    print("register ok")
}
```

**验收**：编译通过（Pair 被注册为 template，不作为普通 struct），ctest 全过。可加一个负向测试：`struct Pair(T, U) { ... } struct Pair(T, U) { ... }` → 报 "already declared"。

### 5.5 涉及文件

| 文件 | 改动 |
|------|------|
| `src/checker.h` | +10 行结构体字段 |
| `src/checker.c` | +40 行（`register_struct_template` + `check_struct_decl` 入口分支 + init/destroy） |

---

## 6. Step 4: `resolve_type_node_with_substitution`

### 6.1 目的

这是泛型系统的**核心替换引擎**：给定一个 TypeNode 树和一组 `{name→Type}` 替换对，递归解析并返回替换后的 `Type*`。

### 6.2 实现

**文件**：`src/checker.c`

```c
/* Resolve a TypeNode to a Type, replacing type parameter names with concrete types.
   tp_names[i] ↔ type_args[i].
   Falls back to resolve_type_node for non-parameterized paths. */
static Type *resolve_type_node_with_substitution(
    Checker *c,
    TypeNode *node,
    char **tp_names, Type **type_args, int tp_count)
{
    if (node == NULL) return type_void();

    switch (node->kind) {
    case TYPE_NODE_PRIMITIVE:
        return resolve_type_node(c, node);

    case TYPE_NODE_NAMED: {
        const char *name = node->as.named.name;

        /* 检查是否是类型参数名 */
        for (int i = 0; i < tp_count; i++) {
            if (strcmp(name, tp_names[i]) == 0) {
                if (node->as.named.arg_count > 0) {
                    checker_error(c, node->line, node->column,
                        "type parameter '%s' cannot have type arguments", name);
                    return type_int(); /* fallback */
                }
                return type_args[i];
            }
        }

        /* 非类型参数：如果有 args 则递归替换 args */
        if (node->as.named.arg_count > 0) {
            int nargs = node->as.named.arg_count;
            Type **resolved_args = (Type **)malloc_safe(
                (size_t)nargs * sizeof(Type *));
            for (int i = 0; i < nargs; i++) {
                resolved_args[i] = resolve_type_node_with_substitution(
                    c, node->as.named.args[i], tp_names, type_args, tp_count);
            }

            Type *result = NULL;

            /* 尝试内建 enum 模板（Option, Result） */
            int tmpl_idx = find_template_idx(c, name);
            if (tmpl_idx >= 0) {
                result = instantiate_template(c, tmpl_idx,
                    resolved_args, nargs, node->line, node->column);
            }
            /* 尝试用户泛型 struct */
            if (result == NULL) {
                result = checker_instantiate_struct(c, name,
                    resolved_args, nargs, node->line, node->column);
            }
            /* 回退到普通 resolve（可能是非泛型同名） */
            if (result == NULL) {
                result = resolve_type_node(c, node);
            }

            free(resolved_args);
            return result;
        }

        /* 无 args 的普通 named type */
        return resolve_type_node(c, node);
    }

    case TYPE_NODE_VECTOR: {
        Type *elem = resolve_type_node_with_substitution(
            c, node->as.vec.elem, tp_names, type_args, tp_count);
        return type_vector(elem);
    }

    case TYPE_NODE_MAP: {
        Type *key = resolve_type_node_with_substitution(
            c, node->as.map.key, tp_names, type_args, tp_count);
        Type *val = resolve_type_node_with_substitution(
            c, node->as.map.val, tp_names, type_args, tp_count);
        return type_map(key, val);
    }

    case TYPE_NODE_ARRAY: {
        Type *elem = resolve_type_node_with_substitution(
            c, node->as.array.elem, tp_names, type_args, tp_count);
        return type_array(elem, node->as.array.size);
    }

    case TYPE_NODE_POINTER: {
        Type *pointee = resolve_type_node_with_substitution(
            c, node->as.pointee, tp_names, type_args, tp_count);
        return type_pointer(pointee);
    }

    case TYPE_NODE_REFERENCE: {
        Type *pointee = resolve_type_node_with_substitution(
            c, node->as.pointee, tp_names, type_args, tp_count);
        return node->is_mut ? type_mut_reference(pointee)
                            : type_reference(pointee);
    }

    case TYPE_NODE_FN:
    case TYPE_NODE_BLOCK: {
        int n = node->as.fn.param_count;
        Type **params = NULL;
        if (n > 0) {
            params = (Type **)malloc_safe((size_t)n * sizeof(Type *));
            for (int i = 0; i < n; i++) {
                params[i] = resolve_type_node_with_substitution(
                    c, node->as.fn.params[i], tp_names, type_args, tp_count);
            }
        }
        Type *ret = resolve_type_node_with_substitution(
            c, node->as.fn.ret, tp_names, type_args, tp_count);
        if (node->kind == TYPE_NODE_BLOCK)
            return type_block(params, n, ret);
        else
            return type_function(params, n, ret, false);
    }

    default:
        return resolve_type_node(c, node);
    }
}
```

### 6.3 测试

此步骤可内部测试（checker 内部调用），或等 Step 5 端到端验证。

### 6.4 涉及文件

| 文件 | 改动 |
|------|------|
| `src/checker.c` | +80 行 |

---

## 7. Step 5: `checker_instantiate_struct` + has_drop

### 7.1 核心函数

**文件**：`src/checker.c`（实现）+ `src/checker.h`（声明）

```c
/* checker.h */
Type *checker_instantiate_struct(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col);
```

```c
/* checker.c */

static int find_struct_template_idx(Checker *c, const char *base_name) {
    for (int i = 0; i < c->struct_template_count; i++) {
        if (strcmp(c->struct_templates[i].base_name, base_name) == 0)
            return i;
    }
    return -1;
}

Type *checker_instantiate_struct(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col)
{
    int tmpl_idx = find_struct_template_idx(c, base_name);
    if (tmpl_idx < 0) return NULL;

    int expected_tpc = c->struct_templates[tmpl_idx].type_param_count;
    if (type_arg_count != expected_tpc) {
        checker_error(c, line, col,
                      "generic struct '%s' expects %d type argument(s), got %d",
                      base_name, expected_tpc, type_arg_count);
        return NULL;
    }

    /* Build mangled name: "Pair(int,string)" */
    char buf[512];
    int pos = snprintf(buf, sizeof(buf), "%s(", base_name);
    for (int i = 0; i < type_arg_count && pos < (int)sizeof(buf) - 2; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",");
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "%s", type_name(type_args[i]));
    }
    snprintf(buf + pos, sizeof(buf) - (size_t)pos, ")");

    /* Cache hit? */
    Type *cached = find_struct_type(c, buf);
    if (cached) return cached;

    /* Instantiate */
    AstNode *decl = c->struct_templates[tmpl_idx].decl_node;
    int fc = decl->as.struct_decl.field_count;
    char **tp_names = c->struct_templates[tmpl_idx].type_params;

    /* Pre-register empty shell (handles self-recursive generics) */
    char *mangled = str_dup(buf);
    Type *st = type_struct(mangled, fc);
    register_struct_type(c, st->as.strukt.name, st);

    /* Fill fields with type substitution */
    bool has_drop = false;
    for (int i = 0; i < fc; i++) {
        TypeNode *ft_node = decl->as.struct_decl.field_types[i];
        Type *ft = resolve_type_node_with_substitution(
            c, ft_node, tp_names, type_args, type_arg_count);
        if (ft == NULL) {
            checker_error(c, ft_node->line, ft_node->column,
                "cannot resolve field type in '%s'", buf);
            ft = type_int();
        }
        st->as.strukt.fields[i].name = decl->as.struct_decl.field_names[i];
        st->as.strukt.fields[i].type = ft;

        if (type_is_has_drop_generic(ft)) has_drop = true;
    }
    st->as.strukt.has_drop = has_drop;

    return st;
}
```

### 7.2 `type_is_has_drop_generic` 辅助

```c
/* Returns true if a type requires a destructor. */
static bool type_is_has_drop_generic(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_STRING:  return true;
    case TYPE_VECTOR:  return true;
    case TYPE_MAP:     return true;
    case TYPE_BLOCK:   return true;
    case TYPE_STRUCT:  return t->as.strukt.has_drop;
    case TYPE_ENUM:    return t->as.enom.has_drop;
    default:           return false;
    }
}
```

### 7.3 `resolve_type_node` 中分发到泛型 struct

在现有 `resolve_type_node` 的 `TYPE_NODE_NAMED` 分支中，`find_struct_type` / `find_enum_type` 之前插入：

```c
case TYPE_NODE_NAMED: {
    const char *name = node->as.named.name;
    int nargs = node->as.named.arg_count;

    /* ... existing type_alias check ... */

    /* G1: 带类型参数的 named type → 尝试泛型 struct 实例化 */
    if (nargs > 0 && find_struct_template_idx(c, name) >= 0) {
        Type **targs = (Type **)malloc_safe((size_t)nargs * sizeof(Type *));
        for (int i = 0; i < nargs; i++)
            targs[i] = resolve_type_node(c, node->as.named.args[i]);
        Type *result = checker_instantiate_struct(c, name,
            targs, nargs, node->line, node->column);
        free(targs);
        if (result) return result;
    }

    /* ... existing find_struct_type, find_enum_type, Option/Result ... */
}
```

### 7.4 `check_new_expr` 适配

当用户写 `Pair(int, string) { first: 42, second: "hello" }` 时：
- Parser 解析为 `AST_NEW_EXPR`，`struct_name = "Pair"`（不含类型参数）
- 但此时 `struct_types["Pair"]` 不存在，只有 `struct_types["Pair(int,string)"]`

**方案**：在 `check_new_expr` 中，若查找 `struct_name` 失败且存在同名 template：
- 需要从使用上下文（变量声明的类型、函数参数的 expected type）获取具体类型参数
- **或者**修改 Parser，让 `AST_NEW_EXPR` 的 `struct_name` 保留完整的 `"Pair(int, string)"`

**推荐方案**（Parser 修改最小化）：
`check_new_expr` 改为先尝试从 `c->expected_type`（由变量声明传播）获取 mangled name：

```c
static void check_new_expr(Checker *c, AstNode *node) {
    const char *sname = node->as.new_expr.struct_name;
    Type *st = find_struct_type(c, sname);

    /* G1: struct_name 只是基础名（"Pair"），尝试从 expected_type 获取实例化类型 */
    if (!st && c->expected_type && c->expected_type->kind == TYPE_STRUCT) {
        /* 验证 expected_type 的 base name 匹配 */
        const char *mangled = c->expected_type->as.strukt.name;
        if (strncmp(mangled, sname, strlen(sname)) == 0
            && mangled[strlen(sname)] == '(') {
            st = c->expected_type;
        }
    }

    if (!st) {
        checker_error(c, node->line, node->column,
                      "unknown struct '%s'", sname);
        return;
    }

    node->resolved_type = st;
    /* 后续字段检查照旧... */
}
```

**同时**需要在 `check_var_decl` 中设置 `c->expected_type`：

```c
/* 在 check_var_decl 的 check_expr(init) 之前 */
if (var_type) c->expected_type = var_type;
check_expr(c, init_node);
c->expected_type = NULL;
```

（这个 expected_type 传播与闭包的类型推导模式一致。）

### 7.5 端到端测试

```ls
// tests/samples/generics_g1_test.ls
struct Pair(T, U) {
    T first
    U second
}

fn main() {
    // G1.1: POD 字段，has_drop=false
    Pair(int, int) p1 = Pair(int, int) { first: 1, second: 2 }
    print(p1.first)    // 1
    print(p1.second)   // 2

    // G1.2: string 字段，has_drop=true
    Pair(string, int) p2 = Pair(string, int) { first: "hello", second: 42 }
    print(p2.first)    // hello
    print(p2.second)   // 42

    // G1.3: 同一泛型不同实例
    Pair(int, string) p3 = Pair(int, string) { first: 99, second: "world" }
    print(p3.first)    // 99
    print(p3.second)   // world

    // G1.4: 嵌套泛型
    Pair(Pair(int, int), string) p4 = Pair(Pair(int, int), string) {
        first: p1,
        second: "nested"
    }
    print(p4.first.first)  // 1
    print(p4.second)       // nested
}
```

**验收**：AOT + JIT + memcheck 0 leak / 0 dfree，ctest 全过。

### 7.6 涉及文件

| 文件 | 改动 |
|------|------|
| `src/checker.h` | +1 行声明 |
| `src/checker.c` | +100 行（`checker_instantiate_struct` + `resolve_type_node` 修改 + `check_new_expr` 适配） |

---

## 8. Step 6: Codegen 适配

### 8.1 问题

`codegen_new_expr` 当前用 `node->as.new_expr.struct_name`（原名 `"Pair"`）查找 struct type。泛型实例化后注册的名字是 `"Pair(int,string)"`。

### 8.2 修改

**文件**：`src/codegen.c`，`codegen_new_expr` 函数

将 struct 查找改为优先使用 `node->resolved_type`：

```c
static LLVMValueRef codegen_new_expr(CodegenContext *ctx, AstNode *node) {
    Type *st = node->resolved_type;
    const char *sname = st ? st->as.strukt.name : node->as.new_expr.struct_name;

    /* 使用 mangled name 查找 LLVM struct type */
    LLVMTypeRef llvm_st = find_struct_llvm(ctx, sname);
    if (!llvm_st) {
        llvm_st = type_to_llvm(ctx, st);
        register_struct_llvm(ctx, sname, llvm_st);
    }

    /* 后续字段赋值照旧... */
}
```

### 8.3 关于 `emit_auto_drop_fn`

`emit_auto_drop_fn` 已经按字段类型逐个生成 drop 代码。泛型实例化的 struct（如 `Pair(string,int)`）字段类型已经是具体的 `TYPE_STRING` + `TYPE_INT`，所以 `emit_auto_drop_fn` **不需要任何修改**。

同理：`emit_scope_cleanup`、`emit_cleanup_to`、`emit_struct_clone_val` 都基于 `Type*` 的字段遍历，不关心 struct 名字是否含 `(`。

### 8.4 测试

**验收**：`generics_g1_test.ls` AOT + JIT + memcheck 全过。

### 8.5 涉及文件

| 文件 | 改动 |
|------|------|
| `src/codegen.c` | ~10 行修改（`codegen_new_expr` 改用 `resolved_type`） |

---

## 9. Step 7: AST + Parser 扩展 `impl(T) Stack(T)`

### 9.1 AST 改动

**文件**：`src/ast.h`

```c
struct {
    char *name;
    char **type_params;      /* G1.5: ["T"] for impl(T) Stack(T); NULL if non-generic */
    int   type_param_count;  /* G1.5: 0 for non-generic */
    AstNode **methods;
    int method_count;
} impl_decl;
```

**文件**：`src/ast.c`，`ast_free` 的 `AST_IMPL_DECL` 分支：

```c
/* G1.5: free type_params */
for (int i = 0; i < node->as.impl_decl.type_param_count; i++)
    free(node->as.impl_decl.type_params[i]);
free(node->as.impl_decl.type_params);
```

### 9.2 Parser 改动

**文件**：`src/parser.c`，`parse_impl_decl` 函数

在 consume `impl` 之后，consume struct name 之前：

```c
/* G1.5: optional type param list */
char **type_params = NULL;
int   type_param_count = 0;
int   type_param_cap = 0;

if (check(p, TOKEN_LPAREN)) {
    advance(p); /* consume '(' */
    if (check(p, TOKEN_IDENTIFIER) && is_upper_ident(p->current)) {
        do {
            if (!check(p, TOKEN_IDENTIFIER) || !is_upper_ident(p->current)) {
                error_at_current(p, "expected uppercase type parameter name");
                break;
            }
            advance(p);
            char *tpname = str_dup_n(p->previous.start, p->previous.length);
            PUSH_ARRAY(type_params, type_param_count, type_param_cap, tpname);
        } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after impl type params");
    } else {
        error_at_current(p, "expected type parameter name");
    }
}
```

解析 struct name 后，跳过可选的 `(T, U)` 类型参数列表（验证在 checker）：

```c
/* 消费 struct name */
advance(p);
char *name = str_dup_n(p->previous.start, p->previous.length);

/* 跳过 impl 目标的类型参数：impl(T) Stack(T) 中的第二个 (T) */
if (check(p, TOKEN_LPAREN)) {
    advance(p); /* ( */
    while (!check(p, TOKEN_RPAREN) && !check(p, TOKEN_EOF))
        advance(p);
    consume(p, TOKEN_RPAREN, "expected ')'");
}
```

赋值 AST：
```c
n->as.impl_decl.type_params = type_params;
n->as.impl_decl.type_param_count = type_param_count;
```

### 9.3 测试

```ls
struct Stack(T) {
    int placeholder
}

impl(T) Stack(T) {
    fn len() -> int { return 0 }
}

fn main() { print("impl parse ok") }
```

**验收**：parser + checker 不 crash（checker 遇到泛型 impl 注册后 return），ctest 全过。

### 9.4 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ast.h` | +2 行字段 |
| `src/ast.c` | +3 行释放 |
| `src/parser.c` | +25 行 |

---

## 10. Step 8: Checker 注册泛型 impl + 方法签名实例化

### 10.1 泛型 impl 注册

**文件**：`src/checker.c`，`check_impl_decl` 入口

```c
static void check_impl_decl(Checker *c, AstNode *node) {
    const char *base_name = node->as.impl_decl.name;
    int tpc = node->as.impl_decl.type_param_count;

    if (tpc > 0) {
        /* G1.5: 泛型 impl → 绑定到 struct template */
        int tidx = find_struct_template_idx(c, base_name);
        if (tidx < 0) {
            checker_error(c, node->line, node->column,
                "impl for unknown generic struct '%s'", base_name);
            return;
        }
        if (c->struct_templates[tidx].impl_node != NULL) {
            checker_error(c, node->line, node->column,
                "duplicate impl for generic struct '%s'", base_name);
            return;
        }
        c->struct_templates[tidx].impl_node = node;
        return;
    }

    /* 非泛型 impl: 现有逻辑不变 */
}
```

### 10.2 联动：实例化时注册方法签名

在 `checker_instantiate_struct` 末尾，如果 template 有 impl_node：

```c
    /* G1.5: instantiate associated impl methods (type signatures only) */
    if (c->struct_templates[tmpl_idx].impl_node != NULL) {
        instantiate_impl_method_types(c, st, buf,
            c->struct_templates[tmpl_idx].impl_node,
            tp_names, type_args, type_arg_count);
    }

    return st;
}
```

```c
static void instantiate_impl_method_types(
    Checker *c, Type *struct_type, const char *mangled_name,
    AstNode *impl_node,
    char **tp_names, Type **type_args, int tp_count)
{
    /* 确保 impl_registry 有 mangled_name 的条目 */
    int impl_idx = find_or_create_impl(c, mangled_name);

    int mc = impl_node->as.impl_decl.method_count;
    for (int m = 0; m < mc; m++) {
        AstNode *method = impl_node->as.impl_decl.methods[m];
        const char *mname = method->as.fn_decl.name;
        bool is_static = method->as.fn_decl.is_static;
        int sbk = method->as.fn_decl.self_borrow_kind;

        /* 构造具体方法签名 */
        int pc = method->as.fn_decl.param_count;

        /* self 参数（非 static 方法）  */
        int total = is_static ? pc : pc + 1;
        Type **params = (Type **)malloc_safe((size_t)total * sizeof(Type *));
        int offset = 0;
        if (!is_static) {
            params[0] = type_pointer(struct_type);
            offset = 1;
        }

        for (int j = 0; j < pc; j++) {
            params[offset + j] = resolve_type_node_with_substitution(
                c, method->as.fn_decl.param_types[j],
                tp_names, type_args, tp_count);
        }

        Type *ret = method->as.fn_decl.return_type
            ? resolve_type_node_with_substitution(
                c, method->as.fn_decl.return_type,
                tp_names, type_args, tp_count)
            : type_void();

        Type *mtype = type_function(params, total, ret, false);

        register_method(c, impl_idx, mname, mtype, is_static, sbk);
    }
}
```

**关键**：这里只注册方法的 **Type 签名**，不碰方法的 AST body。Codegen 在 Step 9 处理。

### 10.3 `check_new_expr` 处理泛型 struct literal

`Pair(int, string) { first: 42, second: "hello" }` 中，Parser 解析 struct literal 时 struct_name 可能只有 "Pair"。

**更好的 Parser 修改**（在 `parse_struct_literal` 或 `parse_new_expr` 中）：如果 struct name 后面跟 `(`，解析类型参数列表，将 struct_name 存为 mangled 形式 `"Pair(int,string)"`。

或者，利用 Step 5 中的 `expected_type` 方案（从变量声明传播）。两种方式都可以。

### 10.4 测试

```ls
struct Stack(T) { vec(T) items }

impl(T) Stack(T) {
    fn push(T item) { self.items.push(item) }
    fn len() -> int { return self.items.length }
}

fn main() {
    Stack(int) s = Stack(int) { items: [] }
    // 此时 checker 已注册 Stack(int) 的方法签名
    // 但 codegen 还不能 emit 方法体 → 仅测 checker 通过
    print("checker ok")
}
```

**验收**：checker 通过（方法签名注册成功），codegen 暂时跳过泛型方法 emit。ctest 现有全过。

### 10.5 涉及文件

| 文件 | 改动 |
|------|------|
| `src/checker.c` | +60 行（`check_impl_decl` 分支 + `instantiate_impl_method_types`） |

---

## 11. Step 9: Codegen 从模板 AST + 替换表 emit 方法

### 11.1 核心设计：**不克隆 AST，codegen 阶段用替换表"即时"emit**

这是与上次 G1.5 **根本不同**的路线：

```
上次：Checker 克隆方法 AST → 合成节点 → ast_free double-free
本次：Codegen 持有 {模板方法 AST, 替换表} → emit 时即时替换 → 原始 AST 不修改
```

### 11.2 数据结构

**文件**：`src/codegen.h`（或 `codegen.c` 内部 static）

```c
/* Pending generic method: template AST + substitution table */
typedef struct {
    char     *mangled_fn_name;  /* "Stack(int)__push"，owned */
    AstNode  *method_ast;       /* 指向模板 impl 的方法 AST，不 own */
    Type     *struct_type;      /* 实例化后的 struct Type */
    char    **tp_names;         /* 类型参数名列表，不 own（指向 struct_template） */
    Type    **type_args;        /* 具体类型参数，owned (Type* 是全局的不需释放) */
    int       tp_count;
} PendingGenericMethod;
```

在 `CodegenContext` 中：
```c
PendingGenericMethod *pending_generic_methods;
int pending_gm_count;
int pending_gm_cap;
```

### 11.3 注册 pending 方法

在 `checker_instantiate_struct` → `instantiate_impl_method_types` 路径中，
**或者**在 codegen 的 Pass A（forward-declare）阶段，检测到泛型 struct 实例化时，
为每个方法注册一个 pending entry。

**推荐**：在 codegen 的 Pass A 中做。当 codegen 遍历 `struct_types` 并发现某个 struct 是泛型实例（name 含 `(`），且有 impl：

```c
/* codegen Pass A: 对泛型实例的方法做 forward-declare */
for (int i = 0; i < ctx->checker->struct_template_count; i++) {
    AstNode *impl = ctx->checker->struct_templates[i].impl_node;
    if (!impl) continue;

    /* 遍历所有已实例化的该模板的具体类型 */
    /* 从 struct_types 中筛选以 base_name + "(" 开头的 */
    /* 对每个实例：forward-declare + 注册 pending */
}
```

更实际的做法：在 `checker_instantiate_struct` 的末尾记录需要 codegen 的方法信息到 Checker 上，然后 Codegen 在 Pass A 统一处理。

### 11.4 方法 emit：替换表驱动

当 codegen 需要 emit `Stack(int)__push(self, item)` 时：

```c
static void codegen_generic_method(CodegenContext *ctx, PendingGenericMethod *pm) {
    /* 1. 创建 LLVM function（签名已在 forward-declare 阶段确定） */
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, pm->mangled_fn_name);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(fn, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    /* 2. 保存/恢复 codegen 状态 */
    LLVMValueRef saved_fn = ctx->current_fn;
    LLVMBasicBlockRef saved_bb = /* ... */;
    ctx->current_fn = fn;

    /* 3. 将类型替换表压入 ctx（新字段：active_substitution） */
    ctx->active_tp_names = pm->tp_names;
    ctx->active_type_args = pm->type_args;
    ctx->active_tp_count = pm->tp_count;

    /* 4. 注册 self + 参数到作用域 */
    codegen_fn_params(ctx, pm->method_ast, pm->struct_type);

    /* 5. emit body（body 中遇到类型 T 时通过替换表解析） */
    codegen_block(ctx, pm->method_ast->as.fn_decl.body);

    /* 6. 清除替换表 */
    ctx->active_tp_names = NULL;
    ctx->active_type_args = NULL;
    ctx->active_tp_count = 0;

    /* 7. 恢复状态 */
    ctx->current_fn = saved_fn;
    /* ... */
}
```

### 11.5 codegen 中类型解析的替换

在 codegen 中需要将 TypeNode 解析为 LLVM 类型时（例如参数类型、局部变量类型），需要检查 active substitution：

```c
/* codegen 的 resolve_type_for_codegen：优先走替换表 */
static Type *cg_resolve_type(CodegenContext *ctx, TypeNode *tn) {
    if (ctx->active_tp_count > 0) {
        return resolve_type_node_with_substitution(
            ctx->checker, tn,
            ctx->active_tp_names, ctx->active_type_args, ctx->active_tp_count);
    }
    return resolve_type_node(ctx->checker, tn);
}
```

但 **问题**：codegen 通常不直接调用 `resolve_type_node`——它使用 `node->resolved_type`（checker 阶段已填好的）。

**更好的方案**：让 checker 在 `instantiate_impl_method_types` 之后，**对方法 body 做一轮 type-check**，但用替换表。这样 body 中每个节点的 `resolved_type` 都被填为具体类型，codegen 照旧使用 `resolved_type` 即可。

但这又回到了"需要给 body 的每个节点设置 resolved_type"的问题——而 body 是模板的 AST，不能修改（否则第二个实例会覆盖第一个的 resolved_type）。

**终极方案**：

方案 A：**每个实例化的方法 body 做一次 shallow AST clone + type-check**
- 需要可靠的 AST clone → 这是 G1.5 失败的点
- 但如果 Step 0 的 `type_node_clone` + 加强的 `ast_clone_node`（所有字符串做 strdup）已经就位，可以安全使用

方案 B：**Codegen 走模板 AST，but body 编译时动态查替换表**
- 需要在 codegen 的很多地方加 `if (has_substitution)` 分支
- 改动量大，侵入性高

方案 C：**不做 AST clone，而是对 body 做"二次 check + codegen"流水线**
- 给 `checker_check_fn_body` 加一个替换表参数，check 时即时替换
- resolved_type 写入一个 **side table**（per-instantiation map<AstNode*, Type*>），而非 AST 节点
- Codegen 从 side table 读 resolved_type

**推荐方案 A**，辅以 Step 0 的基础设施：

```c
/* 安全的 AST body clone：所有 char* 做 strdup，所有 TypeNode 做 type_node_clone */
AstNode *ast_clone_deep(const AstNode *src);
```

在 Step 0 补充实现 `ast_clone_deep`（与旧的 `ast_clone_node` 不同，新函数做完整深拷贝）。
每个实例化方法拿到一份独立的 body clone，checker type-check 后 resolved_type 写入 clone 节点，codegen 照旧。clone 的 body 在 codegen 完成后 `ast_free` 释放。

**关键区别**：上次失败是因为 `fn_decl` 级别的 clone 共享了 name/param_types 指针。本次只 clone **body**（`fn_decl.body`），fn_decl 本身由 codegen 自己构造（从类型签名），不碰模板的 fn_decl 节点。

### 11.6 实际实现路径

```
a) 在 Step 0 扩展 ast_clone_deep（body-only deep clone）
b) checker_instantiate_struct 末尾：
   - 对每个方法，clone body
   - 用替换表 type-check cloned body
   - 存入 pending_methods 队列（cloned body + 方法签名）
c) codegen Pass A: forward-declare 所有 pending 方法
d) codegen Pass B: emit 所有 pending 方法（cloned body 有 resolved_type）
e) emit 完成后 ast_free(cloned_body)
```

### 11.7 测试

```ls
struct Stack(T) { vec(T) items }

impl(T) Stack(T) {
    fn push(T item) { self.items.push(item) }
    fn len() -> int { return self.items.length }
}

fn main() {
    Stack(int) s = Stack(int) { items: [] }
    s.push(10)
    s.push(20)
    print(s.len())     // 2

    Stack(string) ss = Stack(string) { items: [] }
    ss.push("hello")
    ss.push("world")
    print(ss.len())    // 2
}
```

**验收**：AOT + JIT + memcheck 0 leak / 0 dfree。

### 11.8 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ast.h` | +1 行（`ast_clone_deep` 声明） |
| `src/ast.c` | +100 行（`ast_clone_deep` 实现） |
| `src/checker.c` | +30 行（body clone + type-check） |
| `src/codegen.c` | +80 行（pending 队列 + forward-declare + emit） |

---

## 12. Step 10: 泛型函数 `fn identity(T)(T x) -> T`

### 12.1 语法

```ls
fn identity(T)(T x) -> T { return x }
fn make_pair(T, U)(T a, U b) -> Pair(T, U) {
    return Pair(T, U) { first: a, second: b }
}
```

类型参数列表在函数名和普通参数之间：`fn name(TYPE_PARAMS)(PARAMS) -> RET`。

### 12.2 AST 扩展

**文件**：`src/ast.h`

```c
struct {
    char *name;
    char **type_params;      /* G2: ["T"] for fn id(T)(...); NULL if non-generic */
    int   type_param_count;  /* G2: 0 for non-generic */
    TypeNode **param_types;
    char **param_names;
    int param_count;
    TypeNode *return_type;
    AstNode *body;
    /* ... existing fields ... */
} fn_decl;
```

`ast_free` 对应释放 `type_params`。

### 12.3 Parser

`parse_fn_decl` 在解析完函数名后：

```c
/* G2: optional type param list before regular params */
if (check(p, TOKEN_LPAREN)) {
    /* Peek: if next token is uppercase ident, it's type params */
    /* 需要 lookahead 或试探 */
    Token saved = p->current;
    advance(p); /* consume '(' */
    if (check(p, TOKEN_IDENTIFIER) && is_upper_ident(p->current)) {
        /* 解析类型参数列表 */
        do { /* ... same pattern as struct ... */ } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "...");
        /* 接下来是 '(' 开始的普通参数列表 */
    } else {
        /* 不是类型参数 → 回退，当作普通参数列表 */
        /* 方法：把 '(' 当作已消费的 regular param list 的开始 */
        /* （需要调整 parse flow） */
    }
}
```

**二义性处理**：函数名后的 `(` 可能是类型参数或普通参数。判断规则：
- 如果 `(` 后紧跟大写首字母标识符 → 类型参数
- 否则 → 普通参数

这与 struct 的规则一致。

### 12.4 Checker

新增 `fn_templates` 注册表：

```c
struct {
    const char *name;
    char      **type_params;
    int         type_param_count;
    AstNode    *decl_node;
} *fn_templates;
int fn_template_count;
int fn_template_cap;
```

`check_fn_decl` 入口：

```c
if (node->as.fn_decl.type_param_count > 0) {
    register_fn_template(c, node);
    return;
}
```

调用点 `identity(int)(42)` 的处理：
1. `check_call` 检测 callee 是 fn_template
2. 显式类型参数 `(int)` → 解析为 type_args
3. Mangled name：`"identity(int)"`
4. 缓存检查 → clone body → type-check → 注册到 scope

### 12.5 类型推断（显式调用 first，推断 later）

**v1 限制**：只支持显式类型参数 `identity(int)(42)`，不支持推断 `identity(42)`。

推断可以在后续版本（G2.5）添加：从实参类型反推类型参数。

### 12.6 测试

```ls
fn identity(T)(T x) -> T { return x }

fn main() {
    int a = identity(int)(42)
    print(a)           // 42

    string s = identity(string)("hello")
    print(s)           // hello
}
```

**验收**：AOT + JIT + memcheck 0 leak / 0 dfree。

### 12.7 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ast.h` | +2 行字段 |
| `src/ast.c` | +3 行释放 |
| `src/parser.c` | +30 行 |
| `src/checker.c` | +80 行（fn_templates + 调用点实例化） |
| `src/codegen.c` | +40 行（pending fn 队列 + emit） |

---

## 13. has_drop 传播完整矩阵

确保每个实例化的 struct 正确推导 has_drop：

| 泛型 struct 字段 | T=int | T=string | T=vec(int) | T=Pair(string,int) |
|-----------------|-------|----------|------------|-------------------|
| `T field` | has_drop=❌ | has_drop=✅ | has_drop=✅ | has_drop=✅ |
| `vec(T) field` | has_drop=✅ | has_drop=✅ | has_drop=✅ | has_drop=✅ |
| `Option(T) field` | has_drop=❌ | has_drop=✅ | has_drop=✅ | has_drop=✅ |
| `int field` | has_drop=❌ | has_drop=❌ | has_drop=❌ | has_drop=❌ |

关键：`vec(T)` 本身始终 has_drop（vec 管理堆内存），与 T 无关。但 `T field` 直接取决于 T。

---

## 14. 风险与回退策略

| Step | 风险 | 回退 |
|------|------|------|
| 0 | `type_node_clone` 漏处理某种 TypeNode | 只添加不修改，直接删除 |
| 1-2 | AST/Parser 改动破坏现有 struct | `type_param_count=0` 保证旧路径不变 |
| 3-5 | Checker 实例化逻辑 bug | `struct_template_count=0` 时全跳过 |
| 6 | Codegen 查找 mangled name 失败 | 回退到原 `struct_name` 查找 |
| 7-8 | 泛型 impl 类型签名错误 | `impl_node=NULL` 时跳过 |
| 9 | AST body clone double-free | **最大风险**，用 `ast_clone_deep` + 测试覆盖 |
| 10 | 泛型函数解析二义性 | 限制为显式类型参数 |

**每步完成后跑 ctest 全套，不减少为原则。**

---

## 15. 总文件改动量估算

| 文件 | 新增行数（估算） |
|------|-----------------|
| `src/ast.h` | +15 |
| `src/ast.c` | +160（type_node_clone + ast_clone_deep + ast_free 扩展） |
| `src/parser.c` | +90（struct/impl/fn 类型参数解析 + is_upper_ident） |
| `src/checker.h` | +20（struct_templates + fn_templates + 公开 API） |
| `src/checker.c` | +350（核心：实例化 + 替换 + impl 方法签名） |
| `src/codegen.c` | +130（pending 方法队列 + emit + new_expr 适配） |
| `tests/` | +5 个 .ls + 5 个 .cmake |

**总计**：约 765 行新增，分 10 步交付，每步 ~75 行。
