# LS 语言特性：Struct 堆分配（`new` / `free`）

> **用途**：将此章节追加到 CLAUDE.md 的「7. 计划新增的语言特性」中，作为下一个开发迭代。

---

## 概述

为 struct 类型增加堆分配能力，使用 `new` 关键字分配、`free()` 释放、`nil` 表示空指针。
遵循 LS 现有设计哲学：**手动内存管理，无 GC，编译器不插入隐式 free**。

## 语法设计

```
// 栈分配（现有行为，不变）
Point p
p.x = 1.0
p.y = 2.0

// 堆分配 — 带字段初始化器
*Point p = new Point { x: 1.0, y: 2.0 }

// 堆分配 — 零初始化（所有字段为 0 / 0.0 / false / nil）
*Point p = new Point

// 空指针
*Point p = nil

// 空指针检查
if (p == nil) { print("null pointer") }
if (p != nil) { print(p.x) }

// 手动释放
free(p)

// 方法调用 — 指针和值类型统一语法（现有行为，不变）
f64 d = p.distance(p2)    // p 是 *Point，自动解引用
```

### 字段初始化器语法细节

```
new_expr := "new" IDENTIFIER                          // 零初始化
          | "new" IDENTIFIER "{" field_init_list "}"   // 命名字段初始化

field_init_list := field_init ("," field_init)* ","?   // 允许尾随逗号
field_init      := IDENTIFIER ":" expr
```

- 字段初始化器中**必须使用字段名**（不支持位置初始化）
- 未列出的字段自动零初始化
- 字段名重复报错
- 字段名不存在报错
- 字段类型不匹配报错

## 实现计划

### 1. Scanner 改动

**文件**：`token.h`, `scanner.c`

新增 Token：
```c
TOKEN_NEW,    // new
TOKEN_FREE,   // free（如果尚未作为内建函数存在）
```

- `new` 作为保留关键字加入关键字表
- `nil` 已存在（`TOKEN_NIL`），无需改动
- `free` 已作为内建函数存在（通过 C runtime `free` 声明），无需新增 token

### 2. AST 改动

**文件**：`ast.h`, `ast.c`

新增 AST 节点类型：
```c
AST_NEW_EXPR,       // new StructName { field: val, ... }
```

节点数据结构：
```c
// AST_NEW_EXPR
struct {
    char *struct_name;                   // 目标 struct 名称
    struct {
        char *name;                      // 字段名
        AstNode *value;                  // 字段初始化表达式
    } *field_inits;
    int field_init_count;                // 0 = 零初始化（new Point）
} new_expr;
```

`ast.c` 中新增：
- `ast_new_expr_create(name, fields, count)` 构造函数
- `ast_free` 中递归释放 `field_inits` 数组及其 value 节点

### 3. Parser 改动

**文件**：`parser.c`

在 **prefix 解析表** 中为 `TOKEN_NEW` 注册 prefix 解析函数 `parse_new_expr`：

```
parse_new_expr:
    1. 消费 TOKEN_NEW
    2. 期望 TOKEN_IDENTIFIER（struct 名称）
    3. 如果下一个 token 是 TOKEN_LBRACE：
       a. 消费 '{'
       b. 循环解析 field_init：
          - 期望 TOKEN_IDENTIFIER（字段名）
          - 期望 TOKEN_COLON
          - 解析 expr（字段值）
          - 如果下一个 token 是 TOKEN_COMMA，消费之
       c. 期望 TOKEN_RBRACE
       d. 构造 AST_NEW_EXPR（带字段初始化）
    4. 否则：
       构造 AST_NEW_EXPR（field_init_count = 0，零初始化）
```

`nil` 赋值给 `*T` 已有支持（`AST_NIL_LIT`），无需改动。

### 4. 类型检查改动

**文件**：`checker.c`

新增 `check_new_expr(checker, node)` 函数：

```
check_new_expr:
    1. 在类型注册表中查找 struct_name → 获取 Type* (TYPE_STRUCT)
       - 未找到：报错 "unknown struct type 'X'"
    2. 结果类型 = TYPE_POINTER { pointer_to: TYPE_STRUCT }
    3. 如果 field_init_count > 0：
       a. 遍历每个 field_init：
          - 在 struct 字段列表中查找 field_init.name
          - 未找到：报错 "struct 'X' has no field 'Y'"
          - 重复字段名：报错 "duplicate field initializer 'Y'"
          - 对 field_init.value 递归类型检查
          - 检查 value 的 resolved_type 与字段类型一致
       b. 未在初始化器中列出的字段：允许（零初始化），不报错
    4. 设置 node->resolved_type = pointer-to-struct
```

额外的类型检查规则：

- **`nil` 赋值**：`*T p = nil` — 已有支持（`nil` 的 resolved_type 可赋给任何 `TYPE_POINTER`）
- **`nil` 比较**：`p == nil` / `p != nil` — 已有支持（pointer 与 nil 的 `==`/`!=`）
- **`free()` 调用**：`free(p)` — 已作为 C runtime 函数声明存在，参数类型检查为 `*T` → `void*` 的隐式转换（通过 `TYPE_OBJECT` 或直接允许 `TYPE_POINTER` → `void*`）

### 5. Codegen 改动

**文件**：`codegen.c`

#### 5.1 new 表达式代码生成

新增 `codegen_new_expr(ctx, node)` 函数：

```
codegen_new_expr:
    1. 查找 struct 对应的 LLVM type：
       LLVMTypeRef struct_type = 已注册的 LLVM struct type

    2. 计算 struct 大小：
       LLVMValueRef size = LLVMSizeOf(struct_type)

    3. 调用 malloc：
       LLVMValueRef raw = LLVMBuildCall2(ctx->builder, malloc_fn_type,
                                          malloc_fn, &size, 1, "new_ptr")

    4. 位转换为正确的指针类型：
       LLVMValueRef typed_ptr = LLVMBuildBitCast(ctx->builder, raw,
                                                  LLVMPointerType(struct_type, 0),
                                                  "typed_ptr")
       // 注意：如果使用 opaque pointer（LLVM 15+），此步可省略

    5. 零初始化整个 struct：
       LLVMValueRef zero = LLVMConstNull(struct_type)
       LLVMBuildStore(ctx->builder, zero, typed_ptr)

    6. 如果有字段初始化器（field_init_count > 0）：
       对每个 field_init：
         a. 找到字段在 struct 中的索引 field_idx
         b. 生成字段值的 IR：
            LLVMValueRef val = codegen_expr(ctx, field_init.value)
         c. 生成 GEP + Store：
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder,
                struct_type, typed_ptr, field_idx, "field_ptr")
            LLVMBuildStore(ctx->builder, val, field_ptr)

    7. 返回 typed_ptr
```

#### 5.2 nil 字面量

`nil` 赋给 `*T` 时生成 `LLVMConstNull(LLVMPointerType(T, 0))`。
（如已有此逻辑，无需改动。）

#### 5.3 free() 调用

`free(p)` 已作为 C runtime 函数调用存在（`declare_builtins` 中声明了 `free`）。

codegen 中 `AST_CALL` 对 `free` 的处理：
- 参数是 `*T`（指向 struct 的指针），需要 bitcast 为 `i8*`（或 opaque pointer）传给 C `free`
- 如已有此隐式转换逻辑，无需改动

#### 5.4 指针字段访问（现有逻辑确认）

```ls
*Point p = new Point { x: 1.0, y: 2.0 }
print(p.x)    // p 是 *Point，需要自动解引用
p.y = 3.0     // 通过指针写入字段
```

`AST_FIELD` 和 `AST_ASSIGN` 已支持 `*Struct → Struct` 自动解引用（impl 隐式 self 阶段实现），
此处无需额外改动。确认以下行为正确：
- `p.x` → `LLVMBuildLoad(GEP(load(p_alloca), 0, field_idx))`
- `p.x = val` → `LLVMBuildStore(val, GEP(load(p_alloca), 0, field_idx))`

#### 5.5 nil 比较

```ls
if (p == nil) { ... }
```

对 `*T == nil` 生成：
```
LLVMBuildICmp(LLVMIntEQ, ptr_val, LLVMConstNull(ptr_type), "is_nil")
```

如已有 pointer 与 nil 比较的 codegen 逻辑（object 类型阶段实现），确认同样适用于 `*Struct`。

### 6. 方法调用兼容性

堆分配的 struct 指针调用 impl 方法时，现有逻辑已能正确处理：

```ls
struct Point { f64 x; f64 y }
impl Point {
    fn length() -> f64 {
        sqrt(self.x * self.x + self.y * self.y)
    }
    static fn origin() -> *Point {
        return new Point { x: 0.0, y: 0.0 }
    }
}

*Point p = new Point { x: 3.0, y: 4.0 }
f64 len = p.length()       // 自动传 p 作为 self（已经是 *Point）
*Point o = Point.origin()   // 静态方法返回堆分配的指针
free(o)
free(p)
```

与栈分配的区别：
- 栈分配 `Point p` 调用方法时，codegen 取 `&p`（alloca 地址）作为 self
- 堆分配 `*Point p` 调用方法时，codegen 取 `load(p_alloca)`（指针值本身）作为 self
- 现有 impl 的 codegen 需要确认能正确区分这两种情况

**确认要点**：在 `AST_CALL` 的方法调用 codegen 中，当 receiver 类型已经是 `*Struct`（而非 `Struct`），
应该直接用 receiver 的值作为 self 参数，**不再取地址**。
如果现有实现总是对 receiver 做 `&obj`，则需要加一个判断：
```c
if (receiver_type->kind == TYPE_POINTER &&
    receiver_type->pointer_to->kind == TYPE_STRUCT) {
    // receiver 已经是指针，直接用
    self_arg = codegen_expr(ctx, receiver);
} else {
    // receiver 是值类型，取地址
    self_arg = get_alloca_address(ctx, receiver);
}
```

## 测试计划

### 7.1 Parser 测试（test_parser.c）

新增 3 个用例：

1. **new 零初始化**：`*Point p = new Point` → 验证 AST_NEW_EXPR，field_init_count == 0
2. **new 带字段**：`*Point p = new Point { x: 1.0, y: 2.0 }` → 验证字段名和表达式
3. **new 带尾随逗号**：`*Point p = new Point { x: 1.0, }` → 允许尾随逗号

### 7.2 类型检查测试（test_types.c）

新增 6 个用例：

1. **正确：new 已知 struct** → 通过，resolved_type 为 `*Point`
2. **正确：new 部分字段初始化** → 通过，未列出的字段零初始化
3. **错误：new 未知类型** → `"unknown struct type 'Foo'"`
4. **错误：new 字段名不存在** → `"struct 'Point' has no field 'z'"`
5. **错误：new 字段类型不匹配** → `"expected 'f64', got 'string'"`
6. **错误：new 字段名重复** → `"duplicate field initializer 'x'"`

### 7.3 Codegen 测试（test_codegen.c）

新增 5 个用例：

1. **new 零初始化 + 字段读写**：验证 malloc + 零值 + GEP store/load
2. **new 带字段初始化器**：验证 malloc + 字段 store
3. **nil 赋值和比较**：验证 null 常量 + icmp
4. **free 调用**：验证 bitcast + free 调用
5. **方法调用**：验证 `*Point` 上调用 impl 方法（self 传指针而非取地址）

### 7.4 端到端测试（samples/new_test.ls）

```ls
struct Point {
    f64 x;
    f64 y;
}

impl Point {
    fn length() -> f64 {
        sqrt(self.x * self.x + self.y * self.y)
    }

    fn to_string() -> string {
        return f"({self.x}, {self.y})"
    }

    static fn create(f64 x, f64 y) -> *Point {
        return new Point { x: x, y: y }
    }
}

// 1. 基本堆分配 + 字段初始化
*Point p = new Point { x: 3.0, y: 4.0 }
print(p.x, p.y)                 // 期望：3.000000 4.000000

// 2. 零初始化
*Point p2 = new Point
print(p2.x, p2.y)               // 期望：0.000000 0.000000

// 3. 字段写入
p2.x = 10.0
p2.y = 20.0
print(p2.x, p2.y)               // 期望：10.000000 20.000000

// 4. 方法调用
f64 len = p.length()
print(len)                       // 期望：5.000000

// 5. nil 赋值和检查
*Point p3 = nil
if (p3 == nil) {
    print("p3 is nil")           // 期望：p3 is nil
}

// 6. 赋值后非 nil
p3 = new Point { x: 1.0, y: 1.0 }
if (p3 != nil) {
    print("p3 is not nil")       // 期望：p3 is not nil
}

// 7. 静态方法返回堆指针
*Point p4 = Point.create(5.0, 12.0)
print(p4.length())               // 期望：13.000000

// 8. 释放
free(p)
free(p2)
free(p3)
free(p4)
print("done")                    // 期望：done
```

期望输出：
```
3.000000 4.000000
0.000000 0.000000
10.000000 20.000000
5.000000
p3 is nil
p3 is not nil
13.000000
done
```

### 7.5 嵌套 struct 测试（samples/new_nested_test.ls）

```ls
struct Color {
    int r;
    int g;
    int b;
}

struct Pixel {
    f64 x;
    f64 y;
    Color color;     // 嵌套值类型 struct
}

// 堆分配嵌套 struct
Color red = Color
red.r = 255
red.g = 0
red.b = 0

*Pixel px = new Pixel { x: 10.0, y: 20.0, color: red }
print(px.x, px.y)                    // 期望：10.000000 20.000000
print(px.color.r, px.color.g, px.color.b)  // 期望：255 0 0

free(px)
print("done")                        // 期望：done
```

## 涉及文件汇总

| 文件 | 改动类型 | 说明 |
|------|----------|------|
| `token.h` | 新增 | `TOKEN_NEW` |
| `scanner.c` | 新增 | `new` 关键字识别 |
| `ast.h` | 新增 | `AST_NEW_EXPR` 节点定义 |
| `ast.c` | 新增 | `ast_new_expr_create` + `ast_free` 分支 |
| `parser.c` | 新增 | `parse_new_expr` 前缀解析函数 |
| `checker.c` | 新增 | `check_new_expr` 类型检查函数 |
| `codegen.c` | 新增 | `codegen_new_expr` + 方法调用 receiver 判断修正 |
| `test_parser.c` | 新增 | 3 个用例 |
| `test_types.c` | 新增 | 6 个用例 |
| `test_codegen.c` | 新增 | 5 个用例 |
| `samples/new_test.ls` | 新增 | 端到端测试 |
| `samples/new_nested_test.ls` | 新增 | 嵌套 struct 测试 |

## 不在此迭代范围内的事项

- **自动 free（RAII）**：本次不为 `*Struct` 添加作用域自动释放，保持纯手动管理。未来可以作为独立特性增加（类似 string 的 `emit_scope_cleanup` 泛化）
- **构造函数 / `init` 方法**：暂不支持，用 `static fn` 工厂方法替代
- **析构函数 / `drop` 方法**：暂不支持，未来配合 RAII 一起考虑
- **指针算术**：`p + 1` 等不支持（LS 不是 C），只支持字段访问和方法调用
- **`new` 数组**：`new array(int, N)` 动态数组堆分配是独立特性，不在本次范围
- **string 重构为 struct**：将 string 从内置类型改为 `impl __LsString` 是后续独立任务
