# LS Markdown & HTML 读写模块实现计划（v2）

> **初版日期**：2026-05-20
> **修订日期**：2026-05-26（根据 LS 现有实现能力全面修订）
> **前置依赖**：无编译器改动，纯 LS + 现有 string/vec/map/io/regex 实现
> **参考**：`docs/plan_output_formats.md` OF.3/OF.7；`std/json.ls` 实现模式

---

## 0. 总体设计

两个独立模块，可分别使用：

```
std/md.ls     — Markdown 解析 + 生成
std/html.ls   — HTML 解析 + 生成
```

### 0.1 设计原则

1. **跟随 json.ls 惯用法**：用 `enum` 变体表示节点类型（非 struct + int tag），获得 `match` 穷尽性检查
2. **MD 和 HTML 节点类型分离**：两个领域差异大（MD 有 heading level / ordered list；HTML 有 attrs map / 自闭合标签），强行共用一个 struct 会导致大量冗余字段
3. **解析与生成共享同一棵树**：Phase 1 就建树模型，Phase 3 解析直接产出同类型树，`render` 统一从树输出文本
4. **解析器模式**：`struct Parser { string input; int pos; int len }` + `&!Parser` 函数组，与 json.ls `JParser` 一致

### 0.2 现有能力确认

| 能力 | 状态 | 说明 |
|------|------|------|
| `vec(StructType)` 自递归 | ✅ 已验证 | json.ls `vec(JsonValue)` 正常工作 |
| `std.regex` | ✅ 已实现 | Pike VM NFA，支持 `find` / `find_all` / `split` / `replace_all` / `capture` |
| `string` 方法 | ✅ 足够 | `.at()` `.substr()` `.find()` `.append()` `.length` `.trim()` `.copy()` |
| `io.read_file` / `io.write_file` | ✅ 完整 | 返回 `Result(string, string)` / `Result(int, string)` |
| enum + match 穷尽性 | ✅ | `match` 编译器强制覆盖所有变体 |
| struct return | ✅ | 所有权正确转移给调用者 |
| `&!self` 可变借用 | ✅ | Builder 和 Parser 的核心交互方式 |

---

## 1. 核心数据结构

### 1.1 Markdown 节点（`std/md.ls`）

```ls
enum MdInline {
    Text(string content)
    Bold(string content)
    Italic(string content)
    BoldItalic(string content)
    Code(string content)
    Link(string text, string url)
    Image(string alt, string url)
    Newline
}

enum MdBlock {
    Heading(int level, vec(MdInline) content)
    Paragraph(vec(MdInline) content)
    CodeBlock(string lang, string code)
    UnorderedList(vec(vec(MdInline)) items)
    OrderedList(vec(vec(MdInline)) items)
    Blockquote(vec(MdBlock) children)
    Table(vec(string) headers, vec(vec(string)) rows)
    HorizontalRule
}

struct MdDoc {
    vec(MdBlock) blocks
}
```

**设计说明**：
- 行内元素与块级元素分离为两个 enum，反映 Markdown 的双层结构
- `Heading` / `Paragraph` 持有 `vec(MdInline)` 而非 `string`，支持 `**bold** and _italic_` 混合内容
- `Blockquote` 递归持有 `vec(MdBlock)`，支持嵌套引用
- `Table` 用简单的 `vec(string)` 存储单元格文本（表格内行内格式化暂不支持，P2 扩展）

### 1.2 HTML 节点（`std/html.ls`）

```ls
enum HtmlNode {
    Element(string tag, map(string, string) attrs, vec(HtmlNode) children)
    Text(string content)
    RawText(string content)
    Comment(string content)
}

struct HtmlDoc {
    HtmlNode root
}
```

**设计说明**：
- 只有 HTML 节点需要 `attrs` map，与 MD 节点分离避免浪费
- `Element` 变体统一表示所有标签（`div`, `p`, `h1`, ...），自闭合标签在 render 时根据 tag 名判断
- `RawText` 用于 `<script>` / `<style>` 内容，不做 HTML 转义
- 不需要 `SelfClosing` 变体——自闭合是 tag 的属性而非节点类型

---

## Phase 1 — Markdown 生成（写）（2-3 天）

> 对应 `plan_output_formats.md` OF.7，最简单先出成果。

### 1.1 API

```ls
import std.md as md

// ---- 创建文档 ----
MdDoc doc = md.document()

// ---- Builder API（&!MdDoc 可变借用）----
md.h1(&!doc, "Report Title")
md.h2(&!doc, "Section 1")
md.h3(&!doc, "Subsection")
md.paragraph(&!doc, "Normal text with **bold** and _italic_.")

// 代码块
md.code_block(&!doc, "ls", "fn main() { print(42) }")

// 表格
vec(string) headers = ["Name", "Score"]
vec(vec(string)) rows = [["Alice", "9.5"], ["Bob", "8.1"]]
md.table(&!doc, headers, rows)

// 列表
vec(string) items = ["First item", "Second item", "Third item"]
md.ul(&!doc, items)
md.ol(&!doc, items)

// 分隔线
md.hr(&!doc)

// 链接 / 图片（作为独立段落）
md.link_block(&!doc, "Click here", "https://example.com")
md.image_block(&!doc, "alt text", "image.png")

// 引用块
md.blockquote(&!doc, "This is a quote.")

// ---- 输出 ----
string text = md.render(&doc)
io.write_file("report.md", text)

// ---- 便捷：纯字符串 helpers（不操作 MdDoc）----
string h = md.fmt_heading(2, "Title")       // "## Title\n"
string b = md.fmt_bold("important")         // "**important**"
string i = md.fmt_italic("emphasis")        // "_emphasis_"
string c = md.fmt_code("x + 1")            // "`x + 1`"
string l = md.fmt_link("text", "url")       // "[text](url)"
```

### 1.2 实现要点

- `md.document()` 返回 `MdDoc { blocks: [] }`
- 每个 builder 函数接受 `&!MdDoc`，构造对应 `MdBlock` 变体后 `push` 到 `doc.blocks`
- `md.paragraph` 内的 `**bold**` / `_italic_` 在 builder 阶段作为纯文本存入 `Text(content)`；Phase 3 解析时才产出完整的 `vec(MdInline)`
- `md.render(&doc)` 递归遍历 `MdBlock` 树，按变体类型输出 Markdown 文本
- 表格 `render` 需计算列宽对齐（`pad_right` 用空格填充）

### 1.3 render 映射规则

| MdBlock 变体 | 输出格式 |
|-------------|----------|
| `Heading(level, _)` | `"#" * level + " " + content + "\n\n"` |
| `Paragraph(inlines)` | inline render + `"\n\n"` |
| `CodeBlock(lang, code)` | `` "```" + lang + "\n" + code + "\n```\n\n" `` |
| `UnorderedList(items)` | 每项 `"- " + content + "\n"` |
| `OrderedList(items)` | 每项 `"{i}. " + content + "\n"` |
| `Blockquote(children)` | 每行前加 `"> "` |
| `Table(headers, rows)` | GFM 表格格式 |
| `HorizontalRule` | `"---\n\n"` |

---

## Phase 2 — HTML 生成（写）（3-4 天）

> 对应 `plan_output_formats.md` OF.3A。

### 2.1 API

```ls
import std.html as html

// ---- Tree builder（返回 HtmlNode，传入 parent 的 &!引用）----
HtmlDoc doc = html.document()

// 获取 / 创建节点
HtmlNode head = html.head(&!doc)
html.title(&!head, "My Page")
html.meta_charset(&!head, "utf-8")

HtmlNode body = html.body(&!doc)
HtmlNode div = html.div(&!body)
html.set_attr(&!div, "class", "container")
html.set_attr(&!div, "id", "main")

html.h1(&!div, "Hello World")
html.p(&!div, "This is a paragraph.")

// 表格
HtmlNode tbl = html.table(&!div)
HtmlNode tr = html.tr(&!tbl)
html.th(&!tr, "Name")
html.th(&!tr, "Score")
tr = html.tr(&!tbl)
html.td(&!tr, "Alice")
html.td(&!tr, "9.5")

// 链接 / 图片
html.a(&!div, "Click", "https://example.com")
html.img(&!div, "photo.jpg", "alt text")

// 自定义 tag
HtmlNode nav = html.tag(&!body, "nav")
html.set_attr(&!nav, "role", "navigation")

// ---- 输出 ----
string s = html.render(&doc)                // compact 单行
string s = html.render_pretty(&doc, 2)      // 缩进 2 空格
io.write_file("page.html", s)
```

### 2.2 实现要点

- `HtmlDoc` 持有根 `HtmlNode`（通常是 `Element("html", {}, [...])`）
- `html.head` / `html.body` 等函数在 parent 的 `children` 中创建并追加 `Element` 变体
- `render` 递归遍历 `HtmlNode` 树，输出 `<tag attrs>children</tag>`
- `render_pretty` 额外维护缩进层级计数器

### 2.3 关键实现细节

**自闭合标签集合**（render 时判断 tag 名）：
```
"br", "hr", "img", "input", "meta", "link", "area", "base", "col", "embed", "source", "wbr"
```

**HTML 实体转义**（text content 和 attribute value 中）：
| 字符 | 转义 |
|------|------|
| `&` | `&amp;` |
| `<` | `&lt;` |
| `>` | `&gt;` |
| `"` | `&quot;` |

实现方式：遍历 string 逐字符 `.at()`，用 `.append()` 拼接结果（与 json.ls `_parse_string_raw` 相同模式）。

### 2.4 API 注意事项

LS 不支持命名参数（`class: "container"` 不合法），所有参数必须位置传递：

```ls
// ✗ 不合法：html.div(body, class: "container")
// ✓ 正确：先创建再 set_attr
HtmlNode div = html.div(&!body)
html.set_attr(&!div, "class", "container")
```

---

## Phase 3 — Markdown 解析（读）（5-7 天）

### 3.1 API

```ls
import std.md as md
import std.io as io

// 解析
string content = try io.read_file("README.md")
MdDoc doc = md.parse(content)

// 遍历块级节点
for i in 0..doc.blocks.length {
    match doc.blocks[i] {
        Heading(level, inlines) => {
            print(f"H{level}: ...")
        }
        Table(headers, rows) => {
            print(f"Table: {headers.length} cols, {rows.length} rows")
        }
        CodeBlock(lang, code) => {
            print(f"Code ({lang}): {code.length} chars")
        }
        _ => {}
    }
}

// 便捷提取
vec(string) headings = md.extract_headings(&doc)
vec(string) links = md.extract_links(&doc)
string plain = md.to_plain_text(&doc)
```

### 3.2 支持的 Markdown 语法子集

| 语法 | 示例 | 优先级 |
|------|------|--------|
| ATX 标题 | `# H1` ... `###### H6` | P0 |
| 段落 | 空行分隔的文本块 | P0 |
| 粗体 / 斜体 | `**bold**` / `_italic_` / `***both***` | P0 |
| 行内代码 | `` `code` `` | P0 |
| 代码块 | ` ```lang ... ``` ` | P0 |
| 无序列表 | `- item` / `* item` | P0 |
| 有序列表 | `1. item` | P0 |
| 链接 | `[text](url)` | P0 |
| 图片 | `![alt](url)` | P0 |
| 水平线 | `---` / `***` / `___` | P0 |
| 引用块 | `> quote` | P1 |
| 表格 (GFM) | `\| A \| B \|` + `\|---\|---\|` | P1 |
| 嵌套列表 | 缩进子列表 | P2 |
| HTML 内联 | `<br>` / `<div>...</div>` | P2（pass-through） |

### 3.3 实现策略：两阶段解析

**解析器状态**（复用 json.ls 模式）：

```ls
struct MdParser {
    string input
    int pos
    int len
}
```

**阶段一：块级解析 (block pass)**

按行处理，识别块级结构：

```
行首 "# "~"###### "          → Heading
行首 "```"                   → CodeBlock（扫到下一个 "```"）
行首 "- " / "* "             → UnorderedList
行首 数字+"."+" "            → OrderedList
行首 "> "                    → Blockquote
行首 "---"/"***"/"___"       → HorizontalRule
行首 "|" 且下行有 "|---|"    → Table
其他非空行                   → 收集为 Paragraph 原始文本
```

核心算法：将 input 按 `\n` 分割为行（用 `regex.split(input, "\n")` 或手动 `.find("\n")` 扫描），逐行判断块类型。

**阶段二：行内解析 (inline pass)**

对 `Heading` / `Paragraph` / `ListItem` 的文本内容做行内标记解析，产出 `vec(MdInline)`：

| 模式 | 方法 | 产出 |
|------|------|------|
| `**text**` | `regex.find_all(text, "\\*\\*(.+?)\\*\\*")` 或手动扫描 | `Bold(text)` |
| `_text_` | `regex.find_all(text, "_(.+?)_")` 或手动扫描 | `Italic(text)` |
| `` `code` `` | 手动扫描（找配对反引号） | `Code(code)` |
| `[text](url)` | `regex.capture(text, "\\[(.+?)\\]\\((.+?)\\)")` | `Link(text, url)` |
| `![alt](url)` | `regex.capture(text, "!\\[(.+?)\\]\\((.+?)\\)")` | `Image(alt, url)` |
| 普通文本 | 上述模式之间的剩余文本 | `Text(content)` |

**推荐方式**：对行内解析用**逐字符手动扫描**（非 regex），原因是多种标记可能交错嵌套（如 `**bold _and italic_**`），regex 难以正确处理优先级。json.ls 的 `_scan_plain` / `_advance` / `_peek` 模式可直接复用。

### 3.4 辅助函数

```ls
fn _starts_with(string s, string prefix) -> bool {
    if s.length < prefix.length { return false }
    return s.substr(0, prefix.length) == prefix
}

fn _count_leading(string s, int ch) -> int {
    int i = 0
    while i < s.length && s.at(i) == ch { i = i + 1 }
    return i
}
```

---

## Phase 4 — HTML 解析（读）（5-7 天）

### 4.1 API

```ls
import std.html as html
import std.io as io

string content = try io.read_file("page.html")
HtmlDoc doc = html.parse(content)

// 按标签查找
vec(HtmlNode) divs = html.find_by_tag(&doc, "div")
HtmlNode elem = html.find_by_id(&doc, "main")

// 遍历（递归）
fn walk_links(HtmlNode node) {
    match node {
        Element(tag, attrs, children) => {
            if tag == "a" {
                string href = attrs.get("href")
                print(f"Link: {href}")
            }
            for i in 0..children.length {
                walk_links(children[i])
            }
        }
        _ => {}
    }
}
walk_links(doc.root)

// 便捷方法
string text = html.to_text(&doc)
vec(string) links = html.extract_links(&doc)
```

> **注意**：`html.walk(doc, |HtmlNode n| { ... })` 闭包写法依赖闭包捕获 enum 的完善程度。
> 当前推荐用户写递归函数（如上 `walk_links`），或 Phase 4 仅提供非闭包版本的 `find_by_tag` / `find_by_id`。

### 4.2 支持的 HTML 子集

| 功能 | 说明 | 优先级 |
|------|------|--------|
| 标签开闭 | `<div>...</div>` | P0 |
| 自闭合标签 | `<br/>` / `<img .../>` / `<br>` | P0 |
| 属性解析 | `class="x"` / `id='y'` / `disabled`（无值） | P0 |
| 嵌套结构 | 递归子节点 | P0 |
| 文本节点 | 标签之间的纯文本 | P0 |
| 实体解码 | `&amp;` `&lt;` `&gt;` `&quot;` `&#60;` `&#x3C;` | P1 |
| 注释 | `<!-- ... -->` 跳过或保留为 Comment | P1 |
| DOCTYPE | `<!DOCTYPE html>` 跳过 | P1 |
| `<script>` / `<style>` | 内容作为 `RawText` 保留，不解析子标签 | P2 |

### 4.3 实现策略：手写递归下降 tokenizer + 树构建器

**解析器状态**（复用 json.ls JParser 模式）：

```ls
struct HtmlParser {
    string input
    int pos
    int len
}
```

**Tokenizer**（逐字符扫描，不用 regex）：

```
pos 处字符 == '<'：
    下一字符 == '/' → TagClose   （"</div>"）
    下一字符 == '!' → Comment 或 DOCTYPE
    否则           → TagOpen     （"<div class="x">"）
                     包含属性解析 → map(string, string)
                     以 "/>" 结尾 → SelfClose
pos 处字符 != '<'：
    扫到下一个 '<' 为止 → Text
```

**属性解析**（`_parse_attrs` 函数）：
1. 跳过空白
2. 扫描属性名（非空白、非 `=`、非 `>`、非 `/` 的连续字符）
3. 如果下一个是 `=`，读取值：引号包围的用 `"` 或 `'` 定界；无引号的扫到空白或 `>`
4. 无 `=` 的属性（如 `disabled`）存为 `attrs["disabled"] = ""`

**树构建器**：
- 维护 `vec(HtmlNode)` 作为 open tag 栈
- 遇到 `TagOpen` → push 新 `Element` 到栈
- 遇到 `TagClose` → pop 栈顶，将其作为 child 插入新栈顶
- 遇到 `SelfClose` → 直接作为 child 插入当前栈顶
- 遇到 `Text` → 创建 `Text(content)` 作为 child 插入当前栈顶
- EOF 时未闭合标签自动关闭（容错）

**`<script>` / `<style>` 特殊处理**：进入这些标签后，切换到 raw text 模式——不再解析子标签，扫描到对应 `</script>` 或 `</style>` 为止，内容存为 `RawText`。

**不用 regex 的原因**：HTML attribute 语法复杂（引号嵌套、无引号值、布尔属性），regex 容易出边界 case bug。json.ls 证明了逐字符解析在 LS 中性能和可维护性都好。

---

## Phase 5 — Markdown → HTML 转换（1-2 天）

> Phase 3 + Phase 2 的组合，不需要反向转换。

### 5.1 API

```ls
import std.md as md
import std.html as html
import std.io as io

// Markdown → HTML string
string md_text = try io.read_file("README.md")
string html_text = md.to_html(md_text)
io.write_file("README.html", html_text)

// 带完整 HTML 包装
string full = md.to_html_full(md_text, "My Document")
// <!DOCTYPE html><html><head><title>My Document</title>
// <meta charset="utf-8"></head><body>...</body></html>
```

### 5.2 实现

`md.to_html` = `md.parse` → 递归遍历 `MdBlock` / `MdInline` 树 → 直接输出 HTML 字符串。

**不经过 HtmlNode 中间层**——直接从 MD AST 输出 HTML 字符串，避免不必要的树构建开销。

映射表：

| MdBlock / MdInline | HTML 输出 |
|----|-----|
| `Heading(level, content)` | `<h{level}>...</h{level}>` |
| `Paragraph(inlines)` | `<p>...</p>` |
| `CodeBlock(lang, code)` | `<pre><code class="language-{lang}">...</code></pre>` |
| `UnorderedList(items)` | `<ul><li>...</li>...</ul>` |
| `OrderedList(items)` | `<ol><li>...</li>...</ol>` |
| `Blockquote(children)` | `<blockquote>...</blockquote>` |
| `Table(h, rows)` | `<table><thead><tr><th>...</th></tr></thead><tbody>...</tbody></table>` |
| `HorizontalRule` | `<hr>` |
| `Bold(text)` | `<strong>...</strong>` |
| `Italic(text)` | `<em>...</em>` |
| `Code(text)` | `<code>...</code>` |
| `Link(text, url)` | `<a href="{url}">{text}</a>` |
| `Image(alt, url)` | `<img src="{url}" alt="{alt}">` |

### 5.3 关于 HTML → Markdown 反向转换

**本版本不实现 `html.to_markdown`**。原因：
1. HTML → MD 是有损转换（class/style/自定义属性全丢失），实际使用场景少
2. 需要大量启发式规则（`<div>` 映射到什么？`<span style="font-weight:bold">` 算粗体吗？）
3. 投入产出比低，留作后续扩展

---

## 实施顺序与工期

```
Phase 1  Markdown 生成    2-3 天   ← 最简单，建立树模型
Phase 2  HTML 生成        3-4 天   ← builder + render
Phase 3  Markdown 解析    5-7 天   ← 两阶段解析器
Phase 4  HTML 解析        5-7 天   ← tokenizer + 树构建
Phase 5  MD→HTML 转换     1-2 天   ← Phase 3 + Phase 2 组合
                         ─────────
总计                     16-23 天
```

### 优先级与依赖关系

```
Phase 1 (MD write)  ──→  Phase 3 (MD read)  ──→  Phase 5 (MD→HTML)
                                                      ↑
Phase 2 (HTML write) ──→  Phase 4 (HTML read)  ───────┘
```

Phase 1 和 Phase 2 可并行开发（无依赖）。Phase 5 依赖 Phase 3（需要 MD 解析器）但不依赖 Phase 4（直接输出 HTML 字符串，不构建 HtmlNode 树）。

### 推荐启动顺序

1. **Phase 1**（MD 生成）— 2-3 天即可交付，立即可用于报告生成场景
2. **Phase 2**（HTML 生成）— 与 Phase 1 模式类似，熟练后 2-3 天
3. **Phase 5**（MD→HTML）— 若 Phase 3 尚未开发，可先实现简化版（直接 regex 替换，不经过完整解析树）
4. **Phase 3**（MD 解析）— 核心复杂度所在
5. **Phase 4**（HTML 解析）— 可延后到有实际需求时

---

## 风险评估（已更新）

| 风险 | 影响 | 当前状态 |
|------|------|----------|
| ~~`vec(MdNode)` 自递归 struct~~ | ~~阻塞~~ | ✅ **已确认可行**（json.ls `vec(JsonValue)` 正常） |
| ~~`std.regex` 不存在~~ | ~~影响 MD 解析~~ | ✅ **已实现**（Pike VM NFA，含 capture/split/replace_all） |
| `enum` 变体含 `vec(MdInline)` | 低 | json.ls 的 `Array(vec(JsonValue))` 已验证此模式 |
| Markdown 边界情况多 | 低 | 只覆盖 P0 子集，渐进扩展 |
| HTML 容错解析复杂 | 中 | 不追求浏览器级容错，只保证良构 HTML |
| `&!enum` mutation 模式 | 低 | json.ls `push` / `set` 已验证 COPY+REPLACE 模式 |
| 闭包捕获 HtmlNode | 中 | Phase 4 暂不依赖闭包遍历，提供非闭包版 API |
| 性能（纯 LS 字符串操作） | 低 | 文档类文件通常 < 1MB，json.ls 解析性能已证明足够 |

---

## 附录 A：与 `plan_output_formats.md` 的差异说明

| 条目 | OF 计划原文 | 本计划修订 | 原因 |
|------|-------------|-----------|------|
| OF.3A API | 使用命名参数 `class: "container"` | 改为 `set_attr` 函数调用 | LS 不支持命名参数 |
| OF.3B 模板引擎 | Mustache 风格模板 | 不在本计划范围 | 独立功能，依赖 json，单独规划 |
| OF.7 API | `md.p()` / `md.code()` | `md.paragraph()` / `md.code_block()` | 避免与 HTML `<p>` 命名混淆；`code` 保留给行内代码 |
| 共用节点模型 | 一个 MdNode 含 attrs map | MD/HTML enum 分离 | 避免冗余字段，各自类型安全 |
| Phase 5 | 双向互转 | 仅 MD→HTML | HTML→MD 有损且复杂，ROI 低 |
| 总工期 | 17-24 天 | 16-23 天 | 去掉 HTML→MD，Phase 5 简化 |

## 附录 B：实现参考 — json.ls 中可复用的模式

| json.ls 模式 | md.ls / html.ls 复用方式 |
|-------------|-------------------------|
| `struct JParser { string input; int pos; int len }` | `MdParser` / `HtmlParser` 同构 |
| `_skip_ws(&!JParser)` | 通用空白跳过 |
| `_peek(&!JParser) -> int` | 通用 lookahead |
| `_advance(&!JParser) -> int` | 通用消费 |
| `_scan_plain(&!JParser) -> int` | HTML text node / MD 段落内容批量扫描 |
| `_parse_string_raw` escape 处理 | HTML 实体解码 |
| enum `JsonValue { ... Array(vec(JsonValue)) ... }` | `MdBlock { ... Blockquote(vec(MdBlock)) ... }` |
| COPY+REPLACE `&!self` mutation | builder API 的 `&!MdDoc` / `&!HtmlDoc` |
