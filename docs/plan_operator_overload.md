# LS 操作符重载设计

> **状态**：✅ 已实现（2026-05-31，ctest 88/88，含 JIT+AOT+memcheck）
> **风格**：Ruby 风符号方法名 + trait 约束（参考 Rust 的 Add/PartialEq/PartialOrd 模型）
> **核心手法**：checker 把 `a OP b` 降级（lowering）为合成的方法调用 AST 节点，复用现有方法调用 checker + codegen 路径。

---

## 1. 总览

为用户自定义 `struct` / `enum` 提供算术与比较运算符重载，解锁 `Vec2 + Vec2`、`Point == Point`、按模长排序，以及最关键的**泛型数值算法**（`fn sum(T: Add)(vec(T)) -> T`，操作符在泛型体内可被类型检查）。

### 1.1 设计决策

| 维度 | 决策 |
|------|------|
| 写法 | 方法名即符号本身（Ruby 风），写在 `impl Trait for T` 块里：`fn +(&self, &Vec2 rhs) -> Vec2` |
| 约束 | **必须显式 `impl Add for T`** 才能用 `+`；纯方法名不触发（解锁泛型 bound） |
| rhs | 取只读借用 `&Self`，`a + b` 不消耗 `b`，对 has_drop 类型安全；调用点靠现有只读 auto-borrow 自动插 `&b` |
| 操作符集合 | 固定为内建 7 个 trait，不可由用户扩展 |
| 比较 | 推导默认 + 逐个可覆写（见 §4） |

### 1.2 内建 trait（7 个，软保留）

软保留 = 仅占 **trait 命名空间**。用户 `trait Add{}` → 命中已有重复检测报错；`struct Add` / 变量 `Add` 合法（不同命名空间）。

| trait | 必填方法 | 可选覆写方法 | 覆盖运算符 |
|-------|----------|--------------|------------|
| `Add` | `+` | — | `+` |
| `Sub` | `-` | — | `-` |
| `Mul` | `*` | — | `*` |
| `Div` | `/` | — | `/` |
| `Rem` | `%` | — | `%` |
| `Eq`  | `==` | `!=` | `==` `!=` |
| `Ord` | `<`  | `>` `<=` `>=` | `<` `>` `<=` `>=` |

算术 1:1（运算彼此独立）；比较多对一（同一个序关系派生多个运算符）。共 7 trait 覆盖 11 运算符。

---

## 2. 内部规范名（`$` sigil）

parser 把符号 token 规范化为内部方法名，前缀用 **`$` sigil**：

| 运算符 | token | 内部名 | trait |
|---|---|---|---|
| `+` | PLUS | `$op_add` | Add |
| `-` | MINUS | `$op_sub` | Sub |
| `*` | STAR | `$op_mul` | Mul |
| `/` | SLASH | `$op_div` | Div |
| `%` | PERCENT | `$op_rem` | Rem |
| `==` | EQ | `$op_eq` | Eq |
| `!=` | NEQ | `$op_ne` | Eq |
| `<` | LT | `$op_lt` | Ord |
| `>` | GT | `$op_gt` | Ord |
| `<=` | LEQ | `$op_le` | Ord |
| `>=` | GEQ | `$op_ge` | Ord |

**为什么用 `$`**：`$` 不是合法标识符字符（scanner 的 `scan_identifier` 仅收 `isalnum`/`_`），用户在任何位置都写不出 `$op_eq` → **结构上杜绝与用户标识符/方法的碰撞**。`$` 在 LS scanner 里完全空闲（非 token、非指令；区别于已占用的 `#` 指令 / `@` 注解），且是编译器合成符号的惯例 sigil（GCC/JVM `Outer$Inner`），多数目标文件符号格式本就合法，LLVM 函数名大概率无需再 sanitize。内部名由 parser 直接合成为字符串，不经过 scanner。

---

## 3. 符号方法名的合法位置（两层防御）

可重载操作符固定为内建 7 个，**用户不能自定义操作符 trait**。靠两层防御实现：

- **parser 层**：符号 token 作方法名，**仅在 `impl Trait for Type` 的方法位置**接受（`parse_fn_decl` 加 `allow_operator_name`，只在 `parse_impl_decl` 的 trait-impl 分支置 true）。其余位置——顶层 `fn`、`trait{}` 签名体、裸 `impl Type{}` 块——遇符号一律报 `expected function name`。
  → 顶层 `fn +`、用户 trait 声明符号方法，在此被天然拒绝。
- **checker 层**：即便符号方法出现在某 trait impl 中，还要校验该 trait 是匹配的内建操作符 trait（`$op_add`↔Add、`$op_eq`↔Eq、`$op_lt/gt/le/ge`↔Ord…），否则报 `operator method '+' is only valid when implementing built-in trait Add`。
  → 挡住 `impl SomeUserTrait for T { fn + }`。

---

## 4. Eq / Ord 的派生与覆写语义（核心规则）

派生是**惰性的、按使用点、在 lowering 时逐个解析**，**不是**预先生成方法。

### 4.1 派生公式（仅当该运算符无显式方法时启用）

| 表达式 | 派生为 | 依赖 |
|--------|--------|------|
| `a != b` | `!(a $op_eq b)` | `Eq` 的 `==` |
| `a > b`  | `b $op_lt a`     | `Ord` 的 `<` |
| `a <= b` | `!(b $op_lt a)`  | `Ord` 的 `<` |
| `a >= b` | `!(a $op_lt b)`  | `Ord` 的 `<` |

### 4.2 解析顺序（lowering 时对每个比较运算符）

```
对 a OP b（OP ∈ {!=, >, <=, >=}）：
  1. 若 type(a) 有该运算符的显式方法（$op_ne/$op_gt/$op_le/$op_ge） → 直接调用显式方法
  2. 否则 → 按 §4.1 公式派生（要求对应 trait 已 impl）
  显式永远优先于派生。
```

### 4.3 规则清单（显式写明）

1. **`<` 必填且是派生源**：`impl Ord for T` 必须定义 `<`（`$op_lt`）。缺失 → `impl Ord for Vec2 missing required method '<'`。同理 `Eq` 必须定义 `==`。
2. **`> <= >=` 缺省由 `<` 惰性派生**；`!=` 缺省由 `==` 派生。只写 `<` 和 `==` 即可获得全部 6 个比较运算符。
3. **覆写只能写在同一个 trait impl 块内**。`>`/`<=`/`>=` 的覆写必须与 `<` 同处一个 `impl Ord for T` 块；`!=` 覆写与 `==` 同处 `impl Eq for T` 块。原因：
   - 符号方法名只允许出现在匹配的内建 trait impl 里（§3）；
   - 同一 `(trait, type)` **不允许多个 impl**（coherence，checker.c:7527 `trait 'Ord' already implemented for struct 'Vec2'`）。
   - 故"另起一块定义 `>`"在所有写法下都报错：第二个 `impl Ord` → 重复 impl 错；裸 `impl T { fn > }` → parser 拒；`impl OtherTrait for T { fn > }` → checker 拒。
4. **显式优先于派生**：同时写了 `<` 和 `>` 时，`a > b` 走显式 `$op_gt`，派生不触发，互不冲突。
5. **顺序无关**：块内方法定义先后顺序不影响——checker 先注册块内全部方法，再跑完整性 + 放宽检查。`>` 写在 `<` 前亦可。
6. **不可重复**：同块内同一运算符写两遍 → `duplicate implementation of method`（checker.c:7588）。
7. **完整性检查放宽**：内建操作符 trait 的 impl 允许出现额外的已知覆写方法——`Eq` 允许额外 `$op_ne`；`Ord` 允许额外 `$op_gt/$op_le/$op_ge`（普通 trait 仍要求实现方法集恰好等于声明）。

> 适用场景：全序类型（如 Vec2 按模长比）只写 `<`/`==` 省事；偏序 / 特殊语义类型（NaN、集合包含、版本号等，`!(a<b)` ≠ `a>=b`）则逐个精确定义需要覆写的运算符。

---

## 5. 降级（lowering）实现要点

- **AST**：`binary` 节点加 `AstNode *lowered`（默认 NULL）。checker 命中重载时填入合成的方法调用 / 派生表达式节点；codegen 进入 AST_BINARY 第一件事 `if (lowered) return codegen_expr(lowered);`，其余数值/字符串路径不变。实现位置：`check_expr` 的 AST_BINARY 入口调 `try_operator_overload`（`src/checker.c`）。
- **gate**：仅当 `left`（解指针后）为 STRUCT/ENUM 时进入重载分支（泛型类型参数 T 已被 G2 单态化为具体 struct，故同样命中），且 `checker_type_satisfies_trait(c, lt, trait)` 成立；否则报 `type 'Vec2' does not implement trait 'Add' (required for operator '+')`。必须在 op switch **之前**拦截（早于现有 `==` 对 struct 的 `type_equals→bool` 误判路径）。
- **合成**：直接方法 → `AST_CALL{ callee: AST_FIELD{ object, field: 内部名 }, args:[arg] }`；派生 `>` → 交换操作数；派生 `!=`/`<=`/`>=` → 再套 `AST_UNARY(BANG, ...)`。object/arg 用 **`ast_clone_deep` 深克隆** left/right。合成后 `check_expr(lowered)` 完成方法分派 + 借用 + sret，再把结果类型赋给 binary 节点。
- **内存（已采用更稳方案，替代原"浅释放"）**：`lowered` 持有 left/right 的**独立深克隆**，故 `ast_free` 的 AST_BINARY 分支释放 left/right 后**正常递归** `ast_free(lowered)` 即可——无别名、无 double-free 陷阱。`ast_clone_deep` 的 AST_BINARY 分支已将克隆出的 `lowered` 置 NULL。新增公共分配器 `ast_new(kind,line,col)`（`src/ast.c`/`ast.h`）供 checker 合成节点。已 memcheck 验证 0 leak/0 dfree（含 has_drop string 字段返回 owned struct）。
- **泛型体内**：left 在 G2 单态化后是具体 struct，降级方法调用走现有实例方法分派路径，已用 `sum_all(T: Add)` 端到端验证（JIT+AOT）通过。
- **解析歧义（已修）**：`Type x = a * b`（裸标识符相乘作声明初值）曾触发既有的"声明 vs 乘法"误判，现已在 parser 修复，`Vec2 e = a * b` 可直接写，无需括号。

---

## 6. 示例

```rust
struct Vec2 { f64 x; f64 y }

impl Add for Vec2 { fn +(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x:self.x+rhs.x, y:self.y+rhs.y } } }
impl Sub for Vec2 { fn -(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x:self.x-rhs.x, y:self.y-rhs.y } } }
impl Mul for Vec2 { fn *(&self, &Vec2 rhs) -> Vec2 { return Vec2{ x:self.x*rhs.x, y:self.y*rhs.y } } }
impl Eq  for Vec2 { fn ==(&self, &Vec2 rhs) -> bool { return self.x==rhs.x && self.y==rhs.y } }
impl Ord for Vec2 {
    fn <(&self, &Vec2 rhs) -> bool {            // 必填，派生 > <= >=
        return (self.x*self.x+self.y*self.y) < (rhs.x*rhs.x+rhs.y*rhs.y)
    }
}

fn sum_all(T: Add)(vec(T) xs, T zero) -> T {    // 泛型：T: Add 约束让体内 + 可检查
    T acc = zero
    for x in xs { acc = acc + x }
    return acc
}

fn main() {
    Vec2 a = Vec2{ x:1.0, y:2.0 }
    Vec2 b = Vec2{ x:3.0, y:4.0 }
    Vec2 c = a + b                 // a.$op_add(b) → (4,6)
    print(a != b)                  // 由 == 取反派生 → true
    print(a > b)                   // 由 b < a 派生 → false
    vec(Vec2) vs = [a, b, c]
    Vec2 total = sum_all(Vec2)(vs, Vec2{ x:0.0, y:0.0 })   // (8,12)
    print(f"total=({total.x},{total.y})")
}
```

---

## 7. 实现步骤（概要）

1. **AST**（`src/ast.h` / `src/ast.c`）：`binary.lowered` 字段 + `ast_free` 正常递归释放（clone-based，无浅释放）+ 公共 `ast_new`。
2. **parser**（`src/parser.c`）：`parse_fn_decl` 加 `allow_operator_name`，trait-impl 分支传 true，符号 token → `$op_*`。
3. **checker**（`src/checker.c`）：初始化预注册 7 内建 trait（方法名 `$op_*`，`(&self,&Self)->Self|bool`）；符号方法 trait 匹配校验；放宽 Eq/Ord 完整性；重复 trait 报错消息特判。
4. **checker AST_BINARY**（约 4199）：op switch 前插重载降级分支。
5. **codegen AST_BINARY**（`src/codegen.c`）：`if (node->as.binary.lowered) return codegen_expr(ctx, node->as.binary.lowered);`。
6. **测试**：parser 单元 + `ast_free` memcheck；e2e（AOT+JIT）算术/比较/派生/覆写/泛型；has_drop struct 返回 owned 的 memcheck clean；负例（缺 impl / 顶层 `fn +` / 非操作符 trait 用符号名）。全量 `ctest --repeat until-pass:2`。

---

## 8. 后续（首批不做）

- 索引运算符 `[]` / `[]=`（需改 parser 下标赋值路径）。
- 一元 `-`（neg）、`!`（not）的重载。
- 用户自定义操作符 trait（有意封死，保持操作符集合固定）。
