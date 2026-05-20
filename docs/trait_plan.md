# LS Trait 系统实现计划

**日期**：2026-05-20
**前置依赖**：Phase G1/G1.5/G2（无约束泛型 struct + impl + fn）已完成
**目标**：为 LS 引入 trait 系统，解锁泛型约束，使泛型函数真正可用

---

## 0. 设计决策与风险分析

### 0.1 为什么需要 trait

当前泛型函数几乎无法使用——`fn max(T)(T a, T b) -> T` 内部不能对 T 做
任何操作（不能比较、不能打印、不能算术），因为编译器不知道 T 有哪些能力。

trait 提供**有界多态**：`fn max(T: Comparable)(T a, T b) -> T`，编译器可
在实例化前就检查 T 是否满足约束，泛型函数体内可以安全调用约束保证的方法。

### 0.2 核心设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 分派方式 | **纯静态分派（单态化）** | 与现有泛型一致；无 vtable / object 运行时开销；C + LLVM 后端友好 |
| 语法风格 | `trait Name { ... }` + `impl Name for Struct { ... }` | 接近 Rust，与现有 `impl Struct` 语法自然扩展 |
| 约束语法 | `fn f(T: Trait1 + Trait2)(T x)` | 简洁、无二义性（`+` 连接多约束） |
| 关联类型 | **v1 不做** | 巨大复杂度，先做最小集 |
| 默认方法 | **v1 不做** | 需要 self 类型推导 + 方法覆盖检查，留给 v2 |
| 动态分派 | **v1 不做** | 需要 vtable + trait object + 胖指针，独立大特性 |
| 继承 | **不做** | LS 无 OOP 继承设计 |
| impl 适用类型 | **v1 仅 struct** | 基本类型 impl 需扩展 checker + codegen，风险大，Phase T4 再加 |

### 0.3 与现有泛型的融合策略

**核心原则：trait 是泛型的"约束层"，不改变泛型的单态化执行模型。**

```
                    ┌──────────────┐
                    │  trait 定义   │  ← 新增
                    │  (接口声明)   │
                    └──────┬───────┘
                           │ constrains
                    ┌──────▼───────┐
  现有泛型流水线 ──→ │ fn f(T:Tr)() │  ← 约束检查（checker 新增）
                    └──────┬───────┘
                           │ instantiate (T=int)
                    ┌──────▼───────┐
                    │ fn f_int()   │  ← 单态化（复用现有管线）
                    └──────────────┘
```

融合点：
1. **checker 阶段**：实例化泛型时，检查具体类型是否满足所有 trait 约束
2. **codegen 阶段**：零改动——单态化后 T 已被替换为具体类型，trait 信息不进 IR
3. **AST/Parser**：扩展 fn_decl 和 struct_decl 的类型参数携带约束信息

### 0.4 回退策略

每个 Phase 独立可回退：
- **Phase T1（trait 定义）**：仅新增 AST 节点和 token，对现有代码零影响
- **Phase T2（impl Trait for Struct）**：仅扩展 checker 的 impl 注册表
- **Phase T3（约束检查）**：仅在泛型实例化路径加一个 if 检查
- **Phase T4（内建 trait）**：仅在 checker 初始化时预注册

**回退方式**：每个 Phase 的改动集中在 2-3 个函数内，git revert 单次提交即可。
不修改现有数据结构的语义，只在末尾追加新字段。

### 0.5 G1.5 失败教训的应用

1. **不合成 AST 节点**——trait impl 的方法直接复用 `impl Trait for Struct { fn ... }`
   的用户 AST，不克隆、不共享
2. **新增字段初始化为零**——所有新增的 AST/Checker 字段默认 NULL/0，老代码路径
   自然跳过（`if (trait_count > 0)` 守卫）
3. **每步独立 ctest**——不积累未测试的半成品

### 0.6 基本类型 impl 限制（v1 重要约束）

**v1 的 `impl Trait for X` 仅支持 X 为用户定义 struct。** 基本类型（int、
string、f64、bool）不能作为 impl 目标。

这意味着 v1 中以下代码**不合法**：

```ls
// ❌ v1 不支持
impl Comparable for int {
    fn compare(&self, &int other) -> int { return self - other }
}
fn main() {
    print(max(int)(3, 7))   // 编译错误：int 未实现 Comparable
}
```

**根因**：当前 `impl` 基础设施（checker 的 `find_struct_type` + codegen 的
`StructName.method_name` 命名）假设目标是 struct。让基本类型参与 impl 需要：

| 需要扩展的组件 | 具体改动 |
|---------------|----------|
| `checker.c` `find_struct_type` | 让 `int` 等返回一个 pseudo-struct Type，或走单独路径 |
| `checker.c` impl_registry | 接受非 struct 名字（`"int"`、`"string"`） |
| `codegen.c` 方法名 | `int.compare` 作为 LLVM 函数名 |
| `codegen.c` self 参数 ABI | struct 方法的 self 是指针（`&self`），int 的 `&self` 应该是 by-value？还是 pointer-to-int？与现有 `&int` 引用 ABI 对齐 |
| 内建方法冲突 | `string` 已有 `.compare` 内建方法，trait 版本同名时谁优先 |

**Phase T4 会解决这个问题**，分两步：
- Step 11：扩展 impl 基础设施支持基本类型
- Step 12（原 Step 11）：编译器预注册内建 trait impl

**v1 的实用价值**：即使只支持 struct，trait 约束已经很有用——用户自定义的
数据结构（Point、Color、Matrix 等）可以通过 trait 约束参与泛型算法。

---

## 1. 语法设计

### 1.1 trait 声明

```ls
trait Printable {
    fn to_string(&self) -> string
}

trait Comparable {
    fn compare(&self, &Self other) -> int    // &Self = 与自身同类型的引用
}

trait Numeric {
    fn zero() -> Self                        // 静态方法，返回 Self 类型
    fn add(&self, &Self other) -> Self
}
```

关键语法元素：
- `trait Name { ... }` 声明
- 方法签名仅有声明、无 body（v1 不支持默认方法）
- `&self` / `&!self` 遵循现有借用语义
- `Self` 关键字：指代实现该 trait 的具体类型

### 1.2 trait 实现

**v1 仅支持为 struct 实现 trait**（不支持 int/string 等基本类型，见 §0.6）。

```ls
struct Point { int x; int y }

impl Printable for Point {
    fn to_string(&self) -> string {
        return f"({self.x}, {self.y})"
    }
}

impl Comparable for Point {
    fn compare(&self, &Point other) -> int {
        int dx = self.x - other.x
        if dx != 0 { return dx }
        return self.y - other.y
    }
}

// ❌ v1 不支持（Phase T4 再加）：
// impl Comparable for int { ... }
// impl Comparable for string { ... }
```

关键：
- `impl TraitName for StructName { fn ... }` 语法
- 方法签名必须与 trait 声明匹配（参数数量、类型、返回类型）
- `Self` 在 body 中被替换为 `StructName`
- **v1 限制**：`StructName` 必须是用户定义的 struct

### 1.3 约束泛型

```ls
fn max(T: Comparable)(T a, T b) -> T {
    if a.compare(&b) > 0 {
        return a
    }
    return b
}

// 多约束
fn print_max(T: Comparable + Printable)(T a, T b) {
    T m = max(T)(a, b)
    print(m.to_string())
}
```

### 1.4 泛型 struct 约束

```ls
struct SortedPair(T: Comparable) {
    T first
    T second
}
```

---

## 2. 实现步骤总览

```
Phase T1: trait 声明（parse + check，不影响 codegen）
  Step 1: Token + AST 扩展                              ← 编译通过，零行为变化
  Step 2: Parser 解析 trait Name { fn ... }               ← parse-only 测试
  Step 3: Checker 注册 trait（trait_registry）             ← 类型检查通过

Phase T2: impl Trait for Struct（方法注册 + 签名验证）
  Step 4: Parser 解析 impl Trait for Struct { ... }       ← parse-only
  Step 5: Checker 验证 + 注册 trait impl                  ← 签名匹配检查
  Step 6: Codegen 适配 trait 方法名                       ← 端到端调用

Phase T3: 约束泛型（trait bound）
  Step 7: Parser 解析 fn f(T: Trait)(...)                 ← parse-only
  Step 8: Checker 约束检查（实例化时验证）                 ← 编译错误测试
  Step 9: 端到端集成测试                                  ← ctest 全过

Phase T4: Self 关键字 + 基本类型 impl 扩展
  Step 10: Self 关键字支持                                ← 端到端
  Step 11: 扩展 impl 支持基本类型 (int/string/f64/bool)    ← 基础设施
  Step 12: 编译器预注册内建 trait impl                     ← int/string 自带

Phase T5: 泛型 struct 约束
  Step 13: struct Foo(T: Trait) 约束检查                  ← 端到端
```

**每步改动不超过 3 个文件，每步有独立测试，每步可单独 revert。**

---

## 3. Phase T1: trait 声明

### Step 1: Token + AST 扩展

**目的**：让编译器能识别 `trait` 关键字和 AST 节点，不影响任何现有行为。

**文件**：`token.h`、`ast.h`、`ast.c`

token.h:
```c
TOKEN_TRAIT,            /* trait */
TOKEN_FOR,              /* for (impl Trait for Struct) */
```

ast.h:
```c
AST_TRAIT_DECL,         /* trait Name { fn ...; fn ...; } */
AST_IMPL_TRAIT_DECL,    /* impl Trait for Struct { fn ... } */

// AST_TRAIT_DECL 数据
struct {
    char *name;
    AstNode **method_sigs;     // fn 签名（无 body）
    int method_sig_count;
} trait_decl;

// AST_IMPL_TRAIT_DECL 数据
struct {
    char *trait_name;
    char *struct_name;
    AstNode **methods;         // fn 实现（有 body）
    int method_count;
} impl_trait_decl;
```

**回退风险**：极低。新增枚举值和 union 分支，不触碰任何现有 case。
`ast_free` 新增 case 处理释放；老路径不会走到这些 case。

**验收**：编译通过，ctest 全过（零行为变化）。

### Step 2: Parser 解析 trait 声明

**目的**：解析 `trait Name { fn sig1(); fn sig2(); }` 语法。

**文件**：`scanner.c`（关键字注册）、`parser.c`

scanner.c:
- 在 `check_keyword` 的 binary search 表中添加 `"trait"` → `TOKEN_TRAIT`
- 添加 `"for"` → `TOKEN_FOR`

parser.c:
```
parse_trait_decl():
  consume TOKEN_TRAIT
  consume TOKEN_IDENTIFIER → name
  consume TOKEN_LBRACE
  while !TOKEN_RBRACE:
    parse method signature (fn name(params) -> ret，无 body)
  consume TOKEN_RBRACE
  return AST_TRAIT_DECL node
```

方法签名解析：复用 `parse_fn_decl` 的前半部分（到 `->` 返回类型为止），
遇到 `{` 前停止（或要求 `;` / 换行分隔）。需要一个 `is_signature_only` 标志
或单独的 `parse_fn_signature` 函数。

**关键细节**：`Self` 暂时作为普通 TOKEN_IDENTIFIER 处理（Step 10 再特化）。

**回退风险**：低。新增 `parse_trait_decl` 函数 + `case TOKEN_TRAIT` 入口。
移除一个函数 + 一个 case 即可回退。

**验收**：`trait_parse_test.ls` 仅声明 trait，编译通过无错。

### Step 3: Checker 注册 trait

**目的**：在类型系统中记录 trait 及其方法签名，为后续 impl 验证和约束检查做准备。

**文件**：`checker.h`、`checker.c`

checker.h:
```c
// Checker 结构体新增
struct {
    const char *name;
    struct {
        const char *name;
        Type *type;              // TYPE_FUNCTION：方法的完整签名
        bool is_static;          // true = 静态方法（无 self）
        int self_borrow_kind;    // 0=none, 1=&self, 2=&!self
    } *methods;
    int method_count;
} *trait_registry;
int trait_count;
int trait_cap;
```

checker.c:
- `check_trait_decl(c, node)`：
  - 遍历 `method_sigs`，解析每个方法签名的参数/返回类型
  - 注册到 `trait_registry`
  - 检查重名 trait
- `forward_pass` 新增 `case AST_TRAIT_DECL`

**关键细节**：方法签名中 `Self` 暂时不解析（留给 Step 10），
以 `TYPE_VOID` 或特殊标记占位。

**回退风险**：低。新增一个注册表 + 一个 check 函数。`trait_registry` 默认
NULL/0，所有老路径不触碰。

**验收**：trait 声明后可编译，重复 trait 名报错。ctest 全过。

---

## 4. Phase T2: impl Trait for Struct

### Step 4: Parser 解析 impl Trait for Struct

**目的**：解析 `impl TraitName for StructName { fn ... }` 语法。

**文件**：`parser.c`

现有 `parse_impl_decl` 解析 `impl StructName { ... }`。扩展检测模式：

```
parse_impl_decl():
  consume TOKEN_IMPL
  if current is IDENTIFIER and peek is TOKEN_FOR:
    // impl Trait for Struct { ... }
    trait_name = consume IDENTIFIER
    consume TOKEN_FOR
    struct_name = consume IDENTIFIER
    parse methods...
    return AST_IMPL_TRAIT_DECL
  else:
    // 现有路径：impl StructName { ... } 或 impl(T) StructName(T) { ... }
    ... (不改动)
```

**消歧**：`impl Foo for Bar` vs `impl Foo { ... }`。关键在 peek——
如果 `impl` 后的标识符之后是 `TOKEN_FOR`，走 trait impl；否则走现有路径。
`for` 不是现有关键字，加入后唯一出现在 `impl ... for ...` 语法中，无冲突。

**回退风险**：低。在 `parse_impl_decl` 开头加一个 if 分支，else 保持不变。

**验收**：`impl_trait_parse_test.ls` 解析通过。

### Step 5: Checker 验证 + 注册 trait impl

**目的**：检查 trait impl 的方法签名是否与 trait 声明匹配，注册实现关系。

**文件**：`checker.h`、`checker.c`

checker.h:
```c
// Checker 新增
struct {
    const char *trait_name;
    const char *struct_name;
} *trait_impls;
int trait_impl_count;
int trait_impl_cap;
```

checker.c - `check_impl_trait_decl(c, node)`:
1. 查找 trait_registry 中的 trait 定义 → 拿到 method 签名列表
2. 查找 struct 类型 → 验证 struct 存在
3. 遍历用户提供的方法，与 trait 签名逐一匹配：
   - 方法名必须全覆盖（缺失 → 报错列出未实现的方法）
   - 参数数量和类型必须匹配（Self 替换为具体 struct 类型后比较）
   - 返回类型必须匹配
   - 多余方法 → 报错
4. 注册到 `trait_impls` 表
5. 将方法注册到现有 `impl_registry`（复用 struct 方法调用基础设施）

**关键设计**：trait 方法最终以 `StructName.method_name` 注册到 impl_registry，
与普通 impl 方法共用同一套调度路径。这意味着 **codegen 不需要知道 trait 的存在**。

**错误信息示例**：
```
[error] file.ls:10:1: struct 'Point' does not implement trait 'Printable':
  missing method 'to_string(&self) -> string'
[error] file.ls:15:5: method 'compare' signature mismatch:
  trait requires 'fn compare(&self, &Point) -> int'
  but got         'fn compare(&self) -> int'
```

**回退风险**：中低。新增 `check_impl_trait_decl` 函数 + `trait_impls` 表。
但会向 `impl_registry` 插入方法——如果出 bug，可能影响方法查找。
**防护**：trait impl 方法的注册路径与普通 impl 完全一致（调用相同的
`register_impl_method`），不引入新的注册逻辑。

**验收**：
- 正确 impl → 编译通过
- 缺失方法 → 明确报错
- 签名不匹配 → 明确报错
- ctest 全过

### Step 6: Codegen 适配 + 端到端

**目的**：确保 trait 方法可以通过正常的方法调用语法调用。

**文件**：`codegen.c`（改动极小或零改动）

因为 Step 5 将 trait 方法注册到了 `impl_registry`，codegen 中的方法调用
路径（`StructName.method_name` 查找）自然工作。需要确认：

1. `impl Trait for Struct` 中的方法被 codegen 以 `StructName.method_name` emit
2. 调用 `point.to_string()` 解析为 `Point.to_string`

**可能需要的微调**：
- `codegen_fn_decl` 需要识别 `AST_IMPL_TRAIT_DECL` 中的方法（可能只需在
  处理 `AST_IMPL_DECL` 的同一位置加 `case AST_IMPL_TRAIT_DECL`）
- 方法名前缀：确保 `fn_decl.name` 被设为 `"Point.to_string"` 而非 `"to_string"`

**回退风险**：低。trait 方法的 codegen 路径与普通方法完全对齐。

**验收**：
```ls
trait Greet {
    fn greet(&self) -> string
}
struct Person { string name }
impl Greet for Person {
    fn greet(&self) -> string { return f"Hello, {self.name}!" }
}
fn main() {
    Person p = Person { name: "Alice" }
    print(p.greet())   // Hello, Alice!
}
```
JIT + AOT 双通过。

---

## 5. Phase T3: 约束泛型

### Step 7: Parser 解析约束语法

**目的**：解析 `fn f(T: Trait1 + Trait2)(T x) -> T` 语法。

**文件**：`ast.h`、`parser.c`

ast.h — `fn_decl` 扩展：
```c
struct {
    // 现有字段...
    char **type_params;
    int   type_param_count;
    // 新增：每个 type_param 的约束列表
    struct {
        char **trait_names;    // ["Comparable", "Printable"]
        int    count;
    } *type_param_bounds;      // parallel to type_params; NULL if no bounds
} fn_decl;
```

parser.c — `parse_fn_decl` 类型参数解析扩展：
```
parse type param:
  name = IDENTIFIER
  if match TOKEN_COLON:
    parse trait bound list:
      trait1 = IDENTIFIER
      while match TOKEN_PLUS:
        traitN = IDENTIFIER
```

同样扩展 `struct_decl.type_param_bounds`（为 Step 12 准备）。

**消歧**：`T: Trait` 中的 `:` 与其他冒号用法无冲突——类型参数列表
在 `(` `)` 内，`:` 只在 struct literal `{ field: val }` 和 match arm
`pattern => expr` 中使用，上下文不重叠。

**回退风险**：低。新增 AST 字段默认 NULL，parser 新增 if 分支。
现有无约束泛型 `fn f(T)(...)` 走 else 路径，`type_param_bounds = NULL`。

**验收**：约束泛型函数声明可解析，无约束泛型不受影响。

### Step 8: Checker 约束验证

**目的**：泛型实例化时检查具体类型是否满足 trait 约束。

**文件**：`checker.c`

在现有泛型实例化路径中加入约束检查：

```c
// 现有代码（checker.c AST_CALL 泛型函数实例化）：
//   resolve type_args → build mangled name → clone body → type check

// 新增（在 clone body 之前）：
for (int i = 0; i < tp_count; i++) {
    if (bounds[i].count == 0) continue;  // 无约束，跳过
    for (int b = 0; b < bounds[i].count; b++) {
        if (!checker_type_satisfies_trait(c, type_args[i], bounds[i].trait_names[b])) {
            checker_error(c, ...,
                "type '%s' does not satisfy trait '%s' "
                "(required by type parameter '%s')",
                type_name(type_args[i]), bounds[i].trait_names[b], ...);
            return;
        }
    }
}
```

`checker_type_satisfies_trait(c, type, trait_name)`:
```c
static bool checker_type_satisfies_trait(Checker *c, Type *type, const char *trait_name) {
    // 在 trait_impls 表中查找 (trait_name, type_name(type)) 对
    for (int i = 0; i < c->trait_impl_count; i++) {
        if (strcmp(c->trait_impls[i].trait_name, trait_name) == 0 &&
            strcmp(c->trait_impls[i].struct_name, type_name(type)) == 0) {
            return true;
        }
    }
    return false;
}
```

**关键细节**：约束检查只在泛型实例化点触发。非泛型代码不受任何影响。

**回退风险**：低。新增一个 if 块 + 一个辅助函数。移除该 if 块即回退到
无约束泛型。

**验收**：
- `max(int)(3, 5)` 且 `impl Comparable for int` 存在 → 通过
- `max(Point)(a, b)` 且 `Point` 未实现 `Comparable` → 报错
- 无约束泛型 `identity(int)(42)` → 不受影响

### Step 9: 端到端集成测试

**目的**：验证约束泛型的完整流水线（parse → check → codegen）。

**文件**：`tests/samples/trait_basic_test.ls`、`tests/test_trait.cmake`

测试用例：
```ls
trait Describable {
    fn describe(&self) -> string
}

struct Circle { f64 radius }
impl Describable for Circle {
    fn describe(&self) -> string {
        return f"Circle(r={self.radius})"
    }
}

struct Square { f64 side }
impl Describable for Square {
    fn describe(&self) -> string {
        return f"Square(s={self.side})"
    }
}

fn print_desc(T: Describable)(T x) {
    print(x.describe())
}

fn main() {
    Circle c = Circle { radius: 3.14 }
    Square s = Square { side: 2.0 }
    print_desc(Circle)(c)    // Circle(r=3.140000)
    print_desc(Square)(s)    // Square(s=2.000000)
}
```

**回退风险**：无。测试文件不影响编译器。

**验收**：JIT + AOT 双通过，ctest 全过。

---

## 6. Phase T4: Self 关键字 + 基本类型 impl 扩展

### Step 10: Self 关键字

**目的**：trait 签名和 impl 中的 `Self` 解析为实现类型。

**文件**：`checker.c`

`Self` 处理策略：`Self` 作为 TOKEN_IDENTIFIER 进入 parser，checker 在
trait/impl 上下文中识别名字 `"Self"` 并替换为当前 struct 类型。

- trait 签名解析时：`Self` → 占位（不解析类型，记录需要替换）
- impl Trait for Struct 时：签名中的 `Self` 替换为 `Struct` 类型后再匹配

不新增 token，纯 checker 层面处理，改动最小。

**实现**：
```c
// resolve_type_node 中：
case TYPE_NODE_NAMED:
    if (strcmp(name, "Self") == 0 && c->current_impl_struct_type != NULL) {
        return c->current_impl_struct_type;
    }
    // ... 现有路径
```

checker 新增 `current_impl_struct_type` 字段，在 `check_impl_trait_decl` 和
`check_impl_decl` 入口设置，出口清空。

**回退风险**：低。新增一个 if 检查 + 一个上下文字段。

**验收**：
```ls
trait Addable {
    fn add(&self, &Self other) -> Self
}
struct Vec2 { f64 x; f64 y }
impl Addable for Vec2 {
    fn add(&self, &Vec2 other) -> Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
}
```

### Step 11: 扩展 impl 支持基本类型

**目的**：让 `impl TraitName for int { ... }` 合法，解锁基本类型的 trait 实现。

**文件**：`checker.c`、`codegen.c`

这是 Phase T4 中**风险最高的步骤**。需要解决以下问题：

#### 11.1 Checker 侧

`check_impl_trait_decl` 当前调用 `find_struct_type(c, struct_name)` 验证
目标类型存在。对基本类型需要走不同路径：

```c
Type *target_type = find_struct_type(c, struct_name);
if (target_type == NULL) {
    // 尝试基本类型
    target_type = resolve_builtin_type_by_name(struct_name);
    //  "int" → type_int(),  "string" → type_string(), ...
    if (target_type == NULL) {
        checker_error(c, ..., "impl for undefined type '%s'", struct_name);
        return;
    }
}
```

新增 `resolve_builtin_type_by_name` 辅助函数（纯名字→Type* 映射表）。

impl_registry 中注册时，`struct_name` 可以是 `"int"` / `"string"` 等，
无需改动——impl_registry 的 key 就是字符串。

#### 11.2 Codegen 侧

trait 方法以 `"int.compare"` 等名字注册到 impl_registry。codegen 需要：

1. **方法 emit**：`AST_IMPL_TRAIT_DECL` 中的方法正常 emit 为 LLVM 函数，
   名字为 `"int.compare"`。函数体由用户提供，codegen 不需要合成。

2. **self 参数 ABI**：
   - struct 方法的 `&self` → 指针（`Struct*`）
   - 基本类型的 `&self` → 需要决策：
     - **方案 A**：与 struct 一致，传指针（`int*`）。调用点 alloca + store + 传地址。
       优点：一致性好；缺点：int 传指针效率低。
     - **方案 B**：基本类型 by-value 传递。需要 codegen 区分 struct self 和 primitive self。
   - **推荐方案 A**——一致性优先，性能问题留给 LLVM 优化。

3. **方法调用解析**：`a.compare(&b)` 需要 checker 识别 `a` 是 `int` 时，
   去 impl_registry 查 `"int"` 的方法。当前 checker 的方法查找只对 struct 走
   impl_registry（`lookup_impl_method`），需要扩展让基本类型也走这条路。

#### 11.3 内建方法冲突

`string` 已有内建 `.compare` 方法。当用户写 `impl Comparable for string` 时：

- **策略**：trait impl 方法优先级低于内建方法（或反过来）
- **推荐**：v1 直接禁止为已有同名内建方法的类型实现冲突 trait，报编译错误。
  用户可以换一个方法名的 trait 避开。这是最安全的策略。

#### 11.4 回退风险

**中高**。涉及 checker 方法查找路径扩展 + codegen self 参数 ABI 适配。
如果出问题，可能影响现有 struct 方法调用。

**防护**：所有新路径都有 `if (target_is_primitive)` 守卫，不触碰 struct 路径。

**验收**：
```ls
trait Describable {
    fn describe(&self) -> string
}
impl Describable for int {
    fn describe(&self) -> string {
        return f"number:{self}"
    }
}
fn main() {
    int x = 42
    print(x.describe())   // number:42
}
```

### Step 12: 编译器预注册内建 trait impl

**目的**：让 int/string/f64/bool 开箱即有常用 trait，用户无需手写。

**前置依赖**：Step 11（基本类型 impl 基础设施）。

**文件**：`checker.c`、`codegen.c`

在 checker 初始化时，预注册 trait 定义和 impl 记录：

```c
builtin_register_traits(c):
  register trait "Comparable" { fn compare(&self, &Self) -> int }

  register impl "Comparable" for "int"
  register impl "Comparable" for "string"
  register impl "Comparable" for "f64"
```

**关键问题**：内建 trait impl 没有用户 AST（没有 fn body）。codegen 需要
知道 `int.compare()` 怎么实现。

**解决方案**：codegen 对内建 trait 方法做特殊处理——在 emit 方法调用时，
如果 callee 是内建 trait 方法，直接 inline emit IR：
- `int.compare(&self, &int other) -> int` → emit `sub self, other`
- `string.compare(&self, &string other) -> int` → emit 调现有 `ls_str_compare`
- `f64.compare(&self, &f64 other) -> int` → emit `fcmp` + `select`

这类似 `string.length` 等现有内建方法的 codegen 模式。

**替代方案（保守）**：不做内建预注册，仅提供基础设施。用户手写：
```ls
impl Comparable for int {
    fn compare(&self, &int other) -> int { return self - other }
}
```
这更安全但用户体验差。

**回退风险**：中。内建方法 codegen 路径是新增代码，集中在一个 switch 内。

**验收**：
```ls
// 无需手写 impl，开箱即用
fn main() {
    print(max(int)(3, 7))       // 7
    print(max(f64)(1.5, 2.3))   // 2.3
}
```

---

## 7. Phase T5: 泛型 struct 约束

### Step 13: struct Foo(T: Trait) 约束检查

**目的**：泛型 struct 的类型参数也支持 trait 约束。

**文件**：`checker.c`

在 `checker_instantiate_struct` 中添加约束检查，与 Step 8 的泛型函数
约束检查逻辑完全对称：

```c
// checker_instantiate_struct 中，模板查找之后：
if (tmpl->type_param_bounds) {
    for (int i = 0; i < tp_count; i++) {
        // 复用 checker_type_satisfies_trait
    }
}
```

**回退风险**：极低。新增一个 if 块。

**验收**：
```ls
trait Hashable {
    fn hash(&self) -> int
}
struct HashSet(T: Hashable) {
    vec(T) items
}
// HashSet(int) → 需要 impl Hashable for int
```

---

## 8. 风险矩阵

| Phase | 步骤 | 改动范围 | 回退难度 | 与现有代码冲突风险 | 高风险点 |
|-------|------|----------|----------|-------------------|----------|
| T1 | 1-3 | token.h, ast.h/c, scanner.c, parser.c, checker.h/c | 极易 | 无 | — |
| T2 | 4-6 | parser.c, checker.c, codegen.c | 易 | **低**：向 impl_registry 插入方法 | Step 5 签名匹配逻辑 |
| T3 | 7-9 | ast.h, parser.c, checker.c | 易 | 无（仅加 if 守卫） | Step 8 `checker_type_satisfies_trait` 漏判 |
| T4 | 10-12 | checker.c, codegen.c | **中高** | **高**：基本类型方法查找 + self ABI | Step 11 基本类型 impl 基础设施 |
| T5 | 13 | checker.c | 极易 | 无 | — |

**最高风险项**：
1. **Step 11**（基本类型 impl 扩展）——涉及 checker 方法查找路径 + codegen
   self 参数 ABI，可能影响现有 struct 方法调用。
   **缓解**：所有新路径用 `if (is_primitive)` 守卫隔离
2. **Step 12**（内建 trait codegen）——需要为基本类型合成方法实现（无用户 AST）。
   **缓解**：可跳过，仅做 Step 11 让用户手写 impl body

**中等风险项**：
1. **Step 5**（impl 签名匹配）——类型比较需要处理 Self 替换、引用 ABI 等细节
2. **Step 6**（codegen 方法名）——trait impl 方法必须以 `StructName.method` 命名，
   与现有 impl 方法一致

**低风险项**：
1. **Step 7**（约束语法解析）——`:` 在 `(T: Trait)` 上下文中无冲突
2. **Step 13**（struct 约束）——复用 Step 8 的 `checker_type_satisfies_trait`

**整体风险评估**：Phase T1-T3（Step 1-9）风险很低，可以放心推进。
Phase T4（Step 10-12）风险集中在 Step 11，建议做好回退准备（独立提交）。

---

## 9. 测试计划

| 测试文件 | 覆盖阶段 | 测试内容 |
|----------|----------|----------|
| `trait_parse_test.ls` | T1 Step 2 | 仅声明 trait，编译通过 |
| `trait_impl_test.ls` | T2 Step 5-6 | impl Trait for Struct + 方法调用 |
| `trait_impl_reject.ls` | T2 Step 5 | 缺失方法 / 签名不匹配 → 编译错误 |
| `trait_bound_test.ls` | T3 Step 8-9 | 约束泛型函数调用（仅 struct 类型） |
| `trait_bound_reject.ls` | T3 Step 8 | 不满足约束 → 编译错误 |
| `trait_self_test.ls` | T4 Step 10 | Self 关键字 |
| `trait_primitive_test.ls` | T4 Step 11 | impl Trait for int/string + 方法调用 |
| `trait_builtin_test.ls` | T4 Step 12 | 内建 trait（max(int) 开箱即用） |
| `trait_struct_bound.ls` | T5 Step 13 | 泛型 struct 约束 |

每个 `.ls` 对应一个 `.cmake` 测试脚本，JIT + AOT 双验证。

---

## 10. 未来扩展（不在 v1 范围）

| 特性 | 复杂度 | 依赖 |
|------|--------|------|
| 默认方法（trait 中提供 body） | 中 | Self 关键字 |
| 动态分派（`&dyn Trait`） | 高 | vtable 生成、trait object 胖指针 |
| 关联类型（`type Item`） | 高 | 类型推导扩展 |
| 内建 trait 自动实现 | 中 | codegen 特殊路径 |
| trait 继承（`trait A: B`） | 中 | 约束传递检查 |
| 泛型 trait（`trait Iter(T)`） | 高 | 二次单态化 |
| `where` 子句 | 低 | 纯语法糖 |

---

## 11. 实现顺序建议

### 11.1 MVP 路径（最小可用集）

```
Step 1-3 → Step 4-6 → Step 7-9
```

Phase T1 + T2 + T3（9 步），**仅支持 struct 实现 trait**。
跳过 Self 关键字、基本类型 impl、内建 trait。

这已经足够让泛型函数对 struct 类型可用：

```ls
trait Sortable {
    fn less_than(&self, &Sortable other) -> bool
}

struct Score { int value }

impl Sortable for Score {
    fn less_than(&self, &Score other) -> bool {
        return self.value < other.value
    }
}

fn min_val(T: Sortable)(T a, T b) -> T {
    if a.less_than(&b) { return a }
    return b
}

fn main() {
    Score a = Score { value: 3 }
    Score b = Score { value: 7 }
    Score m = min_val(Score)(a, b)
    print(m.value)   // 3
}
```

### 11.2 完整路径

```
MVP (Step 1-9)
  → Step 10 (Self 关键字)
  → Step 11 (基本类型 impl 基础设施)     ← 最高风险，独立提交
  → Step 12 (内建 trait impl)            ← 可选，用户可手写替代
  → Step 13 (泛型 struct 约束)           ← 低风险收尾
```

**关键决策点**：MVP 完成后评估是否需要 Phase T4。
如果用户场景主要是 struct（自定义数据结构的泛型算法），MVP 就够用。
如果需要 `max(int)(3, 7)` 这类基本类型泛型调用，才推进 Phase T4。

### 11.3 各阶段可独立发布

| 发布点 | 用户可做的事 |
|--------|-------------|
| Phase T1-T2 完成 | 声明 trait + 为 struct 实现 trait + 当普通方法调用 |
| Phase T3 完成（MVP） | 约束泛型函数 `fn f(T: Trait)(T x)`，T 限 struct |
| Phase T4 完成 | `impl Trait for int/string`，`max(int)` 开箱即用 |
| Phase T5 完成 | `struct Foo(T: Trait)` 泛型 struct 约束 |
