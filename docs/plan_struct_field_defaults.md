# Struct 字段默认值 + 部分初始化 —— 实现计划

> **日期**：2026-06-02
> **状态**：📋 规划中（未实现）
> **动机**：LS 无函数重载、无默认参数、无具名参数 → 可选配置缺乏人体工学手段。
> 采用 **Zig 路线**（options struct + struct 字段默认值），而非函数默认参数。
> 关联讨论：见会话；对照 `docs/plan_plot_infra.md`（plot/timeline 是首个受益方）。

---

## 0. 背景与动机

LS 当前 struct 字面量必须**写全所有字段**，且函数无默认/具名参数、无重载。结果：
所有"可选配置"都要么在调用点重复写默认值，要么把 API 拆成多个函数（如 `topology` /
`topology2`，`plot` / `plot_xy` / `plot_styled`），或加尾随参数让每个调用点都写
`..., 1000, 400, "rainbow"`。

### 三个系统级语言怎么做（都不做函数默认/具名/重载）

| | 默认参数 | 具名参数 | 重载 | 主力惯用法 |
|---|:---:|:---:|:---:|---|
| Zig | ❌ | ❌ | ❌ | **options struct + 字段默认值**（`render(ev, .{ .theme = "x" })`） |
| Rust | ❌ | ❌ | ❌ | Builder / `Opts { theme, ..Default::default() }` |
| Go | ❌ | ❌ | ❌ | Functional Options（`Render(ev, WithTheme("x"))`） |

三者共识：**把可选配置下沉到值（struct）层面**，函数签名保持简单显式。

### 为什么 LS 选 Zig 路线

LS 已经有 struct + 命名 struct 字面量 `Foo{a: 1}`，只差两点：
1. **struct 字段默认值**：`struct Opts { int w = 1000 }`
2. **部分初始化**：`Opts{theme: "viridis"}`，省略的字段用默认

补上这两点即可用 options struct 惯用法表达"具名 + 默认 + 可跳过中间"，
**完全不碰函数调用约定/arity 逻辑**：

```ls
struct PlotOpts { int w = 1000  int h = 400  string theme = "rainbow" }
fn cpu_timeline_svg(vec(CpuSchedEvent) events, CpuTopology topo, PlotOpts opts) -> string

cpu_timeline_svg(ev, topo, PlotOpts{})                  // 全默认
cpu_timeline_svg(ev, topo, PlotOpts{theme: "viridis"})  // 具名 + 跳过中间
```

> **与"函数默认参数（Python 式）"的关系**：两条路不互斥。Zig 路线更隔离（只动 struct
> 字面量，不动函数机制）、复用 LS 现成的 has_drop struct drop，性价比更高，故**先做这条**。
> 函数默认参数将来真需要可独立再加（见 §8）。

---

## 1. 设计

### 1.1 语法

**定义**（struct 字段可带 `= 默认值`）：
```ls
struct PlotOpts {
    int w = 1000              // POD 字面量默认
    int h = 400
    string theme = "rainbow"  // string 字面量默认（该 struct 因含 string 而 has_drop，无妨）
    vec(f64) data             // 无默认 → 构造时必须显式给
}
```

**部分初始化**（字面量里可省略"有默认值的字段"）：
```ls
PlotOpts{data: xs}                    // w/h/theme 用默认，data 显式
PlotOpts{data: xs, theme: "viridis"}  // 再覆盖 theme
PlotOpts{w: 1600, h: 900, theme: "cool", data: xs}  // 全显式，照旧合法
```

字段名匹配靠 `:`（沿用现有 struct 字面量语法），天然具名、可乱序、可跳过有默认的字段。

### 1.1b 字段分隔规则（同步收紧，2026-06-03）

字段边界 = **逗号 `,` / 分号 `;` / 换行 任一**（可混用、可尾随）。**同行两个字段之间必须有
`,` 或 `;`** —— 同行纯空格无分隔（`struct S { int a int b }`）现报错
"struct fields on the same line must be separated by ',' or ';'"。换行分隔无需分隔符（沿用
LS "分号可选" 风格）。实现：parser 字段循环用 token 行号判断是否跨行（[parser.c](../src/parser.c)
`parse_struct_decl`）。收紧后修了 8 个用旧式同行空格写法的 sample（bf045/bf046/modtype_*/
struct_byval_arg/struct_field_readthrough）。

### 1.2 语义（核心）

- **默认值在「struct 构造点」求值**，每次 `PlotOpts{...}` 构造独立填充。
- 这天然避开 Python "默认值定义时求值一次 → 可变默认值被共享" 的坑。
- 一旦所有字段都有值（显式或默认），就是个**完整的普通 struct 值** —— 后续所有权/drop/move
  路径与"写全字段的字面量"**完全一致**。

---

## 2. drop 分析（关键，决定分步边界）

### 2.1 两个独立维度

| 维度 | 含义 |
|---|---|
| **A. struct 是否 has_drop** | 字段里有没有 string/vec/map/has_drop-struct/has_drop-enum/Block |
| **B. 默认值表达式的复杂度** | 默认值是字面量？还是要在构造点构造的容器/struct？ |

**决定实现难度与 drop 处理的是 B，不是 A。**

### 2.2 为什么 has_drop struct 部分初始化「无新 drop 风险」

部分初始化只改变"省略的字段怎么拿到初值"（构造点填默认），**不改变 struct 整体怎么 drop**
（has_drop struct 的自动 drop LS 早已支持）。等价关系：

```
PlotOpts{data: xs}    ≡    PlotOpts{w: 1000, h: 400, theme: "rainbow", data: xs}
```

右边是已支持的完整字面量构造。左边只是把 w/h/theme 的值由"用户写"换成"编译器按默认填"。
所有权语义逐字段独立、与来源无关 → **没有任何新的悬垂/双释放面**。

具体到字段类型：
- `theme = "rainbow"`：默认是 static string（cap=0），构造点填入，struct drop 时 cap=0 **不 free** ✅
- POD 默认（int/bool/...）：无 drop ✅
- `data`（无默认）：用户显式传，正常 move ✅

> 结论：**第一步不该按"是否 has_drop"切**（那会把最常用的 `string theme="rainbow"` 挡在外面，
> 使特性几乎没用）。应按维度 B（默认值表达式复杂度）切。

---

## 3. 分两步（按默认值表达式复杂度）

### 第 1 步（MVP）—— 默认值限「编译期字面量」 ✅ 已实现（2026-06-03）

- 允许的默认值：`int / i64 / f64 / bool / char / string` 字面量（含负数字面量）。
- struct **可以** has_drop（含 string/vec/map 字段都行）。
- **省略的字段**：有声明默认值 → 用默认；**无默认值 → 沿用 LS 既有的零初始化**
  （`Foo{}` 在 LS 本就合法、省略字段零填充，类似 Go —— 见 `tests/test_types.c`，
  **不是错误**）。所以本特性是"默认值覆盖零填充"，**完全不破坏** struct 字面量既有语义。
- 覆盖 options struct 的 ~99% 场景（配置项几乎都是数值/布尔/字符串/枚举值）。

> ⚠️ **实现期纠正**：初版误以为"struct 字面量必须写全字段、缺字段报错"，导致 test_types 7 处
> `Foo{}`/`Pod{}` 合法性断言回归。LS 实际一直允许部分初始化（缺字段零填充）。已改为"只对有
> 默认值的省略字段填默认，其余零填充"，回归全消。

### 第 2 步 —— 默认值扩展到「构造点可求值的表达式」

- 空容器 `vec(f64) data = []`、`map(K,V) m = {}`；嵌套 struct 字面量 `Sub s = Sub{}`；
  枚举构造 `Option(int) o = None`；常量表达式。
- 所有权路径仍是"构造点求值" → drop 安全，纯粹是 codegen 要为这些字段在构造点生成构造代码，
  且测试面更大（嵌套 has_drop、空容器 drop 等）。
- 非 MVP，按需推进。

> 默认值表达式**不允许**引用其它字段、不允许函数副作用调用（v1/v2 都不做），保持"构造点
> 纯求值、可预测、无序依赖"。

---

## 4. 第 1 步实现要点（不碰函数机制）

### 4.1 AST（[ast.h:384](../src/ast.h)）
`struct_decl` 现为并行数组 `field_types[] / field_names[] / field_count`。
**新增** `AstNode **field_defaults`（与 field_names 平行；无默认的字段存 `NULL`）。
- ast.c：`ast_free` 释放 `field_defaults[i]`（非 NULL 时 `ast_free`）；`ast_clone_deep` 深拷。

### 4.2 parser（struct 声明解析）
解析每个字段时，类型+名字之后若遇到 `=`，解析一个**字面量表达式**作为默认值存入
`field_defaults[i]`；否则存 NULL。
- v1 限制：`=` 后只接受字面量节点（number / string / bool / char / 负号+number）。
  非字面量 → parser 报错 "struct field default must be a literal (v1)"。

### 4.3 checker（struct 字面量校验，[checker.c:5956](../src/checker.c)）
当前在 `new_expr` 逐个校验 `field_inits[]`、查重复字段。**新增默认值类型校验**：
- 对 struct 的每个声明字段，若未被 `field_inits` 覆盖：
  - **有默认值** → 用默认值表达式的类型校验 vs 字段类型（`type_equals`）。
  - **无默认值** → 跳过（沿用既有零初始化，**不报错**）。
- 重复/未知字段：保留现有检查。
- 把"该 struct 字面量缺哪些字段、各自默认值"传递给 codegen（可在 new_expr 节点上挂解析结果，
  或 codegen 重新按声明顺序比对 field_inits —— 后者零额外状态，推荐）。

### 4.4 codegen（struct 构造，[codegen.c:13277](../src/codegen.c)）
当前遍历 `field_inits[]` 填字段。**改为按「声明字段顺序」构造**：
- 对每个声明字段 i：
  - 若被 `field_inits` 显式提供 → 用该 value（现有路径）。
  - 否则 → 对 `struct_decl.field_defaults[i]` 求值（字面量：POD 塞常量 / string 走
    `ls_string_from_literal`，cap=0 static），填入该字段槽。
- 其余（GEP、store、has_drop 字段 clone/move 语义）不变。

> 注：当前 codegen 似乎不强制全字段（缺字段可能留未初始化）。新逻辑顺带消除该隐患
> （缺且无默认在 checker 已拦，缺且有默认在 codegen 填）。

### 4.5 runtime
**零改动**。

---

## 5. 规则与错误（第 1 步）

```ls
struct Opts { int w = 1000  string theme = "rainbow"  vec(f64) data }

Opts{data: xs}                 // ✅ w/theme 默认
Opts{}                         // ✅ w/theme 默认，data 零初始化（空 vec，沿用既有语义）
Opts{w: 5, w: 6, data: xs}     // ❌ duplicate field initializer 'w'
Opts{color: "x", data: xs}     // ❌ unknown field 'color'
Opts{w: "big", data: xs}       // ❌ field 'w': expected 'int', got 'string'

struct Bad { int w = foo() }    // ❌ struct field default must be a literal (v1)
```

---

## 6. 测试计划

`tests/samples/struct_field_defaults_test.ls`（自验证，打印 `SFDEF PASS`）：
- POD 默认填充值正确；显式覆盖生效
- string 默认（has_drop struct）：构造 + 读取 + drop，**memcheck 0 leak / 0 double-free**
- 混合：部分字段默认、部分显式（含 vec 字段显式传 + move 正确）
- 嵌套：struct 含 has_drop struct 字段、vec 字段，部分默认部分显式
- 负面（编译期报错，单独的 `should-fail` 校验或 checker 单测）：缺无默认字段 / 未知字段 /
  类型不匹配 / 非字面量默认
- 单元测试 `tests/test_struct_field_defaults.c`：parser 解析默认值、checker 缺字段判定
- 三重：JIT + AOT + `--memcheck`；回归全绿

---

## 7. 受益示例（真实 plot API 改造）

```ls
// 改造前（尾随参数，每个调用点重复默认）
fn cpu_timeline_svg(vec(CpuSchedEvent) events, CpuTopology topo,
                    int w, int h, string theme) -> string
cpu_timeline_svg(ev, topo, 1000, 400, "viridis")   // 被迫写 w/h

// 改造后（options struct）
struct CpuPlotOpts {
    int w = 1000
    int h = 400
    string theme = "rainbow"
}
fn cpu_timeline_svg(vec(CpuSchedEvent) events, CpuTopology topo, CpuPlotOpts opts) -> string
cpu_timeline_svg(ev, topo, CpuPlotOpts{})                  // 全默认
cpu_timeline_svg(ev, topo, CpuPlotOpts{theme: "viridis"})  // 只改 theme
```

可顺带合并掉 `topology2`：
```ls
struct TopoOpts { int physical = 0  bool hyperthreading = true }  // physical=0 表示自动推断
fn topology(int total_cpus, TopoOpts opts = ...) // （注：函数级默认见 §8；v1 仍需显式传 opts）
```

> v1 没有函数级默认，所以"opts 参数本身能否省略"仍需 §8 的函数默认参数。但 struct 字段默认
> 已经让 `CpuPlotOpts{}` 极廉价，调用点只多打一个 `CpuPlotOpts{}`。

---

## 8. 与函数默认参数（Python 式）的关系

| | struct 字段默认（本计划） | 函数默认 + 具名参数（Python 式） |
|---|---|---|
| 实现面 | 只动 struct 字面量（AST/parser/checker/codegen 各小改） | 改函数**调用约定**：arity 放宽、具名实参解析与重排 |
| 调用写法 | `f(ev, Opts{theme: "x"})`（包一层 struct） | `f(ev, theme="x")`（最轻） |
| 具名/跳过中间 | ✅（struct 字段天然） | ✅（具名实参） |
| drop | 构造点填充，复用现有 struct drop | 调用点求值，等价显式传 |
| 与 LS 现状 | 复用现成 struct 机制，最隔离 | 新调用约定逻辑 |

**结论**：先做本计划（struct 字段默认值），它通用、便宜、零函数机制改动。函数默认参数
（让 `opts` 参数本身也能省略、调用更轻）作为**可选的后续**，两者叠加即得 Zig+Python 的全部
人体工学。**档 3（Julia `;` keyword-only）不做**（LS 无多重分派，动机不成立）。

---

## 9. 未来扩展

- **第 2 步**：可构造表达式默认（空容器、嵌套 struct、枚举）。
- **函数级默认参数**：见 §8，独立立项；若做，建议 Python 式（位置默认尾随 + 调用点具名），
  使 `fn f(..., Opts opts = Opts{})` 里 opts 可省略。
- **enum 变体 payload 默认**、**const 默认值引用**：远期，按需。
