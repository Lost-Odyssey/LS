# Plot 模块 — 数据可视化框架

> **日期**：2026-05-30（初稿）
> **实现方式**：纯 LS（零 C 依赖，零编译器改动）
> **后端**：SVG（矢量输出）+ Text/Unicode（终端显示）
> **当前进度**：❌ 未开始

---

## 0. 背景与目标

LS 目前可以处理数值计算（math / FFT 即将加入）、信号处理、文件 I/O，但**无法可视化结果**。数据只能 `print()` 裸数字，无法直观观察趋势、频谱峰值、性能瓶颈。

### 目标（对标 MATLAB 日常使用子集）

| 能力 | 优先级 | 说明 |
|------|:------:|------|
| 2D 线图 + 散点 + 柱状 + 填充 | ★★★★★ | 信号处理、频谱、公式曲线 |
| 自动范围 + 刻度 + 图例 | ★★★★★ | 数据自适应，无需手动指定 |
| 子图布局 | ★★★★☆ | 对比多组数据 |
| 对数坐标 | ★★★☆☆ | 频谱图 dB 刻度 |
| 颜色映射 + 色条 | ★★★☆☆ | 频谱图热度 |
| 火焰图 | ★★★★☆ | perf 调用栈分析 |
| 甘特图 / 时间线 | ★★★★☆ | 多线程调度分析 |
| 流程图 / 调用图 | ★★★☆☆ | 函数调用关系 |
| **SVG 输出** | ★★★★★ | 静态矢量图，浏览器/论文 |
| **Text/Unicode 输出** | ★★★★★ | 终端即看，零依赖 |

---

## 1. 架构

```
┌───────────────────────────────────────────────┐
│  User Code                                    │
│  plot.Figure() → axes.plot() → fig.save_svg() │
├───────────────────────────────────────────────┤
│  std/plot.ls — 核心模型                        │
│    Figure / Axes / LineSeries / BarSeries      │
│    数据管理、自动缩放、刻度生成、图例           │
├──────────────────────┬────────────────────────┤
│  SVG 后端 (内联)     │  Text 后端 (内联)       │
│  • 矢量图形           │  • ▁▂▃▄▅▆▇█ 直方      │
│  • 出版质量输出        │  • ╱╲─│ 折线         │
│  • 浏览器/LaTeX 可用   │  • 即时终端显示        │
└──────────────────────┴────────────────────────┘
```

纯 LS 实现，两个后端都在 `std/plot.ls` 内部（内联渲染函数），不拆分文件。

---

## 2. 核心数据结构

```ls
// ---- 线条样式 ----
struct LineStyle {
    vec(f64) xs
    vec(f64) ys
    string color       // 十六进制 "#e6194b"
    string label
    string linestyle   // "-"  "-."  "--"  ":"
    f64 linewidth      // SVG px
    string marker      // "none"  "circle"  "square"  "diamond"
    f64 marker_size
}

// ---- 填充区域 ----
struct FillBetween {
    vec(f64) xs
    vec(f64) y1
    vec(f64) y2
    string color
    f64 alpha          // 0.0 ~ 1.0
}

// ---- 柱状图 ----
struct BarSeries {
    vec(string) labels
    vec(f64) values
    string color
    string label
    f64 width          // 柱宽比例 0.0~1.0，默认 0.8
}

// ---- 坐标轴 ----
struct Axes {
    vec(LineStyle) lines
    vec(FillBetween) fills
    vec(BarSeries) bars

    string title
    string xlabel
    string ylabel

    // 视图范围（自动检测 + 手动覆盖）
    bool auto_scale      // 默认 true
    f64 xmin, xmax
    f64 ymin, ymax
    bool xscale_log
    bool yscale_log
    f64 xmargin          // x 方向边距比例，默认 0.05
    f64 ymargin          // y 方向边距比例，默认 0.05

    // 刻度（自动生成 + 手动覆盖）
    vec(f64) xticks
    vec(f64) yticks
    vec(string) xtick_labels
    vec(string) ytick_labels
    int tick_label_decimals   // 小数位数，-1=自动
    bool grid_visible
    bool xaxis_visible
    bool yaxis_visible

    // 图例
    bool legend_visible
    string legend_loc   // "best" "upper_right" "upper_left"
                        // "lower_right" "lower_left" "outside_right_top"
}

// ---- 画布 ----
struct Figure {
    vec(Axes) axes_list
    int rows, cols
    int svg_width, svg_height    // 像素
    int text_width, text_height  // 字符数
    string background_color      // SVG 背景色 "#ffffff"
}

// ---- 事件（甘特图用） ----
struct TimelineEvent {
    i64 start_ns
    i64 end_ns
    string thread_name
    string label
    string color
}

// ---- CPU 调度事件（perf 时间线用） ----
struct CpuSchedEvent {
    i64 start_ns
    i64 end_ns
    int thread_id
    string thread_name
    int cpu_id              // 逻辑 CPU 编号（如 0~63）
    string process_name     // 可选，用于多进程
}

// ---- 物理 core 映射（Hyper-Threading 描述） ----
struct CpuTopology {
    int total_logical      // 逻辑 CPU 总数（如 64）
    int total_physical     // 物理 core 总数（如 32）
    int threads_per_core   // 每 core 线程数（= total_logical / total_physical）
    // 物理 core 的编号可通过 cpu_id / threads_per_core 获得
}

// ---- 调用图节点/边 ----
struct CallGraphNode {
    string name
    string label
    f64 weight          // 热度值（决定颜色深浅）
}
struct CallGraphEdge {
    string from
    string to
    int call_count
}
```

---

## 3. 坐标系统

### 3.1 三层坐标

```
数据坐标  [xdata, ydata]    用户空间，f64
    ↓ 映射
像素坐标  [xpixel, ypixel]   SVG 画布像素，f64
    ↓ 量化
网格坐标  [col, row]         Text 后端字符网格，int
```

### 3.2 SVG 坐标映射

```
┌─────────────────────────────────────────────────────┐
│ (0,0)                    svg_width                   │
│           ┌─────────────────────────┐                │
│           │    plot_area            │                │
│  margin   │   (left,top)            │   margin       │
│           │        width×height     │                │
│           │                         │                │
│           └─────────────────────────┘                │
│ (0,svg_height)                                       │
└─────────────────────────────────────────────────────┘
```

```ls
// 布局参数（根据 Figure size 和 margin 比例计算）
fn _compute_layout(Figure fig) -> (int, int, int, int) {
    int margin_ratio = 12   // 每侧边距占较大尺寸的 12%
    int margin = min(fig.svg_width, fig.svg_height) * margin_ratio / 100
    int left  = margin
    int top   = margin
    int right = fig.svg_width  - margin
    int bottom = fig.svg_height - margin
    // 为 y 轴标签留额外空间
    left = left + 50
    // 为 x 轴标签留额外空间
    bottom = bottom - 40
    return (left, top, right - left, bottom - top)
}

// 数据坐标 → SVG 像素（线性坐标）
fn _map_x(f64 x, f64 xmin, f64 xmax, int left, int width) -> f64 {
    return left + (x - xmin) / (xmax - xmin) * width
}
fn _map_y(f64 y, f64 ymin, f64 ymax, int top, int height) -> f64 {
    return top + height - (y - ymin) / (ymax - ymin) * height
    // 注意 SVG y 轴向下，数据 y 轴向上，所以翻转
}

// 数据坐标 → SVG 像素（对数坐标）
fn _map_x_log(f64 x, f64 xmin, f64 xmax, int left, int width) -> f64 {
    f64 log_min = log10(max(xmin, 1e-300))
    f64 log_max = log10(max(xmax, 1e-300))
    f64 log_x = log10(max(x, 1e-300))
    return left + (log_x - log_min) / (log_max - log_min) * width
}
// _map_y_log 同理
```

### 3.3 Text 网格坐标映射

```ls
fn _map_x_text(f64 x, f64 xmin, f64 xmax, int cols) -> int {
    f64 frac = (x - xmin) / (xmax - xmin)
    int col = (frac * cols) as int
    return clamp(col, 0, cols - 1)
}
fn _map_y_text(f64 y, f64 ymin, f64 ymax, int rows) -> int {
    f64 frac = (y - ymin) / (ymax - ymin)
    int row = rows - 1 - (frac * rows) as int  // 翻转：数据大 = 行号小
    return clamp(row, 0, rows - 1)
}
```

### 3.4 相同数据不同后端的表现

例如 sine 波 32 点：

```
SVG (800×400):                                       Text (70×20):
  1.0 ┤     ╭──╮         ╭──╮         ╭─             ██ ██ ██
      ┤    ╭╯  ╰╮       ╭╯  ╰╮       ╭╯              ██ ██ ██ ██
  0.5 ┤   ╭╯    ╰╮     ╭╯    ╰╮     ╭╯           ██████ ██ ██ ██ ██████
      ┤  ╭╯      ╰╮   ╭╯      ╰╮   ╭╯     ██████████████████████████████
  0.0 ┤─╭╯        ╰──╭╯        ╰──╭╯──────                     ██
      ┤╭╯            ╰╯           ╰╯
 -0.5 ┤╯
      ┤
 -1.0 ┤───┬───┬───┬───┬───┬───┬───
         0   5   10  15  20  25  30
```

左侧 SVG 连续、精细；右侧 Text 粗糙但概览可用。

---

## 4. 自动缩放与刻度生成（核心算法）

### 4.1 流程

```
添加数据
    ↓
Axes.update_limits()
    ├─ 遍历所有 lines / fills / bars
    ├─ 找整体 [xmin, xmax], [ymin, ymax]
    ├─ 空数据 → [0, 1]
    └─ 存为 raw_xmin, raw_xmax, raw_ymin, raw_ymax
    ↓
Axes.auto_scale()
    ├─ 如 auto_scale == false → 跳过（使用手动 set_xlim/ylim）
    ├─ 扩展 5% 边距
    │   dx = (raw_xmax - raw_xmin) * xmargin
    │   self.xmin = raw_xmin - dx
    │   self.xmax = raw_xmax + dx
    │   （对数坐标不加边距）
    ├─ 特殊规则：
    │   ├─ 若 raw_xmin > 0 → xmin 保持 raw_xmin - dx*0.5（不强制归零）
    │   ├─ 若 raw_xmin == 0 → xmin 保持 0
    │   ├─ 若 raw_xmax == 0 → xmax = 1
    │   └─ bar 模式 → xmin = -0.5, xmax = (N条 - 1) + 0.5
    │
    ├─ 生成刻度
    │   self.xticks = _generate_ticks(self.xmin, self.xmax, 6)
    │   self.yticks = _generate_ticks(self.ymin, self.ymax, 6)
    │
    └─ 根据刻度反向调整显示范围
        self.xmin = min(self.xmin, min(self.xticks))
        self.xmax = max(self.xmax, max(self.xticks))
```

### 4.2 `_generate_ticks()` — 核心刻度生成

参数：`lo`, `hi`, `max_ticks`（目标最多几个刻度）

```ls
fn _nice_number(f64 v, bool round) -> f64 {
    if v == 0.0 { return 0.0 }
    f64 sign = 1.0
    if v < 0.0 { sign = -1.0; v = -v }
    f64 exp = floor(log10(v))
    f64 frac = v / pow(10.0, exp)     // 归一化到 [1, 10)
    f64 nice
    if round {
        // 向上取整到最近的"好看"值
        if frac <= 1.0       => nice = 1.0
        else if frac <= 2.0  => nice = 2.0
        else if frac <= 5.0  => nice = 5.0
        else                   => nice = 10.0
    } else {
        // 向下取整
        if frac < 1.5        => nice = 1.0
        else if frac < 3.0   => nice = 2.0
        else if frac < 7.0   => nice = 5.0
        else                   => nice = 10.0
    }
    return sign * nice * pow(10.0, exp)
}

fn _generate_ticks(f64 lo, f64 hi, int max_ticks) -> vec(f64) {
    vec(f64) ticks = []
    f64 range = _nice_number(hi - lo, false)
    f64 step = _nice_number(range / (max_ticks - 1), true)
    if step == 0.0 { ticks.push(0.0); return ticks }

    f64 start = ceil(lo / step) * step
    f64 end   = floor(hi / step) * step

    f64 v = start
    while v <= end + step * 0.0001 {   // 浮点容差
        ticks.push(v)
        v = v + step
    }
    return ticks
}
```

**效果示例**：

| 原始范围 | step 计算 | 输出刻度 |
|---------|-----------|---------|
| [0.37, 5.82] | `5.45 → 5 / (6-1)=1.09 → nice=1` → step=1 | [1, 2, 3, 4, 5] |
| [0.03, 0.17] | `0.14 → 0.2 /5=0.04 → nice=0.05` | [0.05, 0.10, 0.15] |
| [12.3, 98.7] | `86.4 → 100/5=20 → nice=20` | [20, 40, 60, 80] |
| [-2.7, 1.3] | `4.0 → 5/5=1 → nice=1` | [-2, -1, 0, 1] |
| [0.0, 0.012] | `0.012 → 0.02/5=0.004 → nice=0.005` | [0, 0.005, 0.01, 0.015] |

### 4.3 刻度标签格式化

```ls
fn _format_tick(f64 v, int decimals) -> string {
    if decimals >= 0 {
        // 固定小数位
        string fmt = f"{v}"
        // 取到指定小数位（硬截断或四舍五入）
        ...
        return formatted
    }

    // 自动小数位
    // 规则：根据 step 值决定小数位数
    f64 abs_v = abs(v)
    if abs_v >= 1000.0   => return f"{v:.0f}"       // 无小数
    if abs_v >= 10.0     => return f"{v:.1f}"       // 1 位小数
    if abs_v >= 0.1      => return f"{v:.2f}"       // 2 位小数
    if abs_v >= 0.001    => return f"{v:.4f}"       // 4 位小数
    else                 => return f"{v:.6f}"       // 6 位小数
}

// 科学计数法（绝对值太大或太小时自动切换）
fn _format_tick_sci(f64 v) -> string {
    f64 abs_v = abs(v)
    if abs_v == 0.0 { return "0" }
    if (abs_v >= 10000.0) || (abs_v <= 0.001) {
        // 使用科学计数法如 1.23e4
        f64 exp = floor(log10(abs_v))
        f64 mantissa = v / pow(10.0, exp)
        return f"{mantissa:.2f}e{exp:.0f}"
    }
    return _format_tick(v, -1)
}
```

### 4.4 对数坐标刻度

```ls
fn _generate_log_ticks(f64 lo, f64 hi) -> vec(f64) {
    // 在对数坐标下生成 1, 2, 3, ..., 10, 20, 30, ..., 100, ...
    vec(f64) ticks = []
    int decade_start = ceil(log10(lo)) as int
    int decade_end   = floor(log10(hi)) as int
    for int d = decade_start; d <= decade_end; d = d + 1 {
        for int m = 1; m < 10; m = m + 1 {
            f64 v = m * pow(10.0, d)
            if v >= lo - lo * 0.01 && v <= hi + hi * 0.01 {
                ticks.push(v)
            }
        }
    }
    return ticks
}
// 对于较宽范围只标注 [1, 2, 5] × 10ⁿ 的主刻度
// 窄范围标注所有 1~9 × 10ⁿ
```

---

## 5. SVG 后端

### 5.1 布局

对于单个子图（以 800×500 为例）：

```
┌──────────────────────────────────────────────────────────┐
│  title (居中, 20px)                                       │  y=20
│                                                          │
│  ┌──┬────────────────────────────────────┬──┐            │
│  │  │                                    │  │            │
│  │y │          plot_area                 │  │            │
│  │l │     left=80, top=50                │  │            │
│  │a │     width=640, height=400          │  │            │
│  │b │                                    │  │            │
│  │  │                                    │  │            │
│  │  │                                    │  │            │
│  └──┴────────────────────────────────────┴──┘            │
│             xlabel (居中)                                 │  y=480
└──────────────────────────────────────────────────────────┘
  x=0                                                       x=800
```

多子图时均分，每个子图递归使用同一布局算法。

### 5.2 SVG 元素映射

| 图形 | SVG 元素 | 属性 | 坐标生成 |
|------|----------|------|---------|
| 折线 | `<polyline>` | `points="x1,y1 x2,y2 ..."` | 每点 `_map_x,_map_y` |
| 散点 | `<circle>` | `cx cy r` | 每点一个 circle |
| 散点(方) | `<rect>` | `x y width height` | 每点一个 rect |
| 柱状 | `<rect>` | `x y width height` | x=映射后中心-半宽，y=ymap(值)，height=ymap(0)-ymap(值) |
| 填充 | `<polygon>` | `points="x1,y1 ... xn,yn xn,y'n ... x1,y'1"` | 上缘正序+下缘反序闭合 |
| 网格 | `<line>` | `x1 y1 x2 y2` | 从每个 tick 延伸 |
| 轴 | `<line>` | `x1 y1 x2 y2` | 两条正交线 |
| 轴标签 | `<text>` | `x y text-anchor transform` | 刻度下 x→rotate，左 y→rotate -90 |
| 图例 | `<rect>`+`<line>`+`<text>` | 组合 | 自动定位到角落 |
| 标题 | `<text>` | `text-anchor="middle" font-size` | 顶部居中 |

### 5.3 SVG 样式体系

```svg
<style>
  .bg          { fill: #ffffff; }
  .plot-bg     { fill: #fafafa; }            <!-- 绘图区背景 -->
  .axis        { stroke: #333333; stroke-width: 1.2; }
  .grid        { stroke: #e0e0e0; stroke-width: 0.5; stroke-dasharray: 4,4; }
  .tick-label  { font-family: Consolas, monospace; font-size: 10px; fill: #555; }
  .axis-label  { font-family: Arial, sans-serif; font-size: 13px; fill: #333; }
  .title       { font-family: Arial, sans-serif; font-size: 16px; fill: #000; font-weight: bold; }
  .legend      { font-family: Arial, sans-serif; font-size: 11px; fill: #333; }
  .legend-box  { fill: #ffffff; stroke: #cccccc; stroke-width: 0.5; rx: 3; }
  .line-0      { fill: none; stroke: #4363d8; stroke-width: 2; }
  .line-1      { fill: none; stroke: #e6194b; stroke-width: 2; }
  .line-2      { fill: none; stroke: #3cb44b; stroke-width: 2; }
  <!-- 共 10 个颜色循环 -->
</style>
```

### 5.4 SVG 字符串构建

```ls
fn _svg_line(LineStyle s, int left, int top, int width, int height,
             f64 xmin, f64 xmax, f64 ymin, f64 ymax, int color_idx) -> string {
    vec(string) pts = []
    int n = min(s.xs.length, s.ys.length)
    for int i = 0; i < n; i = i + 1 {
        f64 px = _map_x(s.xs[i], xmin, xmax, left, width)
        f64 py = _map_y(s.ys[i], ymin, ymax, top, height)
        pts.push(f"{px:.1f},{py:.1f}")
    }
    return f"<polyline class=\"line-{color_idx}\" points=\"{' '.join(pts)}\"/>"
}
```

### 5.5 图例自动定位

```ls
fn _svg_legend(Axes ax, int left, int top, int width, int height) -> string {
    if !ax.legend_visible || ax.lines.length == 0 { return "" }
    // 计算图例绑定尺寸
    int entry_count = ax.lines.length
    int lx = left + width  - 150    // 默认右上角
    int ly = top + 20
    int lw = 140
    int lh = 20 + entry_count * 18
    // 如超出右边界 → 放左上角
    if lx + lw > left + width { lx = left + 10; ly = top + 20 }
    // 如也超出左边界 → 放左下
    ...
}
```

---

## 6. Text/Unicode 后端

### 6.1 字符网格

Text 后端的核心是一块 `vec(string)` 或二维 `array(string, H*W)` 字符网格。

```ls
// 创建字符网格（全部初始化为空格）
fn _make_grid(int w, int h) -> vec(string) {
    string row = ""
    for int j = 0; j < w; j = j + 1 { row = row + " " }
    vec(string) grid = []
    for int i = 0; i < h; i = i + 1 { grid.push(row.copy()) }
    return grid
}

// 在网格 (col, row) 写字符
fn _put_char(&!vec(string) grid, int col, int row, string ch) {
    if row < 0 || row >= grid.length { return }
    string r = grid[row]
    if col < 0 || col >= r.length { return }
    // 替换位置 col 的字符为 ch
    grid[row] = r.substr(0, col) + ch + r.substr(col + 1, r.length - col - 1)
}
```

### 6.2 数据渲染到网格

**线图** — Bresenham-like 线段光栅化：

```ls
fn _rasterize_line(vec(f64) xs, vec(f64) ys,
                    int grid_w, int grid_h,
                    f64 xmin, f64 xmax, f64 ymin, f64 ymax,
                    &!vec(string) grid) {
    int n = min(xs.length, ys.length)
    for int i = 0; i < n - 1; i = i + 1 {
        int c0 = _map_x_text(xs[i],   xmin, xmax, grid_w)
        int r0 = _map_y_text(ys[i],   ymin, ymax, grid_h)
        int c1 = _map_x_text(xs[i+1], xmin, xmax, grid_w)
        int r1 = _map_y_text(ys[i+1], ymin, ymax, grid_h)
        // DDA: 按步长填充中间单元格
        int steps = max(abs(c1 - c0), abs(r1 - r0))
        if steps == 0 {
            _put_char(grid, c0, r0, "█")
        } else {
            for int t = 0; t <= steps; t = t + 1 {
                int c = c0 + (c1 - c0) * t / steps
                int r = r0 + (r1 - r0) * t / steps
                // 根据斜率选择字符
                int dc = c1 - c0
                int dr = r1 - r0
                string ch
                if abs(dr) * 2 < abs(dc) { ch = "─" }     // 平坦→水平
                elif abs(dc) * 2 < abs(dr) { ch = "│" }    // 陡峭→垂直
                elif (dc > 0 && dr < 0) || (dc < 0 && dr > 0) { ch = "╱" }  // 斜向下
                else { ch = "╲" }   // 斜向上
                _put_char(grid, c, r, ch)
            }
        }
    }
}
```

**直方图/频谱** — 使用 8 级区块：

```ls
fn _rasterize_bars(vec(f64) ys, int grid_w, int grid_h,
                   f64 ymin, f64 ymax, &!vec(string) grid) {
    // X 方向每个数据点占一列
    int n = ys.length
    int cols_per_point = max(1, grid_w / n)
    for int i = 0; i < n; i = i + 1 {
        // 高度映射到字符行
        f64 frac = (ys[i] - ymin) / (ymax - ymin)
        int height_chars = (frac * grid_h) as int

        // 每行用 8 级区块微调精度
        int full_rows = height_chars / 8
        int partial = height_chars % 8
        string blocks[] = ["", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"]

        int col_start = i * cols_per_point
        for int j = 0; j < cols_per_point; j = j + 1 {
            int col = col_start + j
            if col >= grid_w { break }
            // 从底部往上画
            for int r = 0; r < full_rows; r = r + 1 {
                int row = grid_h - 1 - r
                _put_char(grid, col, row, "█")
            }
            if partial > 0 {
                int row = grid_h - 1 - full_rows
                _put_char(grid, col, row, blocks[partial])
            }
        }
    }
}
```

### 6.3 坐标轴与刻度

```ls
fn _render_axes_text(&!vec(string) grid, Axes ax, int grid_w, int grid_h,
                     int margin, int label_col, int label_row) {
    // y 轴竖线: 左边缘第 label_col 列
    int axis_col = label_col + 1
    for int r = margin; r < grid_h - margin; r = r + 1 {
        _put_char(grid, axis_col, r, "│")
    }

    // x 轴横线: 底边缘第 label_row 行
    int axis_row = grid_h - margin - 1
    for int c = axis_col + 1; c < grid_w - margin; c = c + 1 {
        _put_char(grid, c, axis_row, "─")
    }

    // 原点交叉 └
    _put_char(grid, axis_col, axis_row, "└")

    // y 轴刻度标签
    for int i = 0; i < ax.yticks.length; i = i + 1 {
        f64 v = ax.yticks[i]
        int row = _map_y_text(v, ax.ymin, ax.ymax, grid_h - 2*margin)
        row = row + margin
        string label = _format_tick(v, -1)
        // 右侧补齐到定宽（右对齐）
        while label.length < 8 { label = " " + label }
        // 写标签到 label_col 列
        for int j = 0; j < label.length; j = j + 1 {
            int c = label_col + j
            _put_char(grid, c, row, label.substr(j, 1))
        }
        // 刻度标记
        _put_char(grid, axis_col - 1, row, "┤")
    }

    // x 轴刻度标签
    for int i = 0; i < ax.xticks.length; i = i + 1 {
        f64 v = ax.xticks[i]
        int col = _map_x_text(v, ax.xmin, ax.xmax, grid_w - 2*margin - 1)
        col = col + axis_col + 1
        string label = _format_tick(v, -1)
        // 刻度标记
        _put_char(grid, col, axis_row + 1, "┬")
        // 标签文字（居中标在下一行）
        int label_start = col - label.length / 2
        for int j = 0; j < label.length; j = j + 1 {
            int c = label_start + j
            if c >= 0 && c < grid_w {
                _put_char(grid, c, axis_row + 2, label.substr(j, 1))
            }
        }
    }
}
```

### 6.4 输出最终字符串

```ls
fn _render_text(vec(string) grid) -> string {
    string result = ""
    for int i = 0; i < grid.length; i = i + 1 {
        // 去掉行尾多余空格
        string line = grid[i]
        while line.ends_with(" ") { line = line.substr(0, line.length - 1) }
        result = result + line + "\n"
    }
    return result
}
```

---

## 7. 特殊图表

### 7.1 火焰图

输入：调用栈采样列表，每行一个栈，深度不限。

```
fn flamegraph(vec(string) stacks, int width, int height, string output_path)
```

算法：
```
1. 解析每行调用栈: "main;parse;scan;next_token"
2. 构建栈频数树（map(string, Node)，每个 Node 含子节点 + 命中数）
3. 传遍树：每个节点宽度 = 命中数 / 总数 × 总宽度
4. SVG 输出：每个节点为 <rect>，y 深度=栈深度，x=累加偏移
           嵌入 <title>name (count)</title> 悬浮提示
5. 颜色：按深度渐变（红橙黄绿蓝）
```

### 7.2 CPU 调度时间线（Scheduling Timeline）

核心场景：perf / ETW / xperf 采样结果可视化。横轴 = 时间，纵轴 = 线程（泳道），每线程的水平条表示在哪个 CPU 上执行、执行了多久。

颜色编码反映 **Hyper-Threading 物理 core** 关系——同一物理 core 的两个逻辑 CPU 使用**相同色相、不同样式**显示。

#### 7.2.1 数据模型

```ls
// CPU 调度事件
struct CpuSchedEvent {
    i64 start_ns
    i64 end_ns
    int thread_id
    string thread_name
    int cpu_id              // 逻辑 CPU 编号 0~N-1
    string process_name     // 可选
}
```

#### 7.2.2 API

```ls
// 基本时间线（通用甘特图）
fn timeline(vec(TimelineEvent) events, int w, int h, string path) -> string

// CPU 调度时间线（带 Hyper-Threading 颜色编码）
fn cpu_timeline(vec(CpuSchedEvent) events, CpuTopology topo,
                int w, int h, string path) -> string

// 简化版本：自动检测线程数、CPU 数
fn cpu_timeline(vec(CpuSchedEvent) events, int total_logical,
                int total_physical, int w, int h, string path) -> string
```

`CpuTopology` 描述：

```ls
struct CpuTopology {
    int total_logical      // 逻辑 CPU 总数（如 64）
    int total_physical     // 物理 core 总数（如 32）
    int threads_per_core   // 每 core 线程数（= total_logical / total_physical）
}
```

物理 core 编号计算：`physical_core = cpu_id % total_physical`（或 `cpu_id / threads_per_core`）

#### 7.2.3 Hyper-Threading 颜色策略

核心规则——同一物理 core 的两个逻辑 CPU 共享**色相**，用**样式**区分：

```
物理 core 0  ─┬─ CPU 0  → 色相=0°  饱和度=60%  明度=70%  实心填充
              └─ CPU 32 → 色相=0°  饱和度=60%  明度=50%  斜线纹理

物理 core 1  ─┬─ CPU 1  → 色相=30° 饱和度=60%  明度=70%  实心填充
              └─ CPU 33 → 色相=30° 饱和度=60%  明度=50%  斜线纹理

...以此类推，32 色循环
```

HSV → Hex 转换算法（纯 LS 数学计算）：

```ls
fn _hsv_to_rgb(f64 h, f64 s, f64 v) -> (f64, f64, f64) {
    // h: 0~360, s: 0~1, v: 0~1
    f64 c = v * s
    f64 hp = h / 60.0
    f64 x = c * (1.0 - abs(hp % 2.0 - 1.0))
    // ... RGB 六边形映射
}

fn _hsv_to_hex(f64 h, f64 s, f64 v) -> string {
    (r, g, b) = _hsv_to_rgb(h, s, v)
    return f"#{r*255:.0f}{g*255:.0f}{b*255:.0f}"
}
```

样式实现：

| 逻辑 CPU 类型 | SVG 填充 | 效果 |
|-------------|----------|------|
| 首线程 (cpu_id < total_physical) | `fill="base_color"` | **实心** |
| 次线程 (cpu_id >= total_physical) | `fill="dim_color" stroke="base_color" stroke-dasharray="4,2"` | **斜线纹理**，边缘色相提示 |

或者更清晰的方式——使用 SVG `<pattern>` 纹理：

```svg
<defs>
  <!-- 为每个 core 生成两种 pattern -->
  <pattern id="ht-0" width="6" height="6" patternUnits="userSpaceOnUse"
           patternTransform="rotate(45)">
    <rect width="6" height="6" fill="#e6194b88"/>
    <line x1="0" y1="0" x2="0" y2="6" stroke="#e6194b" stroke-width="1"/>
  </pattern>
  <pattern id="ht-1" ...>
  ...
</defs>
<rect x="..." y="..." width="..." height="..." fill="url(#ht-0)"/>
```

#### 7.2.4 SVG 布局

```
┌────────────────────────────────────────────────────────────┐
│  标题: "CPU Scheduling Timeline"                           │
├────────┬───────────────────────────────────────────────────┤
│        │   time_ns                                         │
│ Thread ├─────┬──────┬──────┬──────┬──────┬──────┬──────┤  │
│        │     │      │      │      │      │      │      │  │
│ main   │█████████████░░░░░░░░░░██████████████             │  │
│ (TID=  │      CPU0-core0      CPU32-core0-HT             │  │
│  1234) │      first HT         second HT                 │  │
│        │     │      │      │      │      │      │      │  │
│ worker │░░░░░░░░░░██████████████░░░░░░░░░░░░░░░░         │  │
│ (TID=  │  CPU32-core0-HT     CPU1-core1-HT               │  │
│  5678) │                                                  │  │
│        │     │      │      │      │      │      │      │  │
│ worker2│░░░░░░░░░░░░░░░░░░░░░░██████████░░░░░░░░░░░░░     │  │
│ (TID=  │             CPU33-core1-HT   CPU33-core1-HT     │  │
│  9012) │                                                  │  │
│        │     │      │      │      │      │      │      │  │
├────────┴─────┴──────┴──────┴──────┴──────┴──────┴──────┤  │
│       10ms   20ms   30ms   40ms   50ms   60ms   70ms       │
├────────────────────────────────────────────────────────────┤
│  图例（CPU ID ↔ 颜色映射）                                 │
│  ┌─────────────────────────────────────────────────┐       │
│  │ Physical Core 0    ██ CPU 0    ▓▓ CPU 32 (HT)  │       │
│  │ Physical Core 1    ██ CPU 1    ▓▓ CPU 33 (HT)  │       │
│  │ Physical Core 2    ██ CPU 2    ▓▓ CPU 34 (HT)  │       │
│  │ ...                                            │       │
│  │ 仅显示事件中有出现的 CPU/物理 core               │       │
│  └─────────────────────────────────────────────────┘       │
└────────────────────────────────────────────────────────────┘
```

**关键规则**：
- 每条水平泳道 = 一个线程（线程名 + TID 标注在左侧）
- **Bar 上不标注 CPU ID**，只通过颜色和样式区分
- 实心 bar = 首线程（CPU 0~31，色相对应物理 core）
- 纹理/斜线 bar = 次线程（CPU 32~63，同色相但明度低 + 斜线覆盖）
- 悬浮 `<title>` 显示完整信息：`Thread main (TID 1234) on CPU 0 [Core 0, HT=0]`
- **图例独立**展示所有出现的 CPU 及其物理 core 归属，方便对照

#### 7.2.5 文本/Unicode 后端 — CPU 信息不显示

终端输出仅表示线程活动状态，不区分 CPU：

```ls
fn _render_timeline_text(vec(CpuSchedEvent) events, int width, int height) -> string {
    // 每线程一行，活跃期用 █ 表示，空闲段用空格
    // 不区分首次/次线程，不显示 CPU 编号
}
```

终端输出示例：

```
Time: 0ms         10ms        20ms        30ms        40ms
main    █████████████         ████████████████
worker           ████████████████
worker2                    ██████████
```

**文本后端只回答"哪个线程何时在运行"**，不尝试渲染 CPU 颜色——终端分辨率不足以支持 64 色区分。

#### 7.2.6 数据聚合选项

对于长时间跨度（秒级）的 perf 数据，原始事件可能过多。支持**聚合模式**：

```ls
// 按时间窗口聚合：计算每个时间片中每线程在哪个 CPU 上运行最久
fn cpu_timeline_aggregated(vec(CpuSchedEvent) events,
                           CpuTopology topo,
                           int time_window_ns,      // 聚合窗口（如 1ms）
                           int w, int h, string path) -> string
```

聚合后每格显示为主 CPU（占该窗口最大比例的那个），颜色按该 CPU 编号映射。

#### 7.2.7 时间轴格式化

```ls
fn _format_time(i64 ns, string unit) -> string {
    // 自动选择最合适的单位
    if unit == "auto" {
        if ns >= 1_000_000_000  => unit = "s"
        elif ns >= 1_000_000    => unit = "ms"
        elif ns >= 1_000        => unit = "us"
        else                     => unit = "ns"
    }
    f64 val = ns as f64
    if unit == "s"  => val = ns / 1_000_000_000.0; return f"{val:.2f}s"
    if unit == "ms" => val = ns / 1_000_000.0;    return f"{val:.1f}ms"
    if unit == "us" => val = ns / 1_000.0;        return f"{val:.0f}us"
    return f"{ns}ns"
}
```

刻度标签自动选择单位：总跨度 < 10μs → ns，< 10ms → μs，< 10s → ms，以上 → s。

### 7.3 调用图

输入：节点 + 有向边。

```ls
fn callgraph(vec(CallGraphNode) nodes,
             vec(CallGraphEdge) edges,
             int width, int height, string output_path)
```

算法：
```
1. 拓扑排序定层（level 分配）
2. 每层节点均匀分布
3. <rect> 绘制节点框
4. <path marker-end> 从 caller 底部到 callee 顶部画箭头
5. 线宽正比于 call_count
6. 颜色正比于 weight（热点辨识）
```

---

## 8. 公共 API

```ls
// ====== Figure 创建 ======
fn Figure(int svg_w, int svg_h, int text_w, int text_h) -> Figure
fn Figure_add_subplot(&!Figure fig, int rows, int cols, int index) -> &Axes

// ====== Axes 数据添加 ======
fn Axes_plot(&!Axes ax, vec(f64) ys)                      // 自动 x = 0..N-1
fn Axes_plot(&!Axes ax, vec(f64) xs, vec(f64) ys)         // 完整数据
fn Axes_plot(&!Axes ax, vec(f64) xs, vec(f64) ys,
             string color, string label)                   // 带样式

fn Axes_scatter(&!Axes ax, vec(f64) xs, vec(f64) ys)
fn Axes_scatter(&!Axes ax, vec(f64) xs, vec(f64) ys,
                string color, string label)

fn Axes_bar(&!Axes ax, vec(string) labels, vec(f64) values)
fn Axes_bar(&!Axes ax, vec(string) labels, vec(f64) values,
            string color, string label)

fn Axes_fill_between(&!Axes ax, vec(f64) xs,
                     vec(f64) y1, vec(f64) y2,
                     string color, f64 alpha)

fn Axes_hist(&!Axes ax, vec(f64) data, int bins, string color, string label)

// ====== Axes 样式控制 ======
fn Axes_set_xlabel(&!Axes ax, string label)
fn Axes_set_ylabel(&!Axes ax, string label)
fn Axes_set_title(&!Axes ax, string title)
fn Axes_set_xlim(&!Axes ax, f64 lo, f64 hi)     // 关闭 auto_scale
fn Axes_set_ylim(&!Axes ax, f64 lo, f64 hi)
fn Axes_set_xscale(&!Axes ax, string scale)     // "linear" "log"
fn Axes_set_yscale(&!Axes ax, string scale)
fn Axes_legend(&!Axes ax)                        // 显示图例
fn Axes_legend(&!Axes ax, string loc)            // 指定位置
fn Axes_grid(&!Axes ax)                          // 显示网格
fn Axes_grid(&!Axes ax, bool visible)            // 开关

// ====== 缩放 ======
fn Axes_auto_scale(&!Axes ax)                    // 重新计算范围+刻度
fn Axes_zoom(&!Axes ax, f64 factor)              // 等比缩放 +/- 10%

// ====== 输出 ======
fn Figure_save_svg(&Figure fig, string path) -> string   // 返回 "" 或错误
fn Figure_show_text(&Figure fig) -> string               // 返回终端字符串
fn Figure_print_text(&Figure fig)                        // 直接打印到 stdout

// ====== 高级图表 ======
fn flamegraph(vec(string) stacks, int w, int h, string path) -> string
fn timeline(vec(TimelineEvent) events, int w, int h, string path) -> string
fn callgraph(vec(CallGraphNode) nodes, vec(CallGraphEdge) edges,
             int w, int h, string path) -> string

// ====== CPU 调度时间线 ======
fn cpu_timeline(vec(CpuSchedEvent) events, CpuTopology topo,
                int w, int h, string path) -> string
fn cpu_timeline(vec(CpuSchedEvent) events, int total_logical,
                int total_physical, int w, int h, string path) -> string
fn cpu_timeline_aggregated(vec(CpuSchedEvent) events, CpuTopology topo,
                           int time_window_ns, int w, int h,
                           string path) -> string

// ====== 颜色工具 ======
fn hsv_to_hex(f64 h, f64 s, f64 v) -> string       // 0-360, 0-1, 0-1 → "#rrggbb"
fn cpu_color(int cpu_id, int total_physical) -> string   // CPU → 主色（实心）
fn cpu_color_ht(int cpu_id, int total_physical) -> (string, string)  // CPU → (实心色, 纹理色)
fn physical_core_color(int core_idx, int total) -> string   // 物理 core → 色相分布
```

### 8.1 颜色常量

```ls
const BLACK   = "#000000"
const RED     = "#e6194b"
const GREEN   = "#3cb44b"
const BLUE    = "#4363d8"
const CYAN    = "#42d4f4"
const MAGENTA = "#f032e6"
const YELLOW  = "#ffe119"
const ORANGE  = "#f58231"
const PURPLE  = "#911eb4"
const TEAL    = "#469990"
const PINK    = "#fabebe"
const BROWN   = "#9a6324"
const GRAY    = "#888888"

// 自动循环调色板（10 色，Tabl10 风格）
const COLORS = [BLUE, RED, GREEN, MAGENTA, ORANGE,
                CYAN, YELLOW, PURPLE, TEAL, PINK]
```

### 8.2 CPU 时间线颜色映射

物理 core 0 ~ N-1 均匀分布在色环 0°~300°（排除 300°~360° 红色区间以提高区分度）：

```ls
fn _cpu_hue(int cpu_id, int total_physical) -> f64 {
    // CPU 0 分配色相 0°（红），CPU 1~N-1 均匀分布在 15°~285°
    if cpu_id == 0 { return 0.0 }
    f64 hue = 15.0 + (cpu_id as f64) / (total_physical as f64) * 270.0
    return hue
}

fn cpu_color(int cpu_id, int total_physical) -> string {
    f64 h = _cpu_hue(cpu_id, total_physical)
    f64 s = 0.65    // 饱和度 65%
    f64 v = 0.75    // 明度 75%
    return hsv_to_hex(h, s, v)
}

fn cpu_color_ht(int cpu_id, int total_physical) -> (string, string) {
    int pc = cpu_id % total_physical     // 所属物理 core
    int ht = cpu_id / total_physical     // 0=首线程, 1=次线程
    f64 h = _cpu_hue(pc, total_physical)
    if ht == 0 {
        return (hsv_to_hex(h, 0.65, 0.75),    // 首线程：高亮、饱和
                hsv_to_hex(h, 0.30, 0.55))    // 备用暗色（用于斜线）
    } else {
        return (hsv_to_hex(h, 0.60, 0.55),    // 次线程：稍暗
                hsv_to_hex(h, 0.50, 0.65))    // 备用浅色
    }
}
```

效果：同一物理 core 的两个逻辑 CPU 视觉上同色系但明度不同；
横跨 32 个物理 core 时色相均匀分布在红-橙-黄-绿-蓝-紫之间，相邻 core 有明显区分。

### 8.3 使用示例 — FFT 频谱

```ls
import plot
import fft

fn main() {
    // 生成测试信号
    int n = 1024
    vec(f64) t = []
    vec(f64) x = []
    for int i = 0; i < n; i = i + 1 {
        f64 ti = i as f64
        t.push(ti)
        x.push(sin(2.0 * math.PI * 50.0 * ti / n) +
               0.5 * sin(2.0 * math.PI * 120.0 * ti / n))
    }

    // FFT
    vec(f64) mag = fft.rfft(x)                    // 假设 rfft 返回幅度谱
    vec(f64) freqs = fft.fftfreq(n, 1.0)

    // 绘图
    fig = plot.Figure(800, 500, 70, 20)
    ax1 = fig.add_subplot(2, 1, 1)
    ax1.plot(t, x, plot.BLUE, "time domain")
    ax1.set_ylabel("Amplitude")
    ax1.grid()

    ax2 = fig.add_subplot(2, 1, 2)
    ax2.plot(freqs, mag, plot.RED, "spectrum")
    ax2.set_xlabel("Frequency (Hz)")
    ax2.set_ylabel("Magnitude")

    // 输出
    fig.save_svg("fft_spectrum.svg")
    fig.print_text()     // 终端输出
}
```

### 8.4 使用示例 — CPU 调度时间线

```ls
import plot

fn main() {
    // 构建测试事件（从 perf/ETW 抓取的原始数据）
    vec(CpuSchedEvent) events = []

    events.push(CpuSchedEvent { start_ns: 0,      end_ns: 5000000,
                                thread_id: 1234,   thread_name: "main",
                                cpu_id: 0,         process_name: "app" })
    events.push(CpuSchedEvent { start_ns: 3000000, end_ns: 8000000,
                                thread_id: 5678,   thread_name: "worker-1",
                                cpu_id: 32,        process_name: "app" })
    events.push(CpuSchedEvent { start_ns: 6000000, end_ns: 12000000,
                                thread_id: 1234,   thread_name: "main",
                                cpu_id: 1,         process_name: "app" })
    events.push(CpuSchedEvent { start_ns: 9000000, end_ns: 15000000,
                                thread_id: 9012,   thread_name: "worker-2",
                                cpu_id: 33,        process_name: "app" })
    // ... 更多事件

    // 32 物理 core + HT = 64 逻辑 CPU
    CpuTopology topo
    topo.total_logical = 64
    topo.total_physical = 32
    topo.threads_per_core = 2

    // 渲染 CPU 时间线
    string err = plot.cpu_timeline(events, topo, 1200, 600,
                                   "cpu_schedule.svg")
    if err != "" { print("error: " + err) }

    // 聚合模式（1ms 窗口，适合长时间采集）
    string err2 = plot.cpu_timeline_aggregated(events, topo, 1000000,
                                               800, 400,
                                               "cpu_schedule_agg.svg")
}
```

#### 8.4.1 HT 颜色视觉效果

对于 32 物理 core + HT 的 SVG 输出：

- **CPU 0~31**（首线程）：实心矩形，色相从 `#d4453e`（红, core 0）渐变到 `#3e7bd4`（蓝, core 31）
- **CPU 32~63**（次线程）：斜线纹理矩形，色相与 `CPU N-32` 相同但明度降低 20%
- **Bar 上不标 CPU ID** — 颜色和样式本身就承载了 CPU 信息
- 悬浮 `mouseover` `<title>` 显示 `Thread main (TID 1234) on CPU 0 [Core 0, HT=0]`
- **图例作为颜色参照表**，单独列在图形下方，仅显示有事件的物理 core：

```
图例（CPU ↔ 颜色映射）
  Physical Core 0   ██ CPU 0     ▓▓ CPU 32 (HT)
  Physical Core 1   ██ CPU 1     ▓▓ CPU 33 (HT)
  ...
  Physical Core 7   ██ CPU 7     ▓▓ CPU 39 (HT)
```

---

## 9. 测试策略

| 类型 | 方法 | 验证方式 |
|------|------|----------|
| 刻度算法 | 输入固定 [lo, hi] → 检查 `_generate_ticks` 输出 | 预期值 `assert_eq` |
| 刻度算法 | 边界：空范围、负范围、零范围 | 不崩溃 |
| 刻度算法 | 对数坐标刻度 | 主刻度在 1,2,5×10ⁿ |
| HT 颜色 | `cpu_color(0, 32)` → `#e64545`（红色系） | 字符串匹配 |
| HT 颜色 | 相同物理 core 的 `cpu_color` 色相偏差 < 1° | `_cpu_hue` 计算 |
| HT 颜色 | `cpu_color_ht(0, 32)` 返回两个不同 hex | `first != second` |
| HT 颜色 | 32 core 色相均匀分布 0°~285° | 相邻 diff ≈ 8.7° |
| CPU 时间线 | 事件无 CPU ID 标注在 SVG bar 上 | SVG 不含 `>CPU<` 文本 |
| CPU 时间线 | 图例包含所有出现过的 CPU | 图例条数 == 事件中 unique CPU 数 |
| TEXT 后端 | CPU 时间线输出仅含 `█` 和空格 | 不包含颜色/style 字符 |
| 自动缩放 | `update_limits` 后 xmin/xmax 正确 | 预期值 |
| 坐标映射 | 已知数据+视口 → 确认映射数值 | 手工推导 |
| SVG 输出 | 生成固定 SVG → 字符串包含关键 `<polyline>` | `contains` 检查 |
| SVG 输出 | 多个子图 → SVG 含多个 `<g>` | 计数检查 |
| Text 输出 | 生成字符串 → 行数=预期、列数=预期 | `assert_eq` |
| E2E 信号 | sine → FFT → plot → SVG 存文件 | 人工目测 |
| memcheck | 所有测试加 `--memcheck` | 0 leaks / 0 dfree |
| AOT | `ls compile` + 运行 | SVG 内容一致 |

### 测试文件

```
tests/samples/plot_basic_test.ls       ← 基本线图 + SVG 输出
tests/samples/plot_bar_test.ls         ← 柱状图
tests/samples/plot_multi_test.ls       ← 多子图 + 图例 + 网格
tests/samples/plot_log_test.ls         ← 对数坐标
tests/samples/plot_text_test.ls        ← Text 后端输出
tests/samples/plot_flame.ls            ← 火焰图
tests/samples/plot_timeline.ls         ← 甘特图
tests/samples/plot_cpu_timeline.ls     ← CPU 调度时间线（含 HT 颜色验证）
tests/samples/plot_cpu_timeline_agg.ls ← CPU 聚合时间线
tests/samples/plot_color_ht.ls        ← HSV 颜色映射 + HT 色相对比
tests/samples/plot_callgraph.ls       ← 调用图
```

---

## 10. 实现步骤

### Phase 1 — 核心框架 + 刻度引擎（3 天）

- [ ] Figure / Axes / LineStyle 数据结构
- [ ] `_generate_ticks()` + `_nice_number()`
- [ ] `_format_tick()` 自动小数位
- [ ] `update_limits()` + `auto_scale()` + 边距
- [ ] `_map_x()` / `_map_y()` 坐标映射

### Phase 2 — SVG 后端（3 天）

- [ ] `_compute_layout()` SVG 布局
- [ ] SVG 折线图 `<polyline>` 输出
- [ ] 坐标轴 + 刻度标签 + 网格
- [ ] 图例自动定位
- [ ] 颜色调色板循环
- [ ] `Figure.save_svg()`
- [ ] 柱状图 / 填充区域 / 散点图
- [ ] 多子图布局

### Phase 3 — Text/Unicode 后端（2 天）

- [ ] 字符网格 + `_put_char()`
- [ ] `_rasterize_line()` DDA 光栅化
- [ ] `_rasterize_bars()` 8 级区块
- [ ] 文本坐标轴 + 刻度
- [ ] `Figure.show_text()` / `Figure.print_text()`

### Phase 4 — 特殊图表（各 1-2 天）

- [ ] `flamegraph()` 解析 → 树 → SVG
- [ ] `timeline()` 通用泳道 → SVG
- [ ] `cpu_timeline()` CPU 调度 + HT 颜色编码 → SVG
- [ ] `cpu_timeline_aggregated()` 聚合模式
- [ ] `hsv_to_hex()`／`cpu_color()`／`cpu_color_ht()` 颜色工具
- [ ] 时间轴格式化 `_format_time()` 自动选择 ns/μs/ms/s
- [ ] Legend HT 图例（物理 core 每行双色）
- [ ] `callgraph()` 拓扑排序 → SVG

### Phase 5 — 进阶（各 1 天）

- [ ] 对数坐标 `set_xscale("log")`
- [ ] 颜色映射 + 色条 `ax.heatmap(matrix)`
- [ ] 双 y 轴 `ax.twinx()`
- [ ] 手动覆盖 `set_xlim()` / `set_ylim()`
- [ ] 悬浮提示 `title` 属性

---

## 11. 与 FFT 模块的关系

两个模块独立实现，但组合使用是核心场景。FFT 生成频域数据，Plot 将其可视化：

```
fft 输出 vec(f64) 幅度谱
        ↓
plot 作为输入
        ↓
SVG: 连续平滑曲线
Text: 终端频谱概览（▁▂▃▄▅▆▇█）

相同数据、相同缩放算法，两种输出格式
```

建议先完成 Plot Phase 1~3（~8 天），再实现 FFT，因为 Plot 可以立即用于可视化已有 `math` 模块（sin/cos/log 曲线），不需要等 FFT。
