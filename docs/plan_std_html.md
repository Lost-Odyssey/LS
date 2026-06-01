# std.html — HTML 解析 / 生成模块 设计与实现计划

> **日期**：2026-06-01
> **状态**：设计完成，待实现（5–7 天）
> **前置依赖**：无编译器改动。纯 LS，复用 string/vec/map/enum/match/io + vec
> first-class（D/E/F）+ enum payload drop（L-006）+ struct 深拷贝。
> **来源**：本文取代 `docs/plan_md_html.md` 中 HTML 部分（Phase 2/4/5），并依据
> 2026-06 的实现能力（vec first-class、struct MdDoc、Phase G）重写。Markdown 部分
> （std.md）已完成，不再赘述。
> **参考实现**：`std/json.ls`（递归下降解析器）、`std/md.ls`（树模型 + builder +
> render + parse + extract）。

---

## 0. 可行性已验证（spike，2026-06-01）

设计中**唯一的新风险**是核心数据结构：自递归 `enum` 的某个变体**同时**持有
`map(string,string)` 和自递归的 `vec(HtmlNode)`。json 只验证过「两个 vec」
（`Object(vec(string), vec(JsonValue))`），map 作为 enum payload 且同变体内自递归
此前未测。

已用 spike 验证（构造嵌套树 + by-value enum 递归 render + 读 map payload + 二次
render 即 clone 路径），JIT + AOT 正确，**memcheck 0 leak / 0 double-free / 0
invalid free**。结论：

```ls
enum HtmlNode {
    Element(string tag, map(string, string) attrs, vec(HtmlNode) children)
    Text(string content)
    Comment(string content)
    RawText(string content)        // <script>/<style> 原文，不转义不解析
}
```

可放心采用。L-006（enum 含 vec/map payload 自动 drop）+ 嵌套 vec clone（L-011b）+
enum clone（`emit_enum_clone_val`）共同保证其 drop/clone 正确。

---

## 1. 设计原则

1. **跟随 std.md / std.json 惯用法**：模块级自由函数（非 impl 方法），enum + match
   穷尽性；解析器 `struct HtmlParser { string input; int pos; int len }` + `&!P`
   函数组，递归下降（**不用 regex**，HTML 属性语法的引号/无值/布尔属性 regex 易出
   边界 bug）。
2. **构造一律 bottom-up（函数式）**——⚠️ **纠正旧 plan 的设计缺陷**。旧
   `plan_md_html.md` §2.1 的 builder（`HtmlNode div = html.div(&!body)` 然后
   `html.set_attr(&!div, ...)`）在值语义下**是错的**：`div` 是 push 进 `body.children`
   后返回的**副本**，对它 `set_attr` 不会影响树里那一份。LS 是值语义语言，没有节点
   共享引用。正解：**先把子节点完整构造好，再组合进父节点**，永不「插入后再改」。
3. **森林根**：`struct HtmlDoc { vec(HtmlNode) roots }`（不是单 root）。真实 HTML 文件
   常是 `<!DOCTYPE>` + `<html>` 两个顶层节点，且要支持 fragment（多个并列根）。与
   std.md 的 `vec(MdBlock)` 同构。
4. **解析与生成共享同一棵树**：`parse` 产出 `HtmlDoc`，`render` 从同一棵树还原文本，
   round-trip 自洽（与 std.md 一致）。
5. **std.html 不依赖 std.md**；Markdown→HTML 转换（Phase 5）放在 **std.md** 里，直接
   从 MdBlock/MdInline 输出 HTML 字符串（不经过 HtmlNode 中间层）。

---

## 2. 数据结构

```ls
module html

import io

enum HtmlNode {
    Element(string tag, map(string, string) attrs, vec(HtmlNode) children)
    Text(string content)        // 标签间纯文本（render 时转义）
    Comment(string content)     // <!-- ... -->
    RawText(string content)     // <script>/<style> 原文
}

struct HtmlDoc {
    vec(HtmlNode) roots
}
```

**说明**
- 自闭合/void 标签**不**单设变体——void 是 tag 名的属性，render/parse 时按集合判断，
  此类 `Element` 的 `children` 为空。
- DOCTYPE 解析为一个 `Comment("DOCTYPE html")`（或跳过）；保留为 Comment 利于
  round-trip。
- `attrs` 用 `map(string,string)`；无值布尔属性（`disabled`）存 `attrs["disabled"]=""`。

> ⚠️ **map 不保序**：HTML 属性顺序在 render 后可能与原文不同，round-trip 做**语义
> 等价**比较而非字节级。测试据此设计（解析→render→再解析，比较树结构而非字符串）。

---

## 3. Phase H1 — 生成（写） + render（1.5–2 天）

### 3.1 函数式构造 API

```ls
import std.html as html

// 叶子
HtmlNode t  = html.text("hello & <world>")
HtmlNode c  = html.comment(" TODO ")

// 属性：从 [[k,v],...] 构造 map（LS 无命名参数，pairs 最顺手）
map(string,string) a = html.attrs([["class","box"], ["id","main"]])

// 元素（children 先建好再传入）
vec(HtmlNode) kids = []
kids.push(html.text("Hi"))
HtmlNode h1   = html.elem("h1", kids)                  // 无属性
HtmlNode divN = html.elem_attr("div", a, kids2)        // 带属性

// 常用便捷构造器（均返回 HtmlNode，bottom-up）
HtmlNode p   = html.p("a paragraph")                   // <p>text</p>
HtmlNode lnk = html.a("Click", "https://x.com")        // <a href=..>Click</a>
HtmlNode im  = html.img("photo.jpg", "alt")            // void: <img src alt>
HtmlNode br  = html.br()
HtmlNode hN  = html.h(2, "Title")                       // <h2>..</h2>

// 文档
HtmlDoc doc = html.document([
    html.comment("DOCTYPE html"),
    html.elem("html", [ /* head, body 节点 */ ])
])
// 或 fragment（多个并列根，无 html 包裹）
HtmlDoc frag = html.fragment(nodes)

// 输出
string s  = html.render(&doc)                 // 紧凑单行
string sp = html.render_pretty(&doc, 2)       // 缩进 2 空格
io.write_file("page.html", s)

// 纯字符串 helper（不建树，类似 md.fmt_*）
string e = html.escape("a<b&c")               // "a&lt;b&amp;c"
string tag = html.fmt_tag("a", [["href","u"]], "txt")  // <a href="u">txt</a>
```

> **可选** `set_attr(&!HtmlNode n, k, v)`：只在节点**尚未组合进父节点之前**安全
> （对独立局部变量原地改 map）。文档明确标注「组合后勿改」。实现用 `&!enum` 借用 +
> match 取出 attrs map + set + 写回；若 vec-first-class place 引擎对 `&!enum` payload
> 的原地 mutation 支持不足，则**砍掉此 API**，只保留 bottom-up（不影响核心能力）。
> 实现期先 spike 验证再决定。

### 3.2 render 规则

| HtmlNode | 输出 |
|----------|------|
| `Element(tag, attrs, children)` | `<tag k="v"...>` + children render + `</tag>`；void tag 无闭合 |
| `Text(s)` | `escape(s)`（`& < >` → 实体） |
| `Comment(s)` | `<!--{s}-->`；`s=="DOCTYPE html"` → `<!DOCTYPE html>` |
| `RawText(s)` | 原样输出（不转义） |

- **void 标签集合**：`br hr img input meta link area base col embed source wbr`。
- **属性值转义**：`& < > "` → 实体（`render` 内 `_escape_attr`）。
- `render_pretty`：维护缩进层级；block 级标签换行缩进，inline（`a span b i code` 等）
  尽量同行——首版可简化为「所有 Element 都换行缩进」，pretty 不追求完美。
- 递归：`_render_node(HtmlNode) -> string`（by-value，复用 md.ls `_render_block`
  模式）；`render(&HtmlDoc)` 遍历 `roots`。

---

## 4. Phase H2 — 解析（读）（2.5–3.5 天，核心）

### 4.1 API

```ls
import std.html as html

string content = try io.read_file("page.html")
HtmlDoc doc = html.parse(content)

// 查询
vec(HtmlNode) divs = html.find_by_tag(&doc, "div")    // 递归收集
vec(HtmlNode) hit  = html.find_by_id(&doc, "main")    // 0 或 1 个元素
string txt  = html.to_text(&doc)                       // 拼接所有 Text
vec(string) links = html.extract_links(&doc)           // 所有 <a href>
string cls = html.get_attr(node, "class")              // 单节点属性读取（空串=无）

// 用户递归遍历（推荐，不依赖闭包捕获 enum）
fn walk(HtmlNode n) {
    match n {
        Element(tag, attrs, children) => {
            // ... 处理 ...
            for i in 0..children.length { walk(children[i]) }
        }
        _ => {}
    }
}
```

> `find_by_id` 返回 `vec(HtmlNode)`（0/1 元素）而非 `Option(HtmlNode)`：避免
> `Option(has_drop enum)` 单态化的潜在边界（虽 Option(string) 已工作，但 payload 为
> 自递归 has_drop enum 未专门验证）。实现期可 spike `Option(HtmlNode)`，通过则升级。

### 4.2 支持子集

| 功能 | 优先级 |
|------|--------|
| 标签开闭 `<div>...</div>` | P0 |
| void / 自闭合 `<br>` `<img/>` | P0 |
| 属性：`k="v"` / `k='v'` / `k`（布尔） / `k=v`（无引号） | P0 |
| 嵌套 + 文本节点 | P0 |
| 实体解码 `&amp; &lt; &gt; &quot; &#60; &#x3C;` | P1 |
| 注释 `<!-- -->` / DOCTYPE | P1 |
| `<script>`/`<style>` → RawText | P1 |
| 容错（未闭合标签、错配 close） | P1 |

### 4.3 算法：递归下降（复用 json.ls，**非** stack）

旧 plan §4.3 用「open-tag 栈 + pop 插入栈顶」——在值语义下需要「原地修改 vec 中某个
enum 元素的 children」，深且易错。**改用递归下降**（json.ls 已证明）：解析子节点时
直接递归，返回**已建好的** `vec(HtmlNode)`，无原地 mutation。

```
_parse_nodes(&!P, string close_tag) -> vec(HtmlNode):
    nodes = []
    while not EOF:
        if peek == '<':
            if lookahead "</":
                读 close tag 名；若 == close_tag → 消费 "</tag>" 返回 nodes
                                 否则容错（忽略或返回，见下）
            else if lookahead "<!--":  nodes.push(_parse_comment(&!P))
            else if lookahead "<!":    nodes.push(_parse_doctype(&!P))
            else:                      nodes.push(_parse_element(&!P))
        else:
            nodes.push(_parse_text(&!P))     // 扫到下一个 '<'
    return nodes                              // EOF：未闭合自动收尾（容错）

_parse_element(&!P) -> HtmlNode:
    消费 '<'，读 tag 名
    attrs = _parse_attrs(&!P)                 // → map(string,string)
    if 自闭合 "/>" 或 tag ∈ void:
        return Element(tag, attrs, [])
    if tag ∈ {script, style}:
        raw = 扫到 "</tag>" 的原文
        消费 "</tag>"
        return Element(tag, attrs, [ RawText(raw) ])
    children = _parse_nodes(&!P, tag)         // 递归
    return Element(tag, attrs, children)

_parse_attrs(&!P) -> map(string,string):
    map = {}
    loop:
        skip_ws
        if peek ∈ {'>','/'} : break
        name = 扫描属性名（非空白/=/>/'/'）
        skip_ws
        if peek == '=':
            消费 '='; skip_ws
            value = 引号包围(" 或 ')→定界 / 无引号→扫到空白或 '>'
        else:
            value = ""                        // 布尔属性
        map.set(name, value)
    消费 '>'（吃掉可能的 '/'）
    return map
```

**容错策略**（首版，从简）：close tag 与期望不符时——直接把控制权交回上层（视作上层的
close，当前层提前结束）。这是宽松 HTML 解析的常见做法，足以应对良构文档与轻微嵌套
错误。不追求浏览器级容错（adoption agency 算法等）。

**实体解码**：`_parse_text` 收集文本时，遇 `&` 尝试匹配 `&amp;/&lt;/&gt;/&quot;/&#NN;
/&#xHH;`，命中则解码，否则原样保留 `&`。逐字符（复用 json.ls `_parse_string_raw`
的 escape 处理模式）。

### 4.4 查询 helper（递归收集）

- `find_by_tag` / `find_by_id` / `extract_links`：递归遍历，`vec(HtmlNode)` /
  `vec(string)` 累积（返回 vec(has_drop enum) 已由 spike + vec-first-class 验证）。
- `to_text`：拼接所有 `Text` 节点内容（跳过 script/style 的 RawText）。
- `get_attr(HtmlNode, key)`：match Element → `attrs.contains_key ? attrs.get : ""`。

---

## 5. Phase H3 — Markdown → HTML（放在 std.md，0.5–1 天）

在 **std.md** 增加（不在 std.html，零跨模块依赖）：

```ls
string html_text = md.to_html(md_source)              // 片段
string full = md.to_html_full(md_source, "Title")     // 含 <!DOCTYPE><html>... 包裹
```

实现 = `md.parse` → 递归遍历 `MdBlock`/`MdInline` → **直接输出 HTML 字符串**（不建
HtmlNode）。映射表（沿用旧 plan §5.2）：

| MdBlock / MdInline | HTML |
|----|----|
| `Heading(n, c)` | `<hN>..</hN>` |
| `Paragraph(c)` | `<p>..</p>` |
| `CodeBlock(lang, code)` | `<pre><code class="language-{lang}">..</code></pre>` |
| `UnorderedList` / `OrderedList` | `<ul><li>..</li></ul>` / `<ol>..</ol>` |
| `Blockquote` | `<blockquote>..</blockquote>` |
| `Table(h, rows)` | `<table><thead>..<tbody>..</table>` |
| `Bold/Italic/BoldItalic/Code` | `<strong>/<em>/<strong><em>/<code>` |
| `Link/Image` | `<a href>..</a>` / `<img src alt>` |

文本需经 HTML 转义（可在 std.md 内联一个 `_html_escape`，或暴露 `html.escape` 后由
调用方组合——但为保持 std.md 独立，std.md 内联实现）。

> **不做 HTML→Markdown**：有损（class/style 丢失）、需大量启发式、ROI 低（沿用旧
> plan §5.3 结论）。

---

## 6. 工期与里程碑（5–7 天）

```
H1  生成 + render + 函数式 builder + fmt/escape helper      1.5–2 天
H2  parse（tokenizer + 递归下降 + attrs + 实体 + raw + 容错） 2.5–3.5 天
     + 查询 helper（find_by_tag/id, to_text, extract_links）
H3  md.to_html / to_html_full（在 std.md）                  0.5–1 天
测试/边界/文档（贯穿）                                        0.5–1 天
                                                            ─────────
                                                            5–7 天
```

里程碑顺序：H1（建模 + 出文本）→ H2（核心解析，round-trip 自洽）→ H3（转换，
用户可感知）。每个里程碑独立可交付。

---

## 7. 测试计划（JIT + AOT + memcheck 三重，沿用 std.md 模式）

| 测试 | 内容 |
|------|------|
| `html_write` | 函数式构造嵌套树 → render → 断言精确字符串（紧凑）；escape 正确 |
| `html_parse` | parse 各子集（标签/属性/void/注释/嵌套/文本/实体/script raw） |
| `html_roundtrip` | parse → render → parse，比较**树结构语义等价**（attrs 无序，故不比字节） |
| `html_query` | find_by_tag / find_by_id / extract_links / to_text |
| `html_tolerant` | 未闭合标签、错配 close、空属性、布尔属性 |
| `md_to_html` | md.to_html 各 MdBlock/MdInline 映射 + to_html_full 包裹 |
| memcheck | 上述样本全部 `--memcheck` 0 leak / 0 double-free / 0 invalid |

- 样本放 `tests/samples/html_*.ls`，自检打印 `HTML PASS` / `HTML FAIL`（沿用
  closure_g.ls 模式），cmake 驱动跑 JIT+AOT+memcheck（复用
  `test_phase_g_closure.cmake` 三段式骨架）。
- 注册到 CMakeLists，ctest 计数 +N。

---

## 8. 风险评估（已据 spike 更新）

| 风险 | 影响 | 状态 |
|------|------|------|
| ~~enum 变体含 map + 自递归 vec~~ | ~~阻塞~~ | ✅ **spike 验证通过**（JIT+AOT+memcheck clean，2026-06-01） |
| ~~vec(HtmlNode) 自递归 / clone / drop~~ | ~~阻塞~~ | ✅ spike + L-006/L-011b/enum clone 已证 |
| 旧 plan builder 值语义缺陷 | 中 | ✅ 已纠正为 bottom-up 函数式构造 |
| `Option(HtmlNode)`（has_drop enum payload）单态化 | 低 | 规避：find_by_id 返回 `vec`(0/1)；实现期可 spike 升级 |
| `&!enum` payload 原地 mutation（可选 set_attr） | 低 | 非核心；spike 不通过就砍，bottom-up 不受影响 |
| HTML 容错复杂度 | 中 | 只做良构 + 轻微错误的宽松解析，不追浏览器级 |
| map 不保序 → round-trip 字节不一致 | 低 | 测试比较语义等价（树结构），非字节 |
| REPL 跨 snippet has_drop（L-010） | 低 | 与 std.json 同限制；`ls run .ls` 不受影响，文档标注 |
| 性能（纯 LS 字符串） | 低 | 文档类 <1MB，json/md 已证足够 |

---

## 9. 与 plan_md_html.md 的关系

`plan_md_html.md`（v2，2026-05-26）是 MD+HTML 合并计划，其中 Markdown 部分（Phase
1/3）已由 std.md 落地。本文 `plan_std_html.md` 聚焦 HTML（取代其 Phase 2/4/5），并修正
两点过时设计：

1. **builder 值语义缺陷** → bottom-up 函数式构造（§1.2 / §3.1）。
2. **stack-based 解析** → 递归下降（§4.3，与 json.ls 一致，避免 vec 内 enum 原地改）。

并新增：spike 可行性验证（§0）、森林根 `HtmlDoc { vec roots }`、map 无序的 round-trip
测试策略、`Option(has_drop)` 规避方案。

---

## 附录：std.json / std.md 可复用模式

| 来源模式 | std.html 复用 |
|----------|---------------|
| `struct JParser { string input; int pos; int len }` + `&!P` | `HtmlParser` 同构 |
| `_skip_ws` / `_peek` / `_advance` / `_scan_plain` | 通用扫描原语 |
| `_parse_string_raw` 的 escape 逐字符处理 | HTML 实体解码 |
| 递归下降 `_parse_value` 返回完整子树 | `_parse_element` / `_parse_nodes` |
| `enum JsonValue { Array(vec(JsonValue)) }` | `HtmlNode { Element(.., vec(HtmlNode)) }` |
| std.md `_render_block(MdBlock)` by-value 递归 | `_render_node(HtmlNode)` |
| std.md `vec(MdBlock)` 平坦文档根 | `HtmlDoc { vec(HtmlNode) roots }` |
| std.md builder push 模式 / fmt_* helper | bottom-up 构造器 / `fmt_tag` / `escape` |
