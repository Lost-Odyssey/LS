// std/md.ls — Markdown reader/writer for LS (Phase A: data model + builder + render).
// Pure LS; recursive render mirroring std/json.ls's enum-tree pattern.

import io

// ---- Core types ----

enum MdInline {
    Text(string content)            // plain text (also holds raw inline markup pre-parse)
    Bold(string content)            // **bold**
    Italic(string content)          // _italic_
    BoldItalic(string content)      // ***bold italic***
    Code(string content)            // `code`
    Link(string text, string url)   // [text](url)
    Image(string alt, string url)   // ![alt](url)
}

enum MdBlock {
    Heading(int level, vec(MdInline) content)          // # .. ######
    Paragraph(vec(MdInline) content)
    CodeBlock(string lang, string code)                // ```lang ... ```
    UnorderedList(vec(string) items)                   // - item (raw inline text)
    OrderedList(vec(string) items)                     // 1. item (raw inline text)
    Blockquote(vec(MdBlock) children)                  // > quote (recursive)
    Table(vec(string) headers, vec(string) cells)      // GFM; cells row-major flat, ncols=headers.length
    HorizontalRule
}

// MdDoc is an alias for vec(MdBlock), not a wrapping struct: mutating a struct's
// vec field through an &! borrow does not currently write back to the caller,
// whereas &!vec push does. The alias keeps O(1) builder push + clean teardown.
// (Cross-module type aliases aren't yet nameable, so external code spells the
// document type as `vec(md.MdBlock)`.)
type MdDoc = vec(MdBlock)

// ---- Document constructor ----

fn document() -> MdDoc {
    vec(MdBlock) v = []
    return v
}

// ---- Internal helpers ----

// Wrap a raw string as a single-element inline sequence (Phase A: no inline parse).
fn _inline_text(string s) -> vec(MdInline) {
    vec(MdInline) v = []
    v.push(Text(s.copy()))
    return v
}

fn _repeat_char(int ch, int n) -> string {
    string s = ""
    int i = 0
    while i < n {
        s.append(ch)
        i = i + 1
    }
    return s
}

fn _pad_right(string s, int width) -> string {
    string r = s.copy()
    int i = s.length
    while i < width {
        r.append(" ")
        i = i + 1
    }
    return r
}

// ---- Builder API (mutable borrow &!MdDoc) ----

fn heading(&!MdDoc d, int level, string text) {
    int lv = level
    if lv < 1 { lv = 1 }
    if lv > 6 { lv = 6 }
    d.push(Heading(lv, _inline_text(text)))
}

fn h1(&!MdDoc d, string text) { d.push(Heading(1, _inline_text(text))) }
fn h2(&!MdDoc d, string text) { d.push(Heading(2, _inline_text(text))) }
fn h3(&!MdDoc d, string text) { d.push(Heading(3, _inline_text(text))) }
fn h4(&!MdDoc d, string text) { d.push(Heading(4, _inline_text(text))) }
fn h5(&!MdDoc d, string text) { d.push(Heading(5, _inline_text(text))) }
fn h6(&!MdDoc d, string text) { d.push(Heading(6, _inline_text(text))) }

fn paragraph(&!MdDoc d, string text) {
    d.push(Paragraph(_inline_text(text)))
}

fn code_block(&!MdDoc d, string lang, string code) {
    d.push(CodeBlock(lang.copy(), code.copy()))
}

// List items are stored as raw inline-markdown strings (rendered verbatim).
// A vec(vec(MdInline)) shape needs enum/struct payload clone+drop of nested vecs,
// which the compiler doesn't fully support yet; vec(string) is clean & identical.
fn ul(&!MdDoc d, vec(string) items) {
    d.push(UnorderedList(items))
}

fn ol(&!MdDoc d, vec(string) items) {
    d.push(OrderedList(items))
}

fn blockquote(&!MdDoc d, string text) {
    vec(MdBlock) children = []
    children.push(Paragraph(_inline_text(text)))
    d.push(Blockquote(children))
}

// Cells are row-major flat: cells.length must be a multiple of headers.length.
fn table(&!MdDoc d, vec(string) headers, vec(string) cells) {
    d.push(Table(headers, cells))
}

fn hr(&!MdDoc d) {
    d.push(HorizontalRule)
}

// ---- Inline render ----

fn _render_inline(MdInline x) -> string {
    match x {
        Text(c)       => { return c.copy() }
        Bold(c)       => { string r = "**"; r.append(c); r.append("**"); return r }
        Italic(c)     => { string r = "_";  r.append(c); r.append("_");  return r }
        BoldItalic(c) => { string r = "***"; r.append(c); r.append("***"); return r }
        Code(c)       => { string r = "`";  r.append(c); r.append("`");  return r }
        Link(t, u)    => {
            string r = "["; r.append(t); r.append("]("); r.append(u); r.append(")")
            return r
        }
        Image(a, u)   => {
            string r = "!["; r.append(a); r.append("]("); r.append(u); r.append(")")
            return r
        }
    }
}

fn _render_inlines(vec(MdInline) inls) -> string {
    string out = ""
    int i = 0
    while i < inls.length {
        out.append(_render_inline(inls.get(i)))
        i = i + 1
    }
    return out
}

// ---- Table render (GFM, column-aligned) ----

// `cells` is row-major flat with `cols = headers.length` columns.
fn _render_table(vec(string) headers, vec(string) cells) -> string {
    int cols = headers.length
    if cols <= 0 { return "" }
    int nrows = cells.length / cols

    // compute column widths (>= 3 so the "---" separator is valid)
    vec(int) widths = []
    int c = 0
    while c < cols {
        int w = headers.get(c).length
        if w < 3 { w = 3 }
        int r = 0
        while r < nrows {
            int cw = cells.get(r * cols + c).length
            if cw > w { w = cw }
            r = r + 1
        }
        widths.push(w)
        c = c + 1
    }

    string out = ""
    // header row
    out.append("|")
    c = 0
    while c < cols {
        out.append(" ")
        out.append(_pad_right(headers.get(c), widths.get(c)))
        out.append(" |")
        c = c + 1
    }
    out.append("\n")
    // separator
    out.append("|")
    c = 0
    while c < cols {
        out.append(" ")
        out.append(_repeat_char('-', widths.get(c)))
        out.append(" |")
        c = c + 1
    }
    out.append("\n")
    // data rows
    int r = 0
    while r < nrows {
        out.append("|")
        c = 0
        while c < cols {
            out.append(" ")
            out.append(_pad_right(cells.get(r * cols + c), widths.get(c)))
            out.append(" |")
            c = c + 1
        }
        out.append("\n")
        r = r + 1
    }
    return out
}

// ---- Block render ----

fn _render_block(MdBlock b) -> string {
    match b {
        Heading(level, content) => {
            string r = _repeat_char('#', level)
            r.append(" ")
            r.append(_render_inlines(content))
            r.append("\n\n")
            return r
        }
        Paragraph(content) => {
            string r = _render_inlines(content)
            r.append("\n\n")
            return r
        }
        CodeBlock(lang, code) => {
            string r = "```"
            r.append(lang)
            r.append("\n")
            r.append(code)
            r.append("\n```\n\n")
            return r
        }
        UnorderedList(items) => {
            string r = ""
            int i = 0
            while i < items.length {
                r.append("- ")
                r.append(items.get(i))
                r.append("\n")
                i = i + 1
            }
            r.append("\n")
            return r
        }
        OrderedList(items) => {
            string r = ""
            int i = 0
            while i < items.length {
                r.append(f"{i + 1}. ")
                r.append(items.get(i))
                r.append("\n")
                i = i + 1
            }
            r.append("\n")
            return r
        }
        Blockquote(children) => {
            // render children, then prefix every line with "> "
            string inner = ""
            int i = 0
            while i < children.length {
                inner.append(_render_block(children.get(i)))
                i = i + 1
            }
            string r = ""
            int n = inner.length
            int start = 0
            int k = 0
            while k < n {
                if inner.at(k) == '\n' {
                    string line = inner.substr(start, k - start)
                    if line.length > 0 {
                        r.append("> ")
                        r.append(line)
                    } else {
                        r.append(">")
                    }
                    r.append("\n")
                    start = k + 1
                }
                k = k + 1
            }
            r.append("\n")
            return r
        }
        Table(headers, rows) => {
            string r = _render_table(headers, rows)
            r.append("\n")
            return r
        }
        HorizontalRule => {
            return "---\n\n"
        }
    }
}

// ---- Top-level render ----

fn render(&MdDoc d) -> string {
    string out = ""
    int i = 0
    while i < d.length {
        out.append(_render_block(d.get(i)))
        i = i + 1
    }
    return out
}

// ---- String fragment helpers (do not touch MdDoc) ----

fn fmt_heading(int level, string text) -> string {
    int lv = level
    if lv < 1 { lv = 1 }
    if lv > 6 { lv = 6 }
    string r = _repeat_char('#', lv)
    r.append(" ")
    r.append(text)
    return r
}

fn fmt_bold(string s) -> string {
    string r = "**"; r.append(s); r.append("**"); return r
}

fn fmt_italic(string s) -> string {
    string r = "_"; r.append(s); r.append("_"); return r
}

fn fmt_code(string s) -> string {
    string r = "`"; r.append(s); r.append("`"); return r
}

fn fmt_link(string text, string url) -> string {
    string r = "["; r.append(text); r.append("]("); r.append(url); r.append(")")
    return r
}

fn fmt_image(string alt, string url) -> string {
    string r = "!["; r.append(alt); r.append("]("); r.append(url); r.append(")")
    return r
}
