# LS 用户自定义泛型实现规范

**文档版本**：v1.0  
**目标语言版本**：Phase G（G1 / G1.5 / G2）  
**语法风格**：括号 `()` — 与 `vec(T)` / `Option(T)` / `Result(T,E)` 完全一致

---

## 0. 概述

### 0.1 最终目标语法

```ls
// Phase G1：泛型结构体
struct Pair(T, U) {
    T first
    U second
}

// Phase G1.5：泛型 impl 块
struct Stack(T) {
    vec(T) items
}

impl(T) Stack(T) {
    fn push(T item) {
        items.push(item)
    }
    fn pop() -> Option(T) {
        if items.length == 0 { return None }
        return Some(items.pop())
    }
    fn len() -> int {
        return items.length
    }
}

// Phase G2：泛型函数
fn identity(T)(T x) -> T {
    return x
}

fn main() {
    Pair(int, string) p = Pair(int, string) { first: 42, second: "hello" }
    print(p.first)    // 42
    print(p.second)   // hello

    Stack(int) s
    s.push(10)
    s.push(20)
    print(s.pop())    // Some(20)
    print(s.len())    // 1

    print(identity(int)(42))    // 42 — 显式
    print(identity(99))         // 99 — 推断（Phase G2 扩展）
}
```

### 0.2 设计核心原则

1. **单态化**（Monomorphization）：每个具体类型组合生成独立的 LLVM IR，零运行时开销
2. **声明时不展开**：遇到泛型声明只注册模板，不产生任何类型；遇到使用时才实例化
3. **Codegen 层零改动**：单态化在 Checker 完成，Codegen 只看到完全具体的类型
4. **复用现有基础设施**：`instantiate_template`（enum 模板实例化）框架直接复用
5. **has_drop 在实例化时重新推导**：不继承模板的静态 has_drop 值

### 0.3 实现阶段

| Phase | 内容 | 依赖 |
|-------|------|------|
| G1 | 泛型结构体声明 + 实例化（无方法） | 无 |
| G1.5 | 泛型 impl 块（方法系统） | G1 |
| G2 | 泛型函数（类型推断） | G1 |

---

## 1. Phase G1：泛型结构体

### 1.1 AST 扩展（`src/ast.h`）

#### 1.1.1 `struct_decl` 节点加类型参数列表

**当前定义**（第 347–351 行）：
```c
struct {
    char *name;
    TypeNode **field_types;
    char **field_names;
    int field_count;
} struct_decl;
```

**修改后**：
```c
struct {
    char *name;
    /* G1: type parameter names, e.g. ["T", "U"] for struct Pair(T, U).
       NULL / 0 for non-generic structs. Owned (freed by ast_free). */
    char **type_params;
    int   type_param_count;
    TypeNode **field_types;
    char **field_names;
    int field_count;
} struct_decl;
```

#### 1.1.2 `impl_decl` 节点加类型参数列表（Phase G1.5 用，此处一并声明）

**当前定义**（第 368–372 行）：
```c
struct {
    char *name;
    AstNode **methods;
    int method_count;
} impl_decl;
```

**修改后**：
```c
struct {
    char *name;
    /* G1.5: type parameter names for generic impl, e.g. ["T"] for impl(T) Stack(T).
       NULL / 0 for non-generic impl blocks. Owned (freed by ast_free). */
    char **type_params;
    int   type_param_count;
    /* G1.5: when this impl targets a generic struct, the instantiation args
       as written in source, e.g. Stack(T) → type_args=["T"], type_arg_count=1.
       NULL for non-generic. Owned (freed by ast_free). */
    char **type_args;
    int   type_arg_count;
    AstNode **methods;
    int method_count;
} impl_decl;
```

#### 1.1.3 `fn_decl` 节点加类型参数列表（Phase G2 用）

**当前定义**（第 332–344 行）在 `fn_decl` union 字段中增加：
```c
/* G2: type parameter names for generic fn, e.g. ["T"] for fn id(T)(T x)->T.
   NULL / 0 for non-generic functions. Owned (freed by ast_free). */
char **type_params;
int   type_param_count;
```

#### 1.1.4 `ast_free` 修改（`src/ast.c`）

在 `AST_STRUCT_DECL` 分支内，`ast_free` 需要释放 `type_params`：
```c
case AST_STRUCT_DECL:
    free(node->as.struct_decl.name);
    /* G1: free type params */
    for (int i = 0; i < node->as.struct_decl.type_param_count; i++)
        free(node->as.struct_decl.type_params[i]);
    free(node->as.struct_decl.type_params);
    /* existing field cleanup ... */
    break;
```

`AST_IMPL_DECL` 和 `AST_FN_DECL` 同理。

---

### 1.2 Parser 扩展（`src/parser.c`）

#### 1.2.1 `parse_struct_decl`（当前第 1846 行）

**当前逻辑**（简化）：
```
consume struct_name
consume '{'
while not '}': parse field_type + field_name
consume '}'
```

**修改后逻辑**（在 `consume(struct_name)` 之后，`consume('{')` 之前插入）：

```c
/* G1: parse optional type parameter list: struct Name(T, U, ...) */
char **type_params = NULL;
int   type_param_count = 0;
int   type_param_cap = 0;

if (check(p, TOKEN_LPAREN)) {
    /* Disambiguate: type params vs. no params.
       Rule: immediately after struct name, '(' followed by
       one or more identifiers that start with an uppercase letter,
       separated by ',' and closed by ')' before '{'.
       If the first token after '(' is NOT an uppercase-initial identifier,
       treat as error (struct has no value params in declaration). */
    Token lookahead = peek_next(p); /* token after '(' */
    if (is_upper_ident(lookahead)) {
        consume(p, TOKEN_LPAREN, NULL);
        do {
            if (!check(p, TOKEN_IDENTIFIER)) {
                error_at_current(p, "expected type parameter name (uppercase identifier)");
                break;
            }
            if (!is_upper_ident(p->current)) {
                error_at_current(p, "type parameter names must start with an uppercase letter");
                break;
            }
            advance(p);
            char *tpname = str_dup_n(p->previous.start, p->previous.length);
            if (type_param_count >= type_param_cap) {
                type_param_cap = GROW_CAPACITY(type_param_cap);
                type_params = GROW_ARRAY(char *, type_params, type_param_cap);
            }
            type_params[type_param_count++] = tpname;
        } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after type parameters");
    }
}
```

**大写首字母判断辅助函数**（在 parser.c 文件顶部添加）：
```c
static bool is_upper_ident(Token t) {
    return t.type == TOKEN_IDENTIFIER
        && t.length > 0
        && t.start[0] >= 'A' && t.start[0] <= 'Z';
}
```

**peek_next 函数**：Parser 已有 `p->current` 和 `p->previous`，需要一次额外 lookahead。实现方式：调用 `check(p, TOKEN_LPAREN)` 时 `p->current` 就是 `(`，调用 scanner 的单步 scan 得到下一个 token（保存后恢复）。或者更简单：先 `advance()` 消费 `(`，再检查 `p->current` 是否是大写标识符，若不是则报错。

**实际实现建议（无需 peek_next）**：
```c
if (check(p, TOKEN_LPAREN)) {
    /* Save scanner state to allow backtrack-free decision */
    advance(p); /* consume '(' */
    if (check(p, TOKEN_IDENTIFIER) && is_upper_ident(p->current)) {
        /* It IS a type param list */
        do {
            if (!check(p, TOKEN_IDENTIFIER) || !is_upper_ident(p->current)) {
                error_at_current(p, "expected uppercase type parameter name");
                break;
            }
            advance(p);
            char *tpname = str_dup_n(p->previous.start, p->previous.length);
            /* ... append to type_params[] ... */
        } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after type parameters");
    } else {
        /* Not a type param list — this is a parse error for struct decl */
        error_at_current(p, "unexpected token after struct name; did you mean 'struct Name(T) {..}'?");
    }
}
```

**AST 节点赋值**（在 `new_node` 之后）：
```c
n->as.struct_decl.type_params       = type_params;
n->as.struct_decl.type_param_count  = type_param_count;
```

#### 1.2.2 `parse_type`（`TYPE_NODE_NAMED` 路径）

`parse_type` 在处理 `TYPE_NODE_NAMED` 时已经支持 `Name(arg1, arg2)` 语法（这是 `Option(int)` 的解析路径）。**无需修改**，用户写 `Pair(int, string)` 时 Parser 已自动解析为：
```c
TypeNode { kind=TYPE_NODE_NAMED, as.named.name="Pair",
           as.named.args=[TypeNode(int), TypeNode(string)], arg_count=2 }
```

这正是 Checker 单态化需要的输入。

---

### 1.3 Checker 扩展（`src/checker.h` + `src/checker.c`）

#### 1.3.1 新增 `StructTemplate` 类型和注册表（`checker.h`）

在 `Checker` 结构体中，在 `enum_templates` 字段之后添加：

```c
/* Generic struct templates (user-defined). Indexed by base name ("Pair", "Stack").
   Registered when check_struct_decl sees type_param_count > 0.
   Instantiated lazily when resolve_type_node encounters Name(T1, T2, ...). */
struct {
    const char   *base_name;         /* "Pair", "Stack", ... */
    char        **type_params;       /* ["T", "U"] — owned pointers into AST strings */
    int           type_param_count;
    AstNode      *decl_node;         /* original AST_STRUCT_DECL node (not owned) */
    /* G1.5: associated impl template (NULL if no impl declared yet) */
    AstNode      *impl_node;         /* AST_IMPL_DECL node (not owned), may be NULL */
} *struct_templates;
int struct_template_count;
int struct_template_cap;
```

#### 1.3.2 新增辅助函数原型（`checker.h` 公开部分末尾）

```c
/* Instantiate a user-defined generic struct type with concrete type args.
   Returns the cached/freshly-built TYPE_STRUCT. NULL on error. */
Type *checker_instantiate_struct(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col);
```

#### 1.3.3 注册泛型结构体模板（`checker.c`，`check_struct_decl` 内）

`check_struct_decl` 当前逻辑：解析字段 → 构造 `Type(TYPE_STRUCT)` → 注册到 `struct_types`。

**修改**：在函数入口处，检测是否为泛型声明：

```c
static void check_struct_decl(Checker *c, AstNode *node) {
    const char *name = node->as.struct_decl.name;
    int tpc = node->as.struct_decl.type_param_count;

    if (tpc > 0) {
        /* Generic struct: register as template, do NOT instantiate now */
        register_struct_template(c, name,
                                 node->as.struct_decl.type_params, tpc,
                                 node);
        return;  /* Skip all field checking — done at instantiation time */
    }

    /* Non-generic: existing logic unchanged */
    /* ... */
}
```

**`register_struct_template` 实现**：

```c
static void register_struct_template(Checker *c, const char *base_name,
                                     char **type_params, int type_param_count,
                                     AstNode *decl_node)
{
    /* Duplicate check: error if already registered */
    for (int i = 0; i < c->struct_template_count; i++) {
        if (strcmp(c->struct_templates[i].base_name, base_name) == 0) {
            checker_error(c, decl_node->line, decl_node->column,
                          "generic struct '%s' already declared", base_name);
            return;
        }
    }
    if (c->struct_template_count >= c->struct_template_cap) {
        c->struct_template_cap = GROW_CAPACITY(c->struct_template_cap);
        c->struct_templates = realloc_safe(c->struct_templates,
            (size_t)c->struct_template_cap * sizeof(c->struct_templates[0]));
    }
    int idx = c->struct_template_count++;
    c->struct_templates[idx].base_name        = base_name;  /* points into AST */
    c->struct_templates[idx].type_params      = type_params;
    c->struct_templates[idx].type_param_count = type_param_count;
    c->struct_templates[idx].decl_node        = decl_node;
    c->struct_templates[idx].impl_node        = NULL;
}
```

#### 1.3.4 核心函数：`checker_instantiate_struct`（`checker.c`）

这是整个泛型系统的核心，与 `instantiate_template`（enum 模板）完全对称。

```c
Type *checker_instantiate_struct(Checker *c,
                                 const char *base_name,
                                 Type **type_args, int type_arg_count,
                                 int line, int col)
{
    /* 1. Find template */
    int tmpl_idx = -1;
    for (int i = 0; i < c->struct_template_count; i++) {
        if (strcmp(c->struct_templates[i].base_name, base_name) == 0) {
            tmpl_idx = i;
            break;
        }
    }
    if (tmpl_idx < 0) return NULL; /* Not a generic struct */

    /* 2. Validate arg count */
    int expected_tpc = c->struct_templates[tmpl_idx].type_param_count;
    if (type_arg_count != expected_tpc) {
        checker_error(c, line, col,
                      "generic struct '%s' expects %d type argument(s), got %d",
                      base_name, expected_tpc, type_arg_count);
        return NULL;
    }

    /* 3. Build mangled name: "Pair(int,string)" */
    char buf[512];
    int pos = snprintf(buf, sizeof(buf), "%s(", base_name);
    for (int i = 0; i < type_arg_count && pos < (int)sizeof(buf) - 2; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",");
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "%s", type_name(type_args[i]));
    }
    snprintf(buf + pos, sizeof(buf) - (size_t)pos, ")");

    /* 4. Cache hit? */
    Type *cached = find_struct_type(c, buf);
    if (cached) return cached;

    /* 5. Instantiate: create new TYPE_STRUCT with concrete field types */
    AstNode *decl = c->struct_templates[tmpl_idx].decl_node;
    int field_count = decl->as.struct_decl.field_count;
    char **tp_names = c->struct_templates[tmpl_idx].type_params;

    /* Pre-register with NULL fields to handle self-referential generics
       (e.g. struct Tree(T) { Option(Tree(T)) left }) */
    char *mangled_name = str_dup(buf);  /* owned by the Type */
    Type *st = type_struct(mangled_name, field_count);

    register_struct_type(c, st->as.strukt.name, st);  /* cache BEFORE filling fields */

    /* 6. Fill fields: resolve each field's TypeNode with T→concrete substitution */
    bool has_drop = false;
    for (int i = 0; i < field_count; i++) {
        TypeNode *ft = decl->as.struct_decl.field_types[i];
        Type *resolved = resolve_type_node_with_substitution(
                             c, ft, tp_names, type_args, type_arg_count);
        if (resolved == NULL) {
            checker_error(c, ft->line, ft->column,
                          "cannot resolve field type in generic struct '%s'", base_name);
            resolved = type_int(); /* fallback to prevent crash */
        }
        st->as.strukt.fields[i].name = decl->as.struct_decl.field_names[i];
        st->as.strukt.fields[i].type = resolved;

        /* 7. Propagate has_drop from field types */
        if (type_is_has_drop(resolved)) has_drop = true;
    }
    st->as.strukt.has_drop = has_drop;

    /* 8. If has_drop, auto-generate __drop (deferred to codegen via existing mechanism) */
    /* The codegen already handles has_drop structs via emit_auto_drop_fn.
       No extra work needed here. */

    return st;
}
```

#### 1.3.5 `resolve_type_node_with_substitution`（新增辅助函数）

这是将类型参数名（如 `"T"`）替换为具体类型的核心替换函数：

```c
/* Resolve a TypeNode to a Type, replacing any TYPE_NODE_NAMED whose name
   matches a type parameter with the corresponding concrete type argument.
   tp_names[i] corresponds to type_args[i]. */
static Type *resolve_type_node_with_substitution(
    Checker *c,
    TypeNode *node,
    char **tp_names, Type **type_args, int tp_count)
{
    if (node == NULL) return type_void();

    switch (node->kind) {
    case TYPE_NODE_PRIMITIVE:
        /* Primitives: int, f64, bool, string, etc. — no substitution */
        return resolve_type_node(c, node); /* existing function handles this */

    case TYPE_NODE_NAMED: {
        const char *name = node->as.named.name;

        /* Check if this is a type parameter name */
        for (int i = 0; i < tp_count; i++) {
            if (strcmp(name, tp_names[i]) == 0) {
                if (node->as.named.arg_count > 0) {
                    checker_error(c, node->line, node->column,
                        "type parameter '%s' cannot have type arguments", name);
                    return NULL;
                }
                return type_args[i]; /* Substitute */
            }
        }

        /* Not a type parameter: look up as normal struct/enum */
        if (node->as.named.arg_count == 0) {
            /* Plain named type: struct or enum */
            return resolve_type_node(c, node);
        }

        /* Generic named type with args: e.g. Option(T), vec(T), Pair(T,U) */
        /* Recursively resolve each arg with substitution */
        Type **resolved_args = NULL;
        if (node->as.named.arg_count > 0) {
            resolved_args = malloc_safe(
                (size_t)node->as.named.arg_count * sizeof(Type *));
            for (int i = 0; i < node->as.named.arg_count; i++) {
                resolved_args[i] = resolve_type_node_with_substitution(
                    c, node->as.named.args[i], tp_names, type_args, tp_count);
            }
        }

        /* Now instantiate the outer generic: Option, Result, or user struct */
        Type *result = NULL;
        const char *outer = node->as.named.name;
        int nargs = node->as.named.arg_count;

        /* Try builtin enum templates first (Option, Result) */
        int tmpl_idx = find_template_idx(c, outer);
        if (tmpl_idx >= 0) {
            result = instantiate_template(c, tmpl_idx,
                                          resolved_args, nargs,
                                          node->line, node->column);
        } else {
            /* Try user generic struct */
            result = checker_instantiate_struct(c, outer,
                                                resolved_args, nargs,
                                                node->line, node->column);
        }
        free(resolved_args);
        return result;
    }

    case TYPE_NODE_VECTOR: {
        /* vec(T) — substitute T */
        Type *elem = resolve_type_node_with_substitution(
            c, node->as.vec.elem, tp_names, type_args, tp_count);
        return type_vector(elem);
    }

    case TYPE_NODE_MAP: {
        /* map(K, V) — substitute K and V */
        Type *key = resolve_type_node_with_substitution(
            c, node->as.map.key, tp_names, type_args, tp_count);
        Type *val = resolve_type_node_with_substitution(
            c, node->as.map.val, tp_names, type_args, tp_count);
        return type_map(key, val);
    }

    case TYPE_NODE_ARRAY: {
        /* array(T, N) — substitute T, keep N */
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

    default:
        /* Fallback: delegate to existing resolve_type_node (no substitution) */
        return resolve_type_node(c, node);
    }
}
```

#### 1.3.6 `resolve_type_node` 中分发到泛型结构体实例化

**现有 `resolve_type_node`** 处理 `TYPE_NODE_NAMED` 时，在 `find_struct_type` / `find_enum_type` 之前，插入对泛型结构体的检测：

```c
case TYPE_NODE_NAMED: {
    const char *name = node->as.named.name;
    int nargs = node->as.named.arg_count;

    /* 1. Type alias (existing) */
    Type *alias = find_type_alias(c, name);
    if (alias && nargs == 0) return alias;

    /* 2. G1: Generic struct instantiation */
    if (nargs > 0) {
        /* Check if it's a user generic struct first */
        bool is_generic_struct = false;
        for (int i = 0; i < c->struct_template_count; i++) {
            if (strcmp(c->struct_templates[i].base_name, name) == 0) {
                is_generic_struct = true;
                break;
            }
        }
        if (is_generic_struct) {
            /* Resolve type args first */
            Type **targs = malloc_safe((size_t)nargs * sizeof(Type *));
            for (int i = 0; i < nargs; i++)
                targs[i] = resolve_type_node(c, node->as.named.args[i]);
            Type *result = checker_instantiate_struct(c, name,
                               targs, nargs, node->line, node->column);
            free(targs);
            return result;
        }
        /* Not a user generic struct: fall through to Option/Result template lookup */
    }

    /* 3. Existing: find_struct_type, find_enum_type, instantiate_template ... */
    /* ... unchanged ... */
}
```

#### 1.3.7 `has_drop` 推导辅助函数

需要一个判断类型是否 has_drop 的函数（供实例化时字段检测用）：

```c
/* Returns true if a type requires a destructor (owns heap memory). */
static bool type_is_has_drop(const Type *t) {
    if (t == NULL) return false;
    switch (t->kind) {
    case TYPE_STRING:  return true;
    case TYPE_VECTOR:  return true;
    case TYPE_MAP:     return true;
    case TYPE_BLOCK:   return true;  /* Phase F.2 */
    case TYPE_STRUCT:  return t->as.strukt.has_drop;
    case TYPE_ENUM:    return t->as.enom.has_drop;
    default:           return false;
    }
}
```

注意：`checker.c` 中已有 `type_owns_heap_for_enum` 函数（第 172 行附近），可以合并或复用。

---

### 1.4 `new_expr` 和变量声明的处理

#### 1.4.1 变量声明（`AST_VAR_DECL`）

用户写：
```ls
Pair(int, string) p = Pair(int, string) { first: 42, second: "hello" }
```

- 左侧 `Pair(int, string)` 是变量类型：`parse_type` 解析为 `TYPE_NODE_NAMED("Pair", [int, string])`
- `check_var_decl` 调用 `resolve_type_node` → 触发 `checker_instantiate_struct` → 返回 `Type("Pair(int,string)", ...)`
- 右侧 `Pair(int, string) { ... }` 是结构体字面量：`AST_NEW_EXPR`
- `check_new_expr` 查找 struct_types["Pair(int,string)"]（已由左侧实例化注册）→ 字段类型检查正常进行

**关键**：只要左侧声明触发了实例化并注册了 mangled name，右侧的 `AST_NEW_EXPR` 查找时就能命中缓存。

#### 1.4.2 无类型声明的实例化（`Stack(int) s`，无初始化器）

```ls
Stack(int) s
```

`check_var_decl` 解析 `Stack(int)` 类型时调用 `resolve_type_node` → 触发实例化。`s` 的 LLVM alloca 按 `"Stack(int)"` struct 类型分配，与普通 struct 声明完全相同路径。

---

### 1.5 Codegen 层：**零改动**

单态化后的 `Type(TYPE_STRUCT, name="Pair(int,string)")` 与普通 struct `Type(TYPE_STRUCT, name="Point")` 在 Codegen 视角完全相同：

- `struct_types` 缓存按 `name` 字符串查找（mangled name 即 key，无歧义）
- `type_to_llvm` 按字段遍历生成 LLVM struct type（不关心 name 语义）
- `emit_auto_drop_fn` 按字段类型生成 drop（不关心 struct name 格式）
- `AST_NEW_EXPR` 的 `struct_name` 就是 mangled name `"Pair(int,string)"`，直接查 `struct_types` 命中

**唯一需要确认**：`AST_NEW_EXPR` 的 `struct_name` 字段存的是什么。

当前 Parser 在解析 `Pair(int, string) { first: 42 }` 时（struct literal 语法），`struct_name` 取的是最外层的 struct 名字。需要确认：

- 若 `struct_name` 只存 `"Pair"`（不含类型参数），Codegen 查 `struct_types["Pair"]` 会找不到（因为实例化注册的是 `"Pair(int,string)"`）
- 修复方式：在 `check_new_expr` 中，若该 struct 是泛型（`find struct_template("Pair")` 命中），则将 `node->as.new_expr.struct_name` 改写为 mangled name（或在 `resolved_type` 中已有足够信息）

**推荐方案**：`check_new_expr` 检测到泛型后，将 `resolved_type` 设为实例化后的 Type；Codegen 的 `codegen_new_expr` 改为优先从 `node->resolved_type->as.strukt.name` 查找，而不是从 `node->as.new_expr.struct_name` 查找。这样 Codegen 完全不需要知道泛型存在。

---

### 1.6 Name Mangling 规则

| 情况 | Mangled Name | 示例 |
|------|-------------|------|
| 非泛型 struct | 原名 | `"Point"` |
| 1 个类型参数 | `"Name(T1)"` | `"Stack(int)"` |
| 2 个类型参数 | `"Name(T1,T2)"` | `"Pair(int,string)"` |
| 嵌套泛型 | `"Name(Outer(Inner))"` | `"Pair(Stack(int),string)"` |
| 泛型方法（G1.5） | `"Name(T1)__method"` | `"Stack(int)__push"` |
| 泛型函数（G2） | `"fn__T1__T2"` | `"identity__int__"` |

Mangling 规则：用 `type_name()` 的输出直接拼接，`type_name` 对所有类型都有确定性输出，无歧义。

---

## 2. Phase G1.5：泛型 impl 块

### 2.1 Parser 扩展（`parse_impl_decl`，第 1991 行）

**当前语法**：`impl StructName { methods }`  
**新增语法**：`impl(T, U) StructName(T, U) { methods }`

**修改逻辑**（在 `consume('impl')` 之后）：

```c
static AstNode *parse_impl_decl(Parser *p) {
    /* 'impl' already consumed */

    /* G1.5: optional type param list: impl(T, U) */
    char **type_params = NULL;
    int   type_param_count = 0;

    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        do {
            if (!check(p, TOKEN_IDENTIFIER) || !is_upper_ident(p->current)) {
                error_at_current(p, "expected uppercase type parameter name in impl");
                break;
            }
            advance(p);
            /* append p->previous to type_params[] */
        } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after impl type parameters");
    }

    /* struct name */
    if (!check(p, TOKEN_IDENTIFIER)) {
        error_at_current(p, "expected struct name after 'impl'");
        return NULL;
    }
    advance(p);
    char *name = str_dup_n(p->previous.start, p->previous.length);

    /* G1.5: optional type args on the struct: impl(T) Stack(T) — parse and discard
       (we only need the base name; the type args are validated in checker) */
    char **type_args = NULL;
    int   type_arg_count = 0;
    if (check(p, TOKEN_LPAREN)) {
        advance(p); /* consume '(' */
        do {
            if (!check(p, TOKEN_IDENTIFIER)) break;
            advance(p);
            /* append p->previous to type_args[] */
        } while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after struct type arguments in impl");
    }

    /* rest: consume '{' methods '}' — unchanged */
    /* ... */

    n->as.impl_decl.type_params      = type_params;
    n->as.impl_decl.type_param_count = type_param_count;
    n->as.impl_decl.type_args        = type_args;
    n->as.impl_decl.type_arg_count   = type_arg_count;
    /* ... */
}
```

### 2.2 Checker：泛型 impl 模板注册

`check_impl_decl` 入口处，检测是否为泛型 impl：

```c
static void check_impl_decl(Checker *c, AstNode *node) {
    const char *base_name = node->as.impl_decl.name;
    int tpc = node->as.impl_decl.type_param_count;

    if (tpc > 0) {
        /* Generic impl: bind to struct template, store for deferred instantiation */
        for (int i = 0; i < c->struct_template_count; i++) {
            if (strcmp(c->struct_templates[i].base_name, base_name) == 0) {
                if (c->struct_templates[i].impl_node != NULL) {
                    checker_error(c, node->line, node->column,
                                  "duplicate impl for generic struct '%s'", base_name);
                    return;
                }
                c->struct_templates[i].impl_node = node;
                return;
            }
        }
        checker_error(c, node->line, node->column,
                      "impl for unknown generic struct '%s'", base_name);
        return;
    }

    /* Non-generic impl: existing logic unchanged */
    /* ... */
}
```

### 2.3 Checker：联动实例化 impl 方法

在 `checker_instantiate_struct` 末尾，检查该模板是否有 impl，若有则实例化方法：

```c
/* Phase G1.5: instantiate associated impl methods */
if (c->struct_templates[tmpl_idx].impl_node != NULL) {
    instantiate_impl_methods(c, st, mangled_name,
                             c->struct_templates[tmpl_idx].impl_node,
                             tp_names, type_args, type_arg_count);
}
```

**`instantiate_impl_methods` 逻辑**：

```c
static void instantiate_impl_methods(Checker *c, Type *struct_type,
                                     const char *mangled_struct_name,
                                     AstNode *impl_node,
                                     char **tp_names, Type **type_args, int tp_count)
{
    /* Ensure impl_registry entry exists for mangled_struct_name */
    ensure_impl_entry(c, mangled_struct_name);

    int mc = impl_node->as.impl_decl.method_count;
    for (int i = 0; i < mc; i++) {
        AstNode *method = impl_node->as.impl_decl.methods[i];
        const char *mname = method->as.fn_decl.name;

        /* Build concrete method type: substitute T→concrete in param/return types */
        int pc = method->as.fn_decl.param_count;
        Type **param_types = malloc_safe((size_t)(pc + 1) * sizeof(Type *));

        /* First param is implicit 'self' = *struct_type (existing convention) */
        param_types[0] = type_pointer(struct_type);
        for (int j = 0; j < pc; j++) {
            param_types[j + 1] = resolve_type_node_with_substitution(
                c, method->as.fn_decl.param_types[j],
                tp_names, type_args, tp_count);
        }

        Type *ret_type = method->as.fn_decl.return_type
            ? resolve_type_node_with_substitution(
                c, method->as.fn_decl.return_type,
                tp_names, type_args, tp_count)
            : type_void();

        Type *method_type = type_function(param_types, pc + 1, ret_type, false);
        free(param_types);

        /* Register in impl_registry under mangled_struct_name */
        register_impl_method(c, mangled_struct_name, mname, method_type,
                             method->as.fn_decl.is_static,
                             method->as.fn_decl.self_borrow_kind);
    }
}
```

### 2.4 Codegen：泛型方法实例化

当用户调用 `s.push(10)`（`s: Stack(int)`）时：

1. Checker 的 `check_field_or_method` 查找 `impl_registry["Stack(int)"]["push"]` → 命中，返回具体函数类型
2. Codegen 的 `codegen_method_call` 查找函数名 `"Stack(int)__push"` → 首次调用时触发该函数的 codegen

**触发泛型方法的 codegen**：首次调用到某个具体化方法时，需要 emit 其 LLVM IR。方案：

- 在 `instantiate_impl_methods` 中，把方法的 `AstNode`（已做类型替换的语义副本）加入一个待 codegen 队列
- Codegen 的 `codegen_compile` 主循环（`AST_PROGRAM`）结束后，处理该队列

或者更简单（推荐）：

- 在 Codegen 的 `codegen_method_call` 中，检测到调用的是泛型方法（按 mangled struct name 判断），如果该方法的 LLVM 函数尚未 emit（`LLVMGetNamedFunction` 返回 NULL），则立即 emit 该方法
- emit 时，把方法的 `AstNode` 副本配合类型替换表一起传入 `codegen_fn_decl`

这需要在 Codegen 中存储从 mangled method name 到（方法 AstNode + 类型替换表）的映射。具体数据结构：

```c
/* In CodegenContext: */
struct {
    char   *mangled_name;    /* "Stack(int)__push" */
    AstNode *method_node;    /* original AST */
    char  **tp_names;        /* ["T"] */
    Type  **type_args;       /* [int] */
    int     tp_count;
} *pending_generic_methods;
int pending_generic_method_count;
int pending_generic_method_cap;
```

---

## 3. Phase G2：泛型函数

### 3.1 语法

```ls
fn identity(T)(T x) -> T {
    return x
}

fn swap(T)(T a, T b) -> Pair(T, T) {
    return Pair(T, T) { first: b, second: a }
}
```

类型参数列表 `(T)` 紧跟函数名，在普通参数列表之前。

### 3.2 AST 扩展

`fn_decl` 节点已在 Phase G1 中预留了 `type_params` / `type_param_count`。Parser 修改：

`parse_fn_decl`（第 1759 行附近）在解析完函数名后，若遇到 `(` 且内容是大写标识符列表（与 struct 相同的判断），解析为类型参数：

```c
/* G2: optional type params: fn name(T, U)(...) */
if (check(p, TOKEN_LPAREN)) {
    advance(p);
    if (is_upper_ident(p->current)) {
        /* Parse type param list */
        do { /* ... append to type_params[] ... */ }
        while (match_tok(p, TOKEN_COMMA));
        consume(p, TOKEN_RPAREN, "expected ')' after type params");
        /* Next '(' is the regular param list */
    } else {
        /* Not type params — this IS the regular param list, process as before */
        /* (need to unget the '(' or restructure the parse) */
    }
}
```

**注意**：函数名后 `(` 的二义性与 struct 相同，同样用首字母大写判断。

### 3.3 Checker：泛型函数注册与调用点实例化

类似 struct templates，增加 `fn_templates` 注册表：

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

调用点 `identity(int)(42)` 的类型检查：
1. 检测 callee 是泛型函数（`find_fn_template("identity")` 命中）
2. 解析显式类型参数 `(int)` → `[Type(int)]`
3. Mangled name：`"identity__int__"`
4. 缓存检查，未命中则实例化：创建具体函数的类型，注册到 `scope`
5. 替换调用的 callee 为 mangled name

**类型推断**（`identity(42)` 不写 `(int)`）：
- 从第一个实参类型推断 T：arg 类型 = `int` → T = `int`
- 仅支持单一约束推断（Phase G2 v1 限制）

### 3.4 Codegen：泛型函数与 struct 方法类似

首次遇到具体调用时 emit，与泛型方法相同机制。

---

## 4. 关键限制与边界情况

### 4.1 Phase G1 / G1.5 限制（v1）

| 限制 | 说明 | 未来版本解除 |
|------|------|-------------|
| 每个泛型 struct 只能有一个 impl 块 | 不支持多个 `impl(T) Stack(T)` | G2 扩展 |
| 泛型 struct 不能继承/实现 trait | 无 trait 系统 | Phase G3 |
| 泛型参数不能有约束 | `(T: orderable)` 不支持 | Phase G3 |
| impl 方法不能引入新的类型参数 | 方法的 T 只能来自 struct 的 T | Phase G2 |
| 不支持裸 `T` 作为返回类型（函数） | 可通过包裹 `Pair(T,int)` 返回 | G2 |
| 泛型嵌套深度无限制（但 mangled name 可能很长） | Stack(Stack(int)) 合法 | 永久 |

### 4.2 自递归泛型

```ls
struct Tree(T) {
    T value
    Option(Tree(T)) left
    Option(Tree(T)) right
}
```

实例化 `Tree(int)` 时：
1. `checker_instantiate_struct("Tree", [int])` 开始执行
2. **先注册空壳** `Type("Tree(int)", field_count=3)` 到缓存（步骤 5 的预注册）
3. 解析字段 `Option(Tree(T))`：替换 T→int → `Option(Tree(int))`
4. `resolve_type_node_with_substitution` 处理 `Option(Tree(int))` 时，`checker_instantiate_struct("Tree", [int])` 再次被调用 → 命中缓存，返回已有的空壳
5. `instantiate_template("Option", [Tree(int)])` 正常处理
6. 字段全部填完后，空壳变为完整类型

**与自递归 enum 处理一致**（`instantiate_template` 第 263 行已有此模式）。

### 4.3 has_drop 传播矩阵

| 字段类型 | has_drop 贡献 |
|---------|--------------|
| `int`, `f64`, `bool`, `*T` | 无 |
| `string` | ✅ |
| `vec(T)`（任意 T） | ✅ |
| `map(K, V)` | ✅ |
| `Block(...)` | ✅ |
| `struct(has_drop=true)` | ✅ |
| `enum(has_drop=true)` | ✅ |
| `array(T, N)`（T 无 drop） | 无 |

`Pair(int, int)` → has_drop=false → 无 `__drop`，退出作用域时直接丢弃  
`Pair(string, int)` → has_drop=true → 自动合成 `__drop`，释放 first 字段

---

## 5. 测试矩阵

### 5.1 Phase G1 基础测试（`tests/samples/generics_g1_test.ls`）

```ls
struct Pair(T, U) {
    T first
    U second
}

fn main() {
    // G1.1: 基本实例化，POD 字段
    Pair(int, int) p1 = Pair(int, int) { first: 1, second: 2 }
    print(p1.first)   // 1
    print(p1.second)  // 2

    // G1.2: string 字段（has_drop=true）
    Pair(string, int) p2 = Pair(string, int) { first: "hello", second: 42 }
    print(p2.first)   // hello
    print(p2.second)  // 42

    // G1.3: 同一泛型，不同参数（两个独立实例）
    Pair(int, string) p3 = Pair(int, string) { first: 99, second: "world" }
    print(p3.first)   // 99

    // G1.4: 嵌套泛型
    Pair(Pair(int, int), string) p4 = Pair(Pair(int, int), string) {
        first: p1,
        second: "nested"
    }
    print(p4.first.first)  // 1
    print(p4.second)       // nested

    // G1.5: vec(T) 字段（has_drop=true）
    // (需要 G1.5 impl 才能用 push，这里只测构造)
}
```

验收：AOT + JIT + memcheck 0 leak / 0 dfree

### 5.2 Phase G1.5 impl 测试（`tests/samples/generics_g15_test.ls`）

```ls
struct Stack(T) {
    vec(T) items
}

impl(T) Stack(T) {
    fn push(T item) {
        items.push(item)
    }
    fn pop() -> Option(T) {
        if items.length == 0 { return None }
        return Some(items.pop())
    }
    fn peek() -> Option(T) {
        if items.length == 0 { return None }
        return Some(items[items.length - 1])
    }
    fn len() -> int {
        return items.length
    }
    fn is_empty() -> bool {
        return items.length == 0
    }
}

fn main() {
    // G1.5.1: int stack
    Stack(int) si
    si.push(10)
    si.push(20)
    si.push(30)
    print(si.len())    // 3
    print(si.pop())    // Some(30)
    print(si.len())    // 2
    print(si.peek())   // Some(20)

    // G1.5.2: string stack（has_drop=true，测 RAII）
    Stack(string) ss
    ss.push("alpha")
    ss.push("beta")
    print(ss.pop())    // Some(beta)
    print(ss.is_empty()) // false

    // G1.5.3: 作用域自动 drop（退出时 ss.items 自动释放）
}
```

验收：AOT + JIT + memcheck 0 leak / 0 dfree

### 5.3 Phase G2 泛型函数测试（`tests/samples/generics_g2_test.ls`）

```ls
fn identity(T)(T x) -> T {
    return x
}

fn make_pair(T, U)(T a, U b) -> Pair(T, U) {
    return Pair(T, U) { first: a, second: b }
}

fn main() {
    // G2.1: 显式类型参数
    print(identity(int)(42))        // 42
    print(identity(string)("hi"))   // hi

    // G2.2: 类型推断
    print(identity(99))             // 99
    print(identity("world"))        // world

    // G2.3: 多类型参数
    Pair(int, string) p = make_pair(int, string)(7, "seven")
    print(p.first)   // 7
    print(p.second)  // seven
}
```

---

## 6. 文件改动汇总

| 文件 | 改动类型 | 主要内容 |
|------|----------|---------|
| `src/ast.h` | 修改 | `struct_decl` / `impl_decl` / `fn_decl` 加 `type_params` 字段 |
| `src/ast.c` | 修改 | `ast_free` 对应新字段的释放 |
| `src/checker.h` | 修改 | `Checker` 加 `struct_templates` / `fn_templates` 字段；公开 `checker_instantiate_struct` |
| `src/checker.c` | 修改（主要） | `register_struct_template`、`checker_instantiate_struct`、`resolve_type_node_with_substitution`、`instantiate_impl_methods`；修改 `check_struct_decl`、`check_impl_decl`、`resolve_type_node` |
| `src/parser.c` | 修改 | `parse_struct_decl`、`parse_impl_decl`、`parse_fn_decl` 加类型参数解析；`is_upper_ident` 辅助函数 |
| `src/codegen.c` | 微改 | `codegen_new_expr` 改从 `resolved_type->name` 查 struct；`CodegenContext` 加 `pending_generic_methods` 队列（G1.5） |
| `tests/` | 新增 | `generics_g1_test.ls`、`generics_g15_test.ls`、`generics_g2_test.ls`；对应 `.cmake` 测试文件 |

---

## 7. 实现顺序建议

```
G1-a: ast.h struct_decl 加 type_params（30 分钟）
G1-b: ast.c ast_free 对应释放（15 分钟）
G1-c: parser.c parse_struct_decl 加类型参数解析（45 分钟）
G1-d: checker.h Checker 加 struct_templates（20 分钟）
G1-e: checker.c register_struct_template（30 分钟）
G1-f: checker.c resolve_type_node_with_substitution（核心，2 小时）
G1-g: checker.c checker_instantiate_struct（核心，1.5 小时）
G1-h: checker.c resolve_type_node 分发逻辑（30 分钟）
G1-i: codegen.c new_expr 从 resolved_type 查 struct name（20 分钟）
G1-j: 测试 G1（1 小时）

G1.5-a: ast.h impl_decl 加 type_params/type_args（20 分钟）
G1.5-b: parser.c parse_impl_decl 加类型参数解析（30 分钟）
G1.5-c: checker.c check_impl_decl 泛型注册（30 分钟）
G1.5-d: checker.c instantiate_impl_methods（核心，2 小时）
G1.5-e: codegen.c pending_generic_methods 队列 + emit（2 小时）
G1.5-f: 测试 G1.5（1 小时）

G2-a: ast.h fn_decl 加 type_params（已在 G1-a 预留）
G2-b: parser.c parse_fn_decl 加类型参数解析（30 分钟）
G2-c: checker.c fn_templates + 调用点实例化（2 小时）
G2-d: 测试 G2（1 小时）
```

**最关键的两个函数**，写对了整个系统就通了：
1. `resolve_type_node_with_substitution` — 递归类型替换
2. `checker_instantiate_struct` — 单态化 + 缓存 + has_drop 推导
