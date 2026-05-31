// std/md.ls — Markdown reader/writer for LS.
// Pure LS; recursive render + hand-written block parser, mirroring std/json.ls.
//
// Uses the rich data model now that the compiler supports container values as
// first class: MdDoc is a struct with a vec field; lists/table hold nested vecs.

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
    UnorderedList(vec(vec(MdInline)) items)            // - item
    OrderedList(vec(vec(MdInline)) items)              // 1. item
    Blockquote(vec(MdBlock) children)                  // > quote (recursive)
    Table(vec(string) headers, vec(vec(string)) rows)  // GFM table
    HorizontalRule
}

struct MdDoc {
    vec(MdBlock) blocks
}

// ---- Document constructor ----

fn document() -> MdDoc {
    vec(MdBlock) v = []
    return MdDoc { blocks: v }
}

// ---- Internal helpers ----

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
    d.blocks.push(Heading(lv, _inline_text(text)))
}

fn h1(&!MdDoc d, string text) { d.blocks.push(Heading(1, _inline_text(text))) }
fn h2(&!MdDoc d, string text) { d.blocks.push(Heading(2, _inline_text(text))) }
fn h3(&!MdDoc d, string text) { d.blocks.push(Heading(3, _inline_text(text))) }
fn h4(&!MdDoc d, string text) { d.blocks.push(Heading(4, _inline_text(text))) }
fn h5(&!MdDoc d, string text) { d.blocks.push(Heading(5, _inline_text(text))) }
fn h6(&!MdDoc d, string text) { d.blocks.push(Heading(6, _inline_text(text))) }

fn paragraph(&!MdDoc d, string text) {
    d.blocks.push(Paragraph(_inline_text(text)))
}

fn code_block(&!MdDoc d, string lang, string code) {
    d.blocks.push(CodeBlock(lang.copy(), code.copy()))
}

fn ul(&!MdDoc d, vec(string) items) {
    vec(vec(MdInline)) its = []
    int i = 0
    while i < items.length {
        its.push(_inline_text(items.get(i)))
        i = i + 1
    }
    d.blocks.push(UnorderedList(its))
}

fn ol(&!MdDoc d, vec(string) items) {
    vec(vec(MdInline)) its = []
    int i = 0
    while i < items.length {
        its.push(_inline_text(items.get(i)))
        i = i + 1
    }
    d.blocks.push(OrderedList(its))
}

fn blockquote(&!MdDoc d, string text) {
    vec(MdBlock) children = []
    children.push(Paragraph(_inline_text(text)))
    d.blocks.push(Blockquote(children))
}

fn table(&!MdDoc d, vec(string) headers, vec(vec(string)) rows) {
    d.blocks.push(Table(headers, rows))
}

fn hr(&!MdDoc d) {
    d.blocks.push(HorizontalRule)
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

fn _render_table(vec(string) headers, vec(vec(string)) rows) -> string {
    int cols = headers.length
    if cols <= 0 { return "" }

    vec(int) widths = []
    int c = 0
    while c < cols {
        int w = headers.get(c).length
        if w < 3 { w = 3 }
        int r = 0
        while r < rows.length {
            vec(string) row = rows.get(r)
            if c < row.length {
                int cw = row.get(c).length
                if cw > w { w = cw }
            }
            r = r + 1
        }
        widths.push(w)
        c = c + 1
    }

    string out = ""
    out.append("|")
    c = 0
    while c < cols {
        out.append(" ")
        out.append(_pad_right(headers.get(c), widths.get(c)))
        out.append(" |")
        c = c + 1
    }
    out.append("\n")
    out.append("|")
    c = 0
    while c < cols {
        out.append(" ")
        out.append(_repeat_char('-', widths.get(c)))
        out.append(" |")
        c = c + 1
    }
    out.append("\n")
    int r = 0
    while r < rows.length {
        vec(string) row = rows.get(r)
        out.append("|")
        c = 0
        while c < cols {
            string cell = ""
            if c < row.length { cell = row.get(c) }
            out.append(" ")
            out.append(_pad_right(cell, widths.get(c)))
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
                r.append(_render_inlines(items.get(i)))
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
                r.append(_render_inlines(items.get(i)))
                r.append("\n")
                i = i + 1
            }
            r.append("\n")
            return r
        }
        Blockquote(children) => {
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
    while i < d.blocks.length {
        out.append(_render_block(d.blocks.get(i)))
        i = i + 1
    }
    return out
}

// ====================================================================
// Parser (read) — block-level, hand-written line scanner. Lenient.
// Inline content is kept as a single raw Text (round-trips verbatim);
// splitting into Bold/Italic/Link is Phase C.
// ====================================================================

fn _heading_level(string line) -> int {
    int n = line.length
    int i = 0
    while i < n && line.at(i) == '#' { i = i + 1 }
    if i >= 1 && i <= 6 && i < n && line.at(i) == ' ' { return i }
    return 0
}

fn _is_hr(string t) -> bool {
    int n = t.length
    if n < 3 { return false }
    int ch = t.at(0)
    if ch != '-' && ch != '*' && ch != '_' { return false }
    int i = 0
    while i < n {
        if t.at(i) != ch { return false }
        i = i + 1
    }
    return true
}

fn _ordered_prefix_len(string line) -> int {
    int n = line.length
    int i = 0
    while i < n && line.at(i) >= '0' && line.at(i) <= '9' { i = i + 1 }
    if i == 0 { return 0 }
    if i < n && line.at(i) == '.' && i + 1 < n && line.at(i + 1) == ' ' {
        return i + 2
    }
    return 0
}

fn _is_table_sep(string line) -> bool {
    string t = line.trim()
    int n = t.length
    if n == 0 { return false }
    bool has_dash = false
    bool has_pipe = false
    int i = 0
    while i < n {
        int c = t.at(i)
        if c == '-' { has_dash = true }
        else if c == '|' { has_pipe = true }
        else if c == ':' || c == ' ' { }
        else { return false }
        i = i + 1
    }
    return has_dash && has_pipe
}

fn _split_table_row(string line) -> vec(string) {
    vec(string) raw = line.split("|")
    vec(string) cells = []
    int rn = raw.length
    int i = 0
    while i < rn {
        string cell = raw.get(i).trim()
        bool skip = false
        if i == 0 && cell.length == 0 { skip = true }
        if i == rn - 1 && cell.length == 0 { skip = true }
        if !skip { cells.push(cell) }
        i = i + 1
    }
    return cells
}

// Parse `input` into a list of blocks (internal; `parse` wraps it in MdDoc).
fn _parse_blocks(string input) -> vec(MdBlock) {
    vec(MdBlock) blocks = []
    vec(string) lines = input.lines()
    int nl = lines.length
    int i = 0

    while i < nl {
        string line = lines.get(i)
        string t = line.trim()

        if t.length == 0 {
            i = i + 1
            continue
        }

        int hl = _heading_level(line)
        if hl > 0 {
            string content = line.substr(hl + 1, line.length - hl - 1)
            blocks.push(Heading(hl, _inline_text(content.trim())))
            i = i + 1
            continue
        }

        if t.starts_with("```") {
            string lang = t.substr(3, t.length - 3).trim()
            string code = ""
            bool firstc = true
            i = i + 1
            while i < nl {
                string cl = lines.get(i)
                if cl.trim().starts_with("```") { i = i + 1; break }
                if !firstc { code.append("\n") }
                code.append(cl)
                firstc = false
                i = i + 1
            }
            blocks.push(CodeBlock(lang, code))
            continue
        }

        if _is_hr(t) {
            blocks.push(HorizontalRule)
            i = i + 1
            continue
        }

        if line.starts_with("- ") || line.starts_with("* ") {
            vec(vec(MdInline)) items = []
            while i < nl {
                string li = lines.get(i)
                if li.starts_with("- ") || li.starts_with("* ") {
                    items.push(_inline_text(li.substr(2, li.length - 2).trim()))
                    i = i + 1
                } else {
                    break
                }
            }
            blocks.push(UnorderedList(items))
            continue
        }

        if _ordered_prefix_len(line) > 0 {
            vec(vec(MdInline)) items = []
            while i < nl {
                string li = lines.get(i)
                int p = _ordered_prefix_len(li)
                if p > 0 {
                    items.push(_inline_text(li.substr(p, li.length - p).trim()))
                    i = i + 1
                } else {
                    break
                }
            }
            blocks.push(OrderedList(items))
            continue
        }

        if line.starts_with("> ") || t == ">" {
            string inner = ""
            bool firstq = true
            while i < nl {
                string li = lines.get(i)
                string lt = li.trim()
                if li.starts_with("> ") {
                    if !firstq { inner.append("\n") }
                    inner.append(li.substr(2, li.length - 2))
                    firstq = false
                    i = i + 1
                } else if lt == ">" {
                    if !firstq { inner.append("\n") }
                    firstq = false
                    i = i + 1
                } else {
                    break
                }
            }
            blocks.push(Blockquote(_parse_blocks(inner)))
            continue
        }

        if t.starts_with("|") && i + 1 < nl && _is_table_sep(lines.get(i + 1)) {
            vec(string) headers = _split_table_row(line)
            int ncols = headers.length
            i = i + 2
            vec(vec(string)) rows = []
            while i < nl {
                string rl = lines.get(i)
                if !rl.trim().starts_with("|") { break }
                vec(string) rc = _split_table_row(rl)
                vec(string) row = []
                int k = 0
                while k < ncols {
                    if k < rc.length { row.push(rc.get(k)) }
                    else { row.push("") }
                    k = k + 1
                }
                rows.push(row)
                i = i + 1
            }
            blocks.push(Table(headers, rows))
            continue
        }

        string para = ""
        bool firstp = true
        while i < nl {
            string pl = lines.get(i)
            string pt = pl.trim()
            if pt.length == 0 { break }
            if _heading_level(pl) > 0 { break }
            if pt.starts_with("```") { break }
            if _is_hr(pt) { break }
            if pl.starts_with("- ") || pl.starts_with("* ") { break }
            if _ordered_prefix_len(pl) > 0 { break }
            if pl.starts_with("> ") { break }
            if pt.starts_with("|") { break }
            if !firstp { para.append(" ") }
            para.append(pt)
            firstp = false
            i = i + 1
        }
        if para.length > 0 {
            blocks.push(Paragraph(_inline_text(para)))
        } else {
            i = i + 1
        }
    }

    return blocks
}

fn parse(string input) -> MdDoc {
    return MdDoc { blocks: _parse_blocks(input) }
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
