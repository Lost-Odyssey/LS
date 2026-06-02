# Plot 基础设施实现计划（Plot Infrastructure Plan）

> **日期**：2026-06-02
> **状态**：📋 规划中（未开始）
> **背景**：评审 [docs/plan_plot.md](plan_plot.md) 后发现其大量假设了 LS 当前不支持的语言特性。
> 本文档梳理 plot 真正需要的基础设施，并制定零风险优先的分层实现计划。
> 关联：[docs/plan_plot.md](plan_plot.md)（原始设计）

---

## 0. 评审结论

`plan_plot.md` 的**设计本身**（架构、刻度算法、SVG/Text 双后端、HT 配色）是高质量且有价值的，
但它**直接照搬了 LS 不支持的语法/特性**，若原样实现会在 Phase 1 第一天即卡死。

**核心判断**：plot 所需能力约 **80% 已经存在**（`std/strconv`、`math` intrinsics、string 方法、
vec(struct)/嵌套容器 drop 都已就绪）。真正要建的是**一层纯 LS 适配代码 + 2 个可选语言增强**。
绝大多数「缺口」是**写法约束**，不是能力缺失。

---

## 1. plot 依赖能力盘点（实测对照）

| plan_plot.md 用法 | LS 现状 | 缺口归类 |
|---|---|---|
| 定点格式化 `f"{v:.1f}"` | ❌ f-string 无 `:` spec；✅ `strconv.float_fixed(v,1)` 已存在（[strconv.ls:104](../std/strconv.ls)） | 改写即可 / 可选增强 |
| 科学计数法 `f"{m:.2f}e{exp:.0f}"` | ⚠️ 可用 `float_fixed` 拼 | 建 helper |
| `floor/ceil/log10/pow/abs/sqrt/sin/cos` | ✅ 全是 LLVM intrinsic（[builtins_math.c:34-48](../src/builtins_math.c)） | 无 |
| `min(a,b)/max(a,b)`（f64） | ✅ **已存在**（[builtins_math.c:31-32](../src/builtins_math.c)，`llvm.minnum/maxnum.f64`，多态 int/f64） | 无（实测通过） |
| string `join/split/substr/copy/append/at` | ✅ 全有（[codegen.c:6411](../src/codegen.c) 等） | 无 |
| `f64 as int` 窄化 | ✅ `FPToSI`（[codegen.c:12838](../src/codegen.c)） | 无 |
| vec(struct)、嵌套 vec、struct 含 vec 字段自动 drop | ✅ 近期刚修（L-011a/b、has_drop struct 所有权） | 无（但重度使用） |
| 命名 struct 字面量 `T{a:1}` | ✅ parser 支持 | 需冒烟验证嵌套 push |
| **函数重载** `Axes_plot`×3、`cpu_timeline`×2 | ❌ 明确不支持（[checker.c:574](../src/checker.c)） | 改写：改名 |
| **借用返回** `add_subplot -> &Axes` | ❌ 未实现，需生命期系统 | 改写：返回 int 索引 |
| **元组类型/解构** `-> (..)`、`(r,g,b)=` | ❌ 无 `TYPE_TUPLE` | 改写 or 建语言特性 |
| **`elif` / `if c => stmt`** | ❌ `elif` 非关键字；`=>` 仅 match | 改写：`else if {}` |
| **`const X = ...`** | ❌ 仅普通全局变量 | 改写 or 建语言特性 |
| 裸 `floor(...)` 不带 `math.` | ⚠️ math 是模块 | 改写：加 `math.` 前缀 |

---

## 2. 基础设施分三层

### Tier 0 — 纯 LS 适配层（必做，零编译器改动，零风险）

新建 **`std/plotfmt.ls`**（或并入 strconv），把所有「不支持的语法」封装掉：

```ls
fn fmt_fixed(f64 v, int d) -> string         // 包 strconv.float_fixed
fn fmt_auto(f64 v) -> string                 // 按 |v| 量级自动选小数位（替代 _format_tick 自动档）
fn fmt_sci(f64 v) -> string                  // 科学计数法，float_fixed 拼 e 指数
fn fmt_time(i64 ns) -> string                // ns/us/ms/s 自动单位
fn pad_left(string s, int w) -> string       // 右对齐补空格（替代手写 while）
fn pad_right(string s, int w) -> string
fn clamp_i(int v, int lo, int hi) -> int     // f64 min/max 直接用 math.min/max（已存在）
fn clamp_f(f64 v, f64 lo, f64 hi) -> f64
fn rgb_to_hex(int r, int g, int b) -> string // int_to_hex + 补零到 2 位
fn hsv_to_hex(f64 h, f64 s, f64 v) -> string // 纯 LS HSV->RGB->hex（HT 配色用）
```

> 这一层是 plot 能落地的真正前提，且本身就是可独立测试、可独立交付、对其它纯 LS 模块通用的成果。

### Tier 1 — 小语言增强（可选，低成本，各 0.5~1 天）

| 项 | 价值 | 成本 | 建议 |
|---|---|---|---|
| `math.min/max`（f64，2 元） | 通用，plot 高频 | 极低（照 `pow` 加两行表项，intrinsic `llvm.minnum/maxnum.f64`） | ✅ 顺手做 |
| `const` 声明 | 语义清晰 | 中（scanner+parser+checker 只读标记） | ⚠️ 可跳过，用全局变量替代 |

### Tier 2 — 大语言增强（可选，高价值，应独立立项）

| 项 | 价值 | 成本 | 建议 |
|---|---|---|---|
| **f-string 格式说明符** `{expr:.Nf}` | 全语言通用，plot 体验提升最大；runtime 零改动 | ~1 天（详见 §4） | ✅ **已完成**（Phase 0.3） |
| **元组类型 + 解构** | 通用 | 高（types/parser/codegen/drop 全链路） | ❌ plot 不必依赖，用 struct 替代；要做须单独立项 |

---

## 3. 推荐实现计划（分阶段）

### Phase 0 — 基础设施（2~3 天）*先于任何 plot 代码*

| 步骤 | 内容 | 验收 |
|---|---|---|
| 0.1 | ~~`math.min/max` 加 intrinsic~~ ✅ **已存在**（[builtins_math.c:31-32](../src/builtins_math.c)，实测 `math.min(1.0,2.0)==1.0` / `math.min(7,3)==3` 通过） | — |
| 0.2 | ~~新建 `std/plotfmt.ls`~~ ✅ **已完成**（`std/plotfmt.ls`：fmt_fixed/auto/sci/time、pad_left/right、clamp_i/f、rgb_to_hex、hsv_to_hex；`tests/samples/plotfmt_test.ls` 27 项 + 通用驱动 `tests/test_plotfmt.cmake`；JIT+AOT+memcheck，全量 **117/117**） | ✅ |
| 0.3 | ~~f-string `:` spec~~ ✅ **已完成**（token+scanner+ast+parser+codegen 共 6 文件，runtime 零改动；浮点 `.Nf/.Ne/.Ng`、整型 `Nd/0Nd/x`、int→double 自动加宽、i64 自动 `ll`、空 spec 回退默认；两条 codegen 路径抽 `cg_fstring_emit_arg` 共用；非数值/浮点用整型 spec 报 `cg_error`。`tests/samples/fstring_spec_test.ls` 12 项；JIT+AOT+memcheck，全量 **118/118**） | ✅ |

> 做了 0.3 后，plot 代码可直接写 `f"{px:.1f}"`，贴近原计划；不做则全程用 `fmt_fixed(px,1)`，可读性差但完全可行。

### Phase 1 — 改写 plan_plot.md 为「可编译版」 ✅ 已完成（2026-06-02）

产出 **`std/plot.ls`** 骨架（数据模型 + builder API + 可验证输出桩）。机械改写：

- 重载 → 改名：`plot` / `plot_xy` / `plot_styled` / `scatter` / `bar`
- `-> &Axes` → **Axes 作为值构建，`add_axes(&!Figure, Axes)` 把它 MOVE 进 Figure**
  （比"返回 int 索引"更稳：避开对 vec 元素字段的可变深链访问）
- 元组 → struct / 直接字段；`const COLORS` → `color_at(int) -> string` 调色板函数
- `elif`/`=>` → 标准 `else if {}`；`floor/sin/...` → `math.` 前缀；格式化走 `std/plotfmt`

**数据结构**：`LineStyle`（xs/ys/color/label/linewidth/is_scatter）、`BarSeries`、
`Axes`（vec(LineStyle) + vec(BarSeries) + 标题/范围/legend/grid）、`Figure`（vec(Axes) + 尺寸）。

**关键验证（所有权压力点全部 memcheck 干净）**：
- 深层嵌套 has_drop：`Figure → vec(Axes) → vec(LineStyle) → vec(f64)` + 多个 string 字段
- `add_axes` 把深层嵌套的 Axes **MOVE** 进 `fig.axes_list`
- `Axes ax = fig.axes_list[i]` **深拷读取** has_drop struct
- 命名 struct 字面量把 `vec(f64)` 参数 **move** 进字段

**踩坑记录（写 Phase 2 注意）**：
- 空 vec 必须经 typed 局部：`vec(Line) ls = []` 再放进 struct 字面量；`{ field: [] }` 直接写会
  "cannot infer element type"。
- 只读借用是 **auto-borrow**：传 `fig`（不是 `&fig`）；写 `&fig` 反而类型不匹配（`*Figure` vs `&Figure`）。

**验收**：`tests/samples/plot_skeleton_test.ls`（`test_plot_skeleton`，MARKER=PLOT）
构建双 Axes（线图 + 柱状）→ MOVE 进 Figure → 校验 `show_text` 摘要 + `to_svg` 良构；
JIT+AOT+memcheck，全量 **119/119**。

### Phase 2 — 刻度引擎落地 ✅ 已完成（2026-06-02）

在 `std/plot.ls` 落地：`_nice_number`（**标准 Heckbert**）/ `generate_ticks` / `map_x` / `map_y`
/ `update_limits` / `finalize`（边距 + 重算刻度，Axes 新增 `xticks/yticks` 字段）。

> ⚠️ **算法修正**：plan_plot.md §4.2 的伪代码把 Heckbert 的 round/ceil 两个分支体**写反了**，
> 导致其示例表内部不自洽（无任何单一算法能同时产出全部 5 行）。本实现用教科书 Heckbert
> （`range = nice(span, ceil)` → `step = nice(range/(n-1), round)`），精确匹配表中第 2/3/4 行；
> 第 1 行实得 `[2,4]`（表理想化为 `[1..5]`）、第 5 行实得 `[0,0.005,0.010]`（表多出范围外的 0.015）。
> 断言锁定算法**实际输出**。

**关键修复（memcheck 驱动）**：
- `generate_ticks` 归一化 `-0.0 → 0.0`（`math.ceil(-0.15)` 产负零 → 标签 `-0.000`）。
- **踩坑（Phase 3 必读）**：把 struct 的 **vec 字段直接传给函数**（即便 `&vec` 借用）会产生别名
  temp → **double-free**。安全 idiom：传 `ax.xticks.copy()`（独立所有权，move 进按值参数，释放一次）；
  或在函数内直接按下标读 `ax.xticks[i]` / `ax.xticks.length`（如 `update_limits` 读 `ln.xs[k]`，干净）。
- `end` 是保留字 → 局部变量改名 `stop`。

**验收**：`tests/samples/plot_ticks_test.ls`（`test_plot_ticks`，MARKER=TICKS）：5 组 `generate_ticks`
+ 4 组 `map_x/map_y` + `finalize` 的 limits/margins/ticks 全 `assert_eq`；JIT+AOT+memcheck，全量 **120/120**。

> 之后进 SVG 后端 / Text 后端 / 特殊图，按原计划 Phase 2~4 推进，每个 `.ls` 测试均过 AOT+JIT+`--memcheck` 三重。

---

## 4. f-string 格式说明符（Phase 0.3）详细方案

### 4.1 架构契合点

f-string codegen **本来就是 printf-based**：编译期拼 `fmt_buf`，运行期调 `sprintf`。
加 `:spec` 本质只是「把某插值的默认 `%f` 换成用户给的 `%.2f`」，**runtime 不动一行**。

### 4.2 精确改动点（6 文件）

| 文件 | 改动 | 行数 | 说明 |
|---|---|---|---|
| [token.h:124](../src/token.h) | 加 `TOKEN_FSTRING_SPEC` | +1 | 承载 spec 文本 |
| [scanner.c:527](../src/scanner.c) | brace 跟踪块加分支：`depth==1 && *cur==':'` → 进入 spec 扫描，读到 `}`，emit `FSTRING_SPEC` 并把 depth 归 0 | ~20 | 见 §4.3 |
| [ast.h:189](../src/ast.h) | `format_string` 加 `char **specs;`（与 `exprs` 平行，NULL=无 spec） | +1 | |
| ast.c | 构造/`ast_free` 同步分配释放 `specs` | ~10 | |
| [parser.c:309](../src/parser.c) | 解析完 expr 后：`if check(FSTRING_SPEC){ specs[i]=text; advance; } else consume(RBRACE)` | ~15 | RBRACE 已被 scanner 消费 |
| [codegen.c:4955](../src/codegen.c) **和** [codegen.c:4758](../src/codegen.c) | spec→printf 转换 + int/float 强制转换；**两处都改**，抽 helper `emit_fstring_conv()` 共用 | ~45 | 见 §4.4 |

### 4.3 为什么 scanner 在 `:` 拦截不会误伤合法表达式

插值里唯一会出现 `:` 的合法语法是 struct 字面量 `f"{Point{x:1}}"`，但那个 `:` 在 `fstring_brace_depth==2`
（内层 `{` 内）。LS 无三元 `?:`。所以**只在 depth 恰好 ==1 时把 `:` 当 spec 分隔符**，struct 字面量
的冒号天然在 depth≥2，不受影响。这是个干净的不变量。

期望 token 序列（`f"v={x:.2f}!"`）：
```
FSTRING_START · FSTRING_TEXT "v=" · LBRACE(depth1) · IDENT x ·
FSTRING_SPEC ".2f"（消费 ':'…'}'，depth->0） · FSTRING_TEXT "!" · FSTRING_END
```

### 4.4 必须改两条 codegen 路径（否则行为不一致）

- `string s = f"{x:.2f}"` 走 [codegen_format_string:4955](../src/codegen.c)
- `print(f"{x:.2f}")` 走 [print 快路径:4758](../src/codegen.c)（为省一次建串直接拼 printf 参数）

两处都用 `printf_fmt_for_type(et)` 拼 spec。**抽一个 helper 同时给两边调**，避免漂移。
这是本特性唯一容易埋坑处。

### 4.5 spec→printf 翻译策略（按结尾转换字符 + 操作数类型驱动）

```
spec 以 f/F/e/E/g/G 结尾  → 浮点转换。操作数若是 int → 插 SIToFP->double；fmt = "%"+spec
spec 以 d/i/x/X/o/u 结尾  → 整型转换（i64 需 %ll 长度修饰，见限制）
spec 无转换字符（如 ".2"） → 按操作数类型补 f 或 d
校验：spec 仅允许 [0-9.+\-# fFeEgGdioxXuU]，含 '%' 直接报错
```
覆盖 plot 全部所需：`{px:.1f}` `{exp:.0f}` `{val:.2f}` `{n:03d}`。

### 4.6 工时与范围

| 范围 | 内容 | 工时 |
|---|---|---|
| **v1（plot 够用）** | 浮点 spec `.Nf/.Ne/.Ng` + 基本整型宽度/补零 `Nd/0Nd` + int->double 自动转 + 测试 | **~1 天（6–8h）** |
| v2（完整对标） | i64 长度修饰、字符串对齐填充 `>8`/`<8`/`^8`、二进制 `b` | 额外 ~1 天 |

**建议只做 v1**。对齐填充用 `plotfmt.pad_left` 纯 LS 更直观；i64 spec 几乎不用（时间格式走 helper）。

### 4.7 风险

1. **i64 + 整型 spec**：`{bignum:08d}` 因缺 `%ll` 会截断 → v1 文档标注为已知限制，plot 不踩。
2. **fmt_buf 固定 1024**：spec 拼接是编译期 C 字符串操作，沿用现有 `< 1020` 边界检查，无新风险。
3. **零运行期/drop 影响**：spec 是 AST 里的编译期 C 字符串，`ast_free` 释放；插值值 codegen/所有权路径不变。
4. **两路径漂移**：靠抽 helper 消除（已计入工时）。

### 4.8 验收

```
test_fstring_spec.c            单测：fmt_buf 拼接正确性
tests/samples/fstring_spec.ls：
  assert f"{3.14159:.2f}" == "3.14"
  assert f"{42:.0f}"      == "42"    // int->float
  assert f"{7:03d}"       == "007"
  边界：负数、0、空 spec {x:}、struct 字面量 {P{a:1}} 不被误判
三重：AOT + JIT + --memcheck 0 leak；回归 ctest 116/116 不退
```

---

## 5. 风险提示（plot 实现期）

1. **plot 是所有权系统的压力测试靶**：`vec(LineStyle)`（struct 含多个 vec(f64)）、`vec(string)` 字符网格
   反复 `grid[row] = substr+ch+substr`、函数间传 `&!vec(string)` —— 会密集触发刚修过的 has_drop struct
   字段所有权、嵌套 vec drop、字符串临时泄漏路径。**建议 Phase 2 一落地就 memcheck**，把所有权 bug 在小
   范围暴露，而非堆到 SVG 阶段。
2. **不要在 plot 里依赖 REPL**：L-010 限制下 has_drop struct 跨 snippet 反复析构会段错误；plot 测试一律
   走 `ls run` / `ls compile`。

---

## 6. 执行顺序建议

```
Phase 0.1  math.min/max            ✅ 已存在（实测通过）
Phase 0.2  std/plotfmt.ls + 测试    ✅ 已完成（test_plotfmt）
Phase 0.3  f-string : spec          ✅ 已完成（test_fstring_spec，118/118）
   ↓
Phase 1    std/plot.ls 可编译骨架     ✅ 已完成（test_plot_skeleton）
Phase 2    刻度引擎 + assert_eq 验收   ✅ 已完成（test_plot_ticks，120/120）
   ↓
原计划 Phase 2~4：SVG / Text / 特殊图   ← 下一步（先 SVG 折线图）
```

> **Phase 0 全部完成（2026-06-02）**。基础设施就位：plot 代码现在可直接写
> `f"{px:.1f}"`、`{n:03d}`，数值/颜色格式化走 `std/plotfmt`，f64 min/max 走 `math`。
> 下一步进入 Phase 1：把 `plan_plot.md` 机械改写为可编译的 `std/plot.ls` 骨架。
