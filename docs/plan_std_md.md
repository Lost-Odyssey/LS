# std.md — Markdown 模块设计

> **状态**：Phase A（写）+ Phase B（读，块级 parse）已实现并 memcheck clean（JIT+AOT），ctest `test_std_md_jit` / `test_std_md_parse_jit`
> **日期**：2026-05-31
>
> **Phase B 说明**：`parse(string)->MdDoc` 手写行扫描，覆盖块级 P0+P1（标题/段落/围栏代码/有序无序列表/引用块递归/GFM 表格/水平线），宽松不失败。行内内容暂存为单个原始 `Text`（round-trip 逐字一致）。**Phase C**：行内拆分（Bold/Italic/Code/Link/Image）+ `extract_headings`/`extract_links`/`to_plain_text`。
>
> **Phase A 实现说明（与下方草案的偏差）**：受编译器 vec 值语义限制（见 feature_inventory L-011c），
> 当前实现做了两处规避，**输出与设计一致、内存干净**：
> 1. `MdDoc` 用 `type MdDoc = vec(MdBlock)` 别名而非 struct（`&!struct` 字段 vec 变更不写回；跨模块别名不可命名 → 外部写 `vec(md.MdBlock)`）。
> 2. 列表项 / 表格单元用扁平 `vec(string)`（`UnorderedList(vec(string))` / `Table(headers, 行优先 cells)`）而非嵌套 `vec(vec(...))`（enum payload 内嵌套 vec 的 clone/drop 尚未支持）。
> 已落地的相关编译器修复：struct 容器字段 drop（L-011a）、嵌套 vec drop/clone（L-011b）、vec rvalue 临时实参 drop。
> 待「vec first-class」专项（D/C/F）完成后可升级为命名 struct + 嵌套 vec 富结构。
> Phase B/C（parse）尚未开始。
> **前置依赖**：**无编译器改动**，纯 LS 实现，复用现有 `string` / `vec` / `map` / `enum`+`match` / `io`
> **实现模板**：`std/json.ls`（递归下降 parser + enum 树 + render，已验证 `vec(enum自递归)` 可用）
> **关联**：本文档是 [`docs/plan_md_html.md`](plan_md_html.md) 中 Markdown 部分的聚焦版（去掉 HTML），用于本轮 API 评审

---

## 0. 目标与非目标

### 0.1 目标
- 提供 `std/md.ls` 单模块，支持 **Markdown 生成（写）** 与 **Markdown 解析（读）**
- 解析与生成共享同一棵 AST（`MdDoc`），读进来能改、能再 render 回去
- 支持 CommonMark/GFM 的常用子集（见 §3 子集表），不追求 100% 规范覆盖
- 纯 LS，零编译器改动，零外部依赖（与「外部依赖仅 LLVM」一致）

### 0.2 非目标（v1 不做）
- 完整 CommonMark 规范（lazy continuation、setext 标题、引用式链接 `[a][1]`、脚注、HTML 块完整解析）
- 任意深度的嵌套列表（v1 仅顶层列表，嵌套留 P2）
- 表格单元格内的行内格式化（v1 单元格为纯文本）
- `md → html` 直转（见 §6 待决策 Q4）

---

## 1. 数据模型

行内（inline）与块级（block）分成两个 enum，对应 Markdown 的双层结构；这样 `match` 能做穷尽性检查。

```ls
// 行内元素
enum MdInline {
    Text(string content)          // 普通文本
    Bold(string content)          // **粗体**
    Italic(string content)        // _斜体_
    BoldItalic(string content)    // ***粗斜体***
    Code(string content)          // `行内代码`
    Link(string text, string url) // [text](url)
    Image(string alt, string url) // ![alt](url)
}

// 块级元素
enum MdBlock {
    Heading(int level, vec(MdInline) content)   // # ~ ######（level 1..6）
    Paragraph(vec(MdInline) content)
    CodeBlock(string lang, string code)         // ```lang ... ```
    UnorderedList(vec(vec(MdInline)) items)     // 每个 item 是一行行内序列
    OrderedList(vec(vec(MdInline)) items)
    Blockquote(vec(MdBlock) children)           // 递归，支持块嵌套
    Table(vec(string) headers, vec(vec(string)) rows)  // GFM；单元格纯文本
    HorizontalRule
}

struct MdDoc {
    vec(MdBlock) blocks
}
```

**设计说明**
- `Heading`/`Paragraph`/列表项持有 `vec(MdInline)` 而非裸 `string`，从而保留 `**bold** 和 _italic_` 的混合结构
- `Blockquote` 递归持有 `vec(MdBlock)` → 支持引用内嵌段落/列表
- `MdInline`/`MdBlock` 含 `string` payload → 是 has_drop enum；`vec(JsonValue)` 在 json.ls 已验证该模式工作正常
- 写场景下，builder 先把整段文本塞进单个 `Text(...)`（不解析行内）；只有 `parse` 才产出完整 `vec(MdInline)`（懒解析，简化 builder）

---

## 2. 用户接口（API）—— 评审重点

`import std.md as md`

### 2.1 写（生成）

```ls
// 创建空文档
MdDoc doc = md.document()

// ---- Builder：可变借用 &!MdDoc，依次追加块 ----
md.heading(&!doc, 1, "Report Title")     // 通用：指定 level
md.h1(&!doc, "Title")                    // 便捷别名 h1..h6
md.h2(&!doc, "Section")
md.paragraph(&!doc, "Text with **bold** and _italic_.")
md.code_block(&!doc, "ls", "fn main() { print(42) }")
md.ul(&!doc, ["First", "Second", "Third"])           // vec(string)
md.ol(&!doc, ["Step 1", "Step 2"])
md.blockquote(&!doc, "A quoted line.")
md.table(&!doc, ["Name", "Score"], [["Alice","9.5"], ["Bob","8.1"]])
md.hr(&!doc)

// ---- 输出整篇 ----
string text = md.render(&doc)            // 只读借用 → Markdown 文本
try io.write_file("report.md", text)

// ---- 纯字符串 helper（不碰 MdDoc，方便手拼）----
string s1 = md.fmt_heading(2, "Title")   // "## Title"
string s2 = md.fmt_bold("important")     // "**important**"
string s3 = md.fmt_italic("emphasis")    // "_emphasis_"
string s4 = md.fmt_code("x + 1")         // "`x + 1`"
string s5 = md.fmt_link("text", "url")   // "[text](url)"
string s6 = md.fmt_image("alt", "url")   // "![alt](url)"
```

### 2.2 读（解析）

```ls
string content = try io.read_file("README.md")
MdDoc doc = md.parse(content)            // 宽松解析，不失败（见 Q3）

// 遍历块
for i in 0..doc.blocks.length {
    match doc.blocks[i] {
        Heading(level, inlines) => { print(f"H{level}") }
        CodeBlock(lang, code)   => { print(f"code {lang}: {code.length}") }
        Table(headers, rows)    => { print(f"table {rows.length} rows") }
        _ => {}
    }
}

// ---- 便捷提取 ----
vec(string) hs    = md.extract_headings(&doc)   // 各标题纯文本
vec(string) links = md.extract_links(&doc)      // 所有 URL
string plain      = md.to_plain_text(&doc)      // 去格式纯文本
```

### 2.3 API 一览表

| 函数 | 签名 | 说明 |
|------|------|------|
| `document` | `() -> MdDoc` | 空文档 |
| `heading` | `(&!MdDoc, int level, string) -> void` | 追加标题 |
| `h1`..`h6` | `(&!MdDoc, string) -> void` | 便捷标题 |
| `paragraph` | `(&!MdDoc, string) -> void` | 追加段落 |
| `code_block` | `(&!MdDoc, string lang, string code) -> void` | 围栏代码块 |
| `ul` / `ol` | `(&!MdDoc, vec(string)) -> void` | 无序/有序列表 |
| `blockquote` | `(&!MdDoc, string) -> void` | 引用块（v1 单行/单段） |
| `table` | `(&!MdDoc, vec(string) headers, vec(vec(string)) rows) -> void` | GFM 表格 |
| `hr` | `(&!MdDoc) -> void` | 水平线 |
| `render` | `(&MdDoc) -> string` | AST → Markdown |
| `parse` | `(string) -> MdDoc` | Markdown → AST |
| `fmt_*` | `(...) -> string` | 行内片段字符串 helper |
| `extract_headings` | `(&MdDoc) -> vec(string)` | 提取标题文本 |
| `extract_links` | `(&MdDoc) -> vec(string)` | 提取链接 URL |
| `to_plain_text` | `(&MdDoc) -> string` | 提取纯文本 |

> 设计原则：与 `std/json.ls` 同构——`document()`/`parse()`/`render()` 三个入口，可变借用做 builder，只读借用做查询/输出。

---

## 3. 支持的 Markdown 子集

| 语法 | 示例 | 优先级 |
|------|------|--------|
| ATX 标题 | `# H1` … `###### H6` | P0 |
| 段落 | 空行分隔的文本块 | P0 |
| 粗体/斜体/粗斜体 | `**b**` / `_i_` / `***bi***` | P0 |
| 行内代码 | `` `code` `` | P0 |
| 围栏代码块 | ` ```lang … ``` ` | P0 |
| 无序列表 | `- ` / `* ` | P0 |
| 有序列表 | `1. ` | P0 |
| 链接/图片 | `[t](u)` / `![a](u)` | P0 |
| 水平线 | `---` / `***` / `___` | P0 |
| 引用块 | `> quote` | P1 |
| GFM 表格 | `\| A \| B \|` + `\|---\|---\|` | P1 |
| 嵌套列表 | 缩进子列表 | P2（暂不做） |
| 内联 HTML | `<br>` / `<div>` | P2（暂不做，原样保留为 Text） |

---

## 4. 实现概要

### 4.1 解析：两阶段
复用 json.ls 的 `struct MdParser { string input; int pos; int len }` + `&!MdParser` 函数组模式。

**阶段一·块级（按行扫描）** —— 把 input 按 `\n` 切行，逐行判断块类型：

```
行首 "# ".."###### "      → Heading
行首 "```"                → CodeBlock（吃到下一个 "```"）
行首 "- " / "* "          → UnorderedList（连续收集）
行首 数字 + ". "          → OrderedList
行首 "> "                 → Blockquote
整行 "---"/"***"/"___"    → HorizontalRule
行首 "|" 且下一行是 |---|  → Table
其余非空行                → 收集进 Paragraph 原始文本（空行结束）
```

**阶段二·行内**（对 Heading/Paragraph/列表项文本）：**逐字符手动扫描**产出 `vec(MdInline)`。
- 不用 regex：`**bold _and italic_**` 这类交错嵌套靠优先级手扫更可控（regex 难处理）
- 直接复用 json.ls 的 `_advance` / `_peek` / `_scan_plain` 风格

### 4.2 生成：递归 render
遍历 `MdBlock` → 按变体输出文本：

| 变体 | 输出 |
|------|------|
| `Heading(n, _)` | `"#"×n + " " + 内容 + "\n\n"` |
| `Paragraph` | 行内 render + `"\n\n"` |
| `CodeBlock(lang, code)` | `` ```lang\ncode\n```\n\n `` |
| `UnorderedList` | 每项 `"- " + 内容 + "\n"` |
| `OrderedList` | 每项 `"{i}. " + 内容 + "\n"` |
| `Blockquote` | 子块 render 后每行前加 `"> "` |
| `Table` | GFM 管道表格（列宽用 `pad_right` 对齐） |
| `HorizontalRule` | `"---\n\n"` |

行内 render：`Bold→**x**`、`Italic→_x_`、`Code→`x``、`Link→[t](u)`、`Image→![a](u)`、`Text→原样`。

### 4.3 round-trip 一致性
目标：`render(parse(text))` 在受支持子集内语义等价（不保证字节级一致——空格/换行可能规范化）。作为测试断言。

---

## 5. 测试与验收

- **单元/端到端 `.ls`（JIT + AOT + memcheck 三重）**，建样例：
  - `md_build.ls`：用 builder 造文档 → render → 断言输出片段
  - `md_parse.ls`：解析覆盖各 P0 语法 → match 断言节点
  - `md_roundtrip.ls`：`render(parse(s))` 语义等价
  - `md_extract.ls`：`extract_headings` / `extract_links` / `to_plain_text`
- **memcheck clean**：has_drop enum（含 `vec(MdInline)`）的构造/drop 零泄漏零双释放（重点验证 `vec(vec(MdInline))` 列表与 `Blockquote` 递归）
- 注册到 ctest（`test_std_md` 等），目标在现 90 基础上 +N 全绿

---

## 6. 决策（已敲定 2026-05-31）

| # | 问题 | 决策 |
|---|------|------|
| Q1 | v1 范围 | ✅ **P0+P1**：含 ATX 标题/段落/粗斜体/行内码/围栏码块/有序无序列表/链接图片/水平线（P0）+ 引用块 + GFM 表格（P1）。嵌套列表、内联 HTML 留 P2 |
| Q2 | 实现顺序 | ✅ **先写后读**：先 builder + render，再 parse；render 作为 parse 的 round-trip 验收工具 |
| Q3 | `parse` 返回类型 | ✅ **`MdDoc`**（宽松、不失败，无法识别的当普通文本） |
| Q4 | `md.to_html` | ✅ **不进 std.md**，HTML 输出/转义统一留到后续 std.html 阶段 |
| Q5 | 行内解析实现 | ✅ **手写逐字扫描**（复用 json.ls `_advance`/`_peek`/`_scan_plain` 风格，嵌套优先级可控，不用 regex） |
| Q6 | 嵌套列表 | ✅ **留 P2**，v1 仅顶层列表 |

---

## 7. 工期估算（参考，待 Q1/Q2 定稿）

| 阶段 | 内容 | 估时 |
|------|------|------|
| A | 数据模型 + builder + render（写，P0+P1） | 2–3 天 |
| B | 块级 parse（P0） | 2–3 天 |
| C | 行内 parse + extract helpers | 2 天 |
| D | 表格/引用块 parse（P1）+ round-trip 测试 + memcheck | 1–2 天 |
| — | **合计** | **约 7–10 天** |

> 风险低：无编译器改动，技术路径已被 json.ls 验证；主要工作量在解析边界情况与测试覆盖。
