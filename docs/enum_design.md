# LS Enum 设计文档（Phase 8）

## 概述

LS 的 `enum` 是代数数据类型（ADT / tagged union），含无 payload / 带 payload / 自递归三种变体形态，搭配 `match` 强制穷尽性检查。内建 `Option(T)` 与 `Result(T,E)` 通过同一套 enum 机制按需单态化实现。

设计参考 Rust，但**类型参数语法采用括号** `Option(T)` 而非尖括号 `Option<T>`，以与现有 `vec(T)` / `map(K,V)` 一致。

## 语法

### 声明

```ls
enum Color {
    Red
    Green
    Blue
    RGB(int r, int g, int b)
}
```

- 体内分隔符 `;` / `,` / 换行任一可省（与 struct 字段一致）
- 变体名首字母大写（与 match binder 消歧）
- payload 字段格式 `类型 名称`（与 fn / struct 字段一致），名称当前主要用于文档
- 同一 enum 内变体名唯一

### 构造

```ls
Color a = Red
Color b = RGB(255, 128, 0)
```

无 payload 变体作为 IDENT；带 payload 变体作为 CALL。Checker 在 `AST_IDENT` / `AST_CALL` 早路径识别变体名 → 旁路普通函数解析直接产生 TYPE_ENUM。

### Pattern Matching

```ls
match c {
    Red => 1
    Green => 2
    Blue => 3
    RGB(r, g, b) => r + g + b
}
```

- **穷尽性强制**：未覆盖所有 variant 且无 `_` 通配则编译错，错误信息列出缺失 variant
- binder（`r`, `g`, `b`）按位置绑定，类型 = payload 声明类型
- binder 当前为只读借用语义（enum 仍持有 payload 所有权，binder 出 scope 不释放）

### 自递归

```ls
enum Tree {
    Leaf
    Node(int value, Tree left, Tree right)
}
```

编译器自动检测 `payload type == 自身`，生成 boxing 代码：构造时 malloc + store 指针，解构时 load 指针 + load 值。对用户透明。

## 内建 Option / Result

```ls
// 实际上等价于（编译器预注册的模板）：
// enum Option(T)    { None  Some(T value) }
// enum Result(T,E) { Ok(T value)  Err(E error) }

Option(int) o = Some(42)
Result(int, string) r = Err("oops")
```

按需单态化：首次使用 `Option(int)` 时实例化为 mangled name `"Option(int)"` 的具体 enum 类型并缓存；第二次使用直接命中缓存。`Option(int)` 与 `Option(string)` 是两个不同的具体类型。

构造时上下文驱动消歧：`Some(42)` 在多个 `Option(*)` 共存场景下，根据声明类型（var_decl 左侧 / fn 返回类型）确定属于哪个实例。

## 实现细节

### Type 表示（src/types.h）

```c
TYPE_ENUM:
struct {
    const char *name;        // mangled name e.g. "Option(int)"
    struct {
        const char *name;
        Type **payload_types;  // NULL if payload_count == 0
        int payload_count;
    } *variants;
    int variant_count;
    bool has_drop;           // any variant payload owns heap
    void *drop_fn;           // LLVMValueRef
} enom;
```

名义等价：按 mangled name 比较（`type_equals`）。

### LLVM 布局（src/codegen.c）

```
%EnumName = type { i8, [N x i8] }
                   ^    ^
                   disc payload (max variant size)
```

- discriminant 是 i8 字节，索引 0..N-1（>256 个 variant 编译错）
- payload 是定长字节数组，N = `LLVMABISizeOfType(variant struct)` 取 max
- 自递归字段在 payload 中存为 `i8*`（boxed pointer），使用时 load 后再 deref

### Constructor IR

```
1. alloca %EnumName
2. memset(alloca, 0, sizeof)
3. store disc to offset 0
4. bitcast payload bytes to variant struct, store each field
   - string payload: emit_string_clone_val (deep copy)
   - self-recursive: malloc + store value to box, store box pointer
5. load %EnumName, return value
```

### Match IR

```
1. alloca temp_subject
2. store subject to temp
3. load disc, switch on disc → case_bb per variant
4. each case_bb:
   a. bitcast payload bytes to variant struct
   b. GEP each field, load to alloca, register binder symbol (is_borrowed=true)
   c. codegen body, store result, br merge_bb
5. default_bb: unreachable (exhaustiveness enforced)
6. merge_bb: load result alloca
```

自递归 binder 特殊处理：先 `load i8* box`，再 `load %EnumName from box`。

### Drop Function IR

`emit_auto_enum_drop_fn` 为 has_drop enum 生成：

```
void EnumName.__drop(EnumName *self):
    disc = load self.disc
    switch disc:
        case Variant_with_string:
            field_ptr = bitcast payload + GEP
            emit_string_free(field_ptr)
            br end
        case Variant_with_self_recursive:
            box = load box_field
            EnumName.__drop(box)   // recursive
            free(box)
            br end
        ...
    end:
        ret void
```

scope cleanup 在 `emit_scope_cleanup` 检测到 has_drop enum 局部变量时调用此函数。

### 模板实例化（src/checker.c）

```c
struct {
    const char *base_name;       // "Option"
    int type_param_count;
    struct {
        const char *name;
        int payload_count;
        struct { int param_idx; Type *concrete; } *payload;
    } *variants;
    int variant_count;
} *enum_templates;
```

`instantiate_template(template_idx, type_args[])` 流程：
1. 构造 mangled name `"Base(arg1,arg2)"`
2. 缓存命中 → 返回
3. 替换 param_idx → 实际类型，构造 TYPE_ENUM
4. 注册到 `enum_types`，返回

### 上下文消歧（src/checker.c）

`Checker.expected_type` 字段在 `check_var_decl` 设置/恢复：

```c
Type *saved = c->expected_type;
c->expected_type = declared;
init_type = check_expr(c, init);
c->expected_type = saved;
```

`find_variant` 优先匹配 `expected_type` 中的 variant，避免 `Some(42)` 在多个 `Option(*)` 共存时报歧义错。

## 已知限制 / 未来工作

| 限制 | 计划处理 Phase |
|---|---|
| `?` 早返操作符未实现 | Phase 8.5 |
| `vec.get` / `map.get` 仍 panic（未改返 Option） | Phase 9（breaking change） |
| enum 含 vec/map payload 的 drop 未实现 | 按需扩展 |
| enum 复杂复用场景的 move 跟踪 | Phase B（与 struct move 路径对齐） |
| 用户自定义泛型 `<T>` | Phase 11；届时 Option/Result 模板可改写为通用泛型流水线 |

## 测试覆盖

- 单元：`tests/test_codegen.c` `test_enum_decl_layout` / `test_enum_ctor_and_match` / `test_enum_drop_fn` / `test_option_template_instantiation`
- 端到端：`tests/samples/enum_basic_test.ls` / `enum_payload_test.ls` / `enum_string_test.ls` / `enum_recursive_test.ls` / `option_result_test.ls`（AOT + JIT 双跑结果一致）
