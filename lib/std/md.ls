// std/md.ls — Markdown reader/writer for LS.
// Pure LS; recursive render + hand-written block parser, mirroring std/json.ls.
//
// Uses the rich data model now that the compiler supports container values as
// first class: MdDoc is a struct with a vec field; lists/table hold nested vecs.
//
// P7-mig: all string workflows migrated to the pure-LS `Str` (std.str).
// By-value `Str` params arrive as owned clones, so `.copy()` on params is gone;
// explicit `.copy()` remains where a borrowed match binder yields a value.

import io
import std.vec
import std.str

// ---- Core types ----

enum MdInline {
    Text(Str content)            // plain text (also holds raw inline markup pre-parse)
    Bold(Str content)            // **bold**
    Italic(Str content)          // _italic_
    BoldItalic(Str content)      // ***bold italic***
    Code(Str content)            // `code`
    Link(Str text, Str url)      // [text](url)
    Image(Str alt, Str url)      // ![alt](url)
}

enum MdBlock {
    Heading(int level, Vec(MdInline) content)          // # .. ######
    Paragraph(Vec(MdInline) content)
    CodeBlock(Str lang, Str code)                      // ```lang ... ```
    UnorderedList(Vec(Vec(MdInline)) items)            // - item
    OrderedList(Vec(Vec(MdInline)) items)              // 1. item
    Blockquote(Vec(MdBlock) children)                  // > quote (recursive)
    Table(Vec(Str) headers, Vec(Vec(Str)) rows)        // GFM table
    HorizontalRule
}

struct MdDoc {
    Vec(MdBlock) blocks
}

// ---- Document constructor ----

fn document() -> MdDoc {
    Vec(MdBlock) v = {}
    return MdDoc { blocks: v }
}

// ---- Internal helpers ----

fn _inline_text(Str s) -> Vec(MdInline) {
    Vec(MdInline) v = {}
    v.push(Text(s))
    return v
}

fn _repeat_char(int ch, int n) -> Str {
    Str s = ""
    int i = 0
    while i < n {
        s.push_byte(ch)
        i = i + 1
    }
    return s
}

fn _pad_right(Str s, int width) -> Str {
    int n = s.len()
    Str r = s
    int i = n
    while i < width {
        r.push_byte(' ')
        i = i + 1
    }
    return r
}

// ---- Builder API (mutable borrow &!MdDoc) ----

fn heading(&!MdDoc d, int level, Str text) {
    int lv = level
    if lv < 1 { lv = 1 }
    if lv > 6 { lv = 6 }
    d.blocks.push(Heading(lv, _inline_text(text)))
}

fn h1(&!MdDoc d, Str text) { d.blocks.push(Heading(1, _inline_text(text))) }
fn h2(&!MdDoc d, Str text) { d.blocks.push(Heading(2, _inline_text(text))) }
fn h3(&!MdDoc d, Str text) { d.blocks.push(Heading(3, _inline_text(text))) }
fn h4(&!MdDoc d, Str text) { d.blocks.push(Heading(4, _inline_text(text))) }
fn h5(&!MdDoc d, Str text) { d.blocks.push(Heading(5, _inline_text(text))) }
fn h6(&!MdDoc d, Str text) { d.blocks.push(Heading(6, _inline_text(text))) }

fn paragraph(&!MdDoc d, Str text) {
    d.blocks.push(Paragraph(_inline_text(text)))
}

fn code_block(&!MdDoc d, Str lang, Str code) {
    d.blocks.push(CodeBlock(lang, code))
}

fn ul(&!MdDoc d, Vec(Str) items) {
    Vec(Vec(MdInline)) its = {}
    int i = 0
    while i < items.len {
        its.push(_inline_text(items.get!(i)))
        i = i + 1
    }
    d.blocks.push(UnorderedList(its))
}

fn ol(&!MdDoc d, Vec(Str) items) {
    Vec(Vec(MdInline)) its = {}
    int i = 0
    while i < items.len {
        its.push(_inline_text(items.get!(i)))
        i = i + 1
    }
    d.blocks.push(OrderedList(its))
}

fn blockquote(&!MdDoc d, Str text) {
    Vec(MdBlock) children = {}
    children.push(Paragraph(_inline_text(text)))
    d.blocks.push(Blockquote(children))
}

fn table(&!MdDoc d, Vec(Str) headers, Vec(Vec(Str)) rows) {
    d.blocks.push(Table(headers, rows))
}

fn hr(&!MdDoc d) {
    d.blocks.push(HorizontalRule)
}

// ---- Inline render ----

fn _render_inline(MdInline x) -> Str {
    match x {
        Text(c)       => { return c.copy() }
        Bold(c)       => { Str r = "**"; r.push_str(c); r.push_str("**"); return r }
        Italic(c)     => { Str r = "_";  r.push_str(c); r.push_str("_");  return r }
        BoldItalic(c) => { Str r = "***"; r.push_str(c); r.push_str("***"); return r }
        Code(c)       => { Str r = "`";  r.push_str(c); r.push_str("`");  return r }
        Link(t, u)    => {
            Str r = "["; r.push_str(t); r.push_str("]("); r.push_str(u); r.push_str(")")
            return r
        }
        Image(a, u)   => {
            Str r = "!["; r.push_str(a); r.push_str("]("); r.push_str(u); r.push_str(")")
            return r
        }
    }
}

fn _render_inlines(Vec(MdInline) inls) -> Str {
    Str out = ""
    int i = 0
    while i < inls.len {
        out.push_str(_render_inline(inls.get!(i)))
        i = i + 1
    }
    return out
}

// ---- Table render (GFM, column-aligned) ----

fn _render_table(Vec(Str) headers, Vec(Vec(Str)) rows) -> Str {
    int cols = headers.len
    if cols <= 0 { return "" }

    Vec(int) widths = {}
    int c = 0
    while c < cols {
        int w = headers.get!(c).len()
        if w < 3 { w = 3 }
        int r = 0
        while r < rows.len {
            Vec(Str) row = rows.get!(r)
            if c < row.len {
                int cw = row.get!(c).len()
                if cw > w { w = cw }
            }
            r = r + 1
        }
        widths.push(w)
        c = c + 1
    }

    Str out = ""
    out.push_str("|")
    c = 0
    while c < cols {
        out.push_str(" ")
        out.push_str(_pad_right(headers.get!(c), widths.get!(c)))
        out.push_str(" |")
        c = c + 1
    }
    out.push_str("\n")
    out.push_str("|")
    c = 0
    while c < cols {
        out.push_str(" ")
        out.push_str(_repeat_char('-', widths.get!(c)))
        out.push_str(" |")
        c = c + 1
    }
    out.push_str("\n")
    int r = 0
    while r < rows.len {
        Vec(Str) row = rows.get!(r)
        out.push_str("|")
        c = 0
        while c < cols {
            Str cell = ""
            if c < row.len { cell = row.get!(c) }
            out.push_str(" ")
            out.push_str(_pad_right(cell, widths.get!(c)))
            out.push_str(" |")
            c = c + 1
        }
        out.push_str("\n")
        r = r + 1
    }
    return out
}

// ---- Block render ----

fn _render_block(MdBlock b) -> Str {
    match b {
        Heading(level, content) => {
            Str r = _repeat_char('#', level)
            r.push_str(" ")
            r.push_str(_render_inlines(content))
            r.push_str("\n\n")
            return r
        }
        Paragraph(content) => {
            Str r = _render_inlines(content)
            r.push_str("\n\n")
            return r
        }
        CodeBlock(lang, code) => {
            Str r = "```"
            r.push_str(lang)
            r.push_str("\n")
            r.push_str(code)
            r.push_str("\n```\n\n")
            return r
        }
        UnorderedList(items) => {
            Str r = ""
            int i = 0
            while i < items.len {
                r.push_str("- ")
                r.push_str(_render_inlines(items.get!(i)))
                r.push_str("\n")
                i = i + 1
            }
            r.push_str("\n")
            return r
        }
        OrderedList(items) => {
            Str r = ""
            int i = 0
            while i < items.len {
                r.push_str(f"{i + 1}. ")
                r.push_str(_render_inlines(items.get!(i)))
                r.push_str("\n")
                i = i + 1
            }
            r.push_str("\n")
            return r
        }
        Blockquote(children) => {
            Str inner = ""
            int i = 0
            while i < children.len {
                inner.push_str(_render_block(children.get!(i)))
                i = i + 1
            }
            Str r = ""
            int n = inner.len()
            int start = 0
            int k = 0
            while k < n {
                if inner.byte_at(k) == '\n' {
                    Str line = inner.substr(start, k - start)
                    if line.len() > 0 {
                        r.push_str("> ")
                        r.push_str(line)
                    } else {
                        r.push_str(">")
                    }
                    r.push_str("\n")
                    start = k + 1
                }
                k = k + 1
            }
            r.push_str("\n")
            return r
        }
        Table(headers, rows) => {
            Str r = _render_table(headers, rows)
            r.push_str("\n")
            return r
        }
        HorizontalRule => {
            return "---\n\n"
        }
    }
}

// ---- Top-level render ----

fn render(&MdDoc d) -> Str {
    Str out = ""
    int i = 0
    while i < d.blocks.len {
        out.push_str(_render_block(d.blocks.get!(i)))
        i = i + 1
    }
    return out
}

// ====================================================================
// Parser (read) — block-level, hand-written line scanner. Lenient.
// Inline content is kept as a single raw Text (round-trips verbatim);
// splitting into Bold/Italic/Link is Phase C.
// ====================================================================

fn _heading_level(&Str line) -> int {
    int n = line.len()
    int i = 0
    while i < n && line.byte_at(i) == '#' { i = i + 1 }
    if i >= 1 && i <= 6 && i < n && line.byte_at(i) == ' ' { return i }
    return 0
}

fn _is_hr(&Str t) -> bool {
    int n = t.len()
    if n < 3 { return false }
    int ch = t.byte_at(0)
    if ch != '-' && ch != '*' && ch != '_' { return false }
    int i = 0
    while i < n {
        if t.byte_at(i) != ch { return false }
        i = i + 1
    }
    return true
}

fn _ordered_prefix_len(&Str line) -> int {
    int n = line.len()
    int i = 0
    while i < n && line.byte_at(i) >= '0' && line.byte_at(i) <= '9' { i = i + 1 }
    if i == 0 { return 0 }
    if i < n && line.byte_at(i) == '.' && i + 1 < n && line.byte_at(i + 1) == ' ' {
        return i + 2
    }
    return 0
}

fn _is_table_sep(Str line) -> bool {
    Str t = line.trim()
    int n = t.len()
    if n == 0 { return false }
    bool has_dash = false
    bool has_pipe = false
    int i = 0
    while i < n {
        int c = t.byte_at(i)
        if c == '-' { has_dash = true }
        else if c == '|' { has_pipe = true }
        else if c == ':' || c == ' ' { }
        else { return false }
        i = i + 1
    }
    return has_dash && has_pipe
}

fn _split_table_row(&Str line) -> Vec(Str) {
    Vec(Str) raw = line.split("|")
    Vec(Str) cells = {}
    int rn = raw.len()
    int i = 0
    while i < rn {
        Str cell = raw.get!(i).trim()
        bool skip = false
        if i == 0 && cell.len() == 0 { skip = true }
        if i == rn - 1 && cell.len() == 0 { skip = true }
        if !skip { cells.push(cell) }
        i = i + 1
    }
    return cells
}

// ---- Inline parsing (Phase C) ----

// Index of `ch` in `s` at/after `from`, or -1.
fn _find_char(&Str s, int start, int ch) -> int {
    int n = s.len()
    int i = start
    while i < n {
        if s.byte_at(i) == ch { return i }
        i = i + 1
    }
    return 0 - 1
}

// Index of the first of two consecutive `ch` at/after `from`, or -1.
fn _find_double(&Str s, int start, int ch) -> int {
    int n = s.len()
    int i = start
    while i + 1 < n {
        if s.byte_at(i) == ch && s.byte_at(i + 1) == ch { return i }
        i = i + 1
    }
    return 0 - 1
}

// Index of the first of three consecutive `ch` at/after `from`, or -1.
fn _find_triple(&Str s, int start, int ch) -> int {
    int n = s.len()
    int i = start
    while i + 2 < n {
        if s.byte_at(i) == ch && s.byte_at(i + 1) == ch && s.byte_at(i + 2) == ch { return i }
        i = i + 1
    }
    return 0 - 1
}

// Hand-written inline scanner: split `text` into MdInline runs. Unclosed markers
// are treated as literal text. Marker priority: image, link, code, ***, **, *, _.
fn _parse_inlines(Str text) -> Vec(MdInline) {
    Vec(MdInline) out = {}
    int n = text.len()
    int i = 0
    Str buf = ""

    while i < n {
        int c = text.byte_at(i)

        // image ![alt](url)
        if c == '!' && i + 1 < n && text.byte_at(i + 1) == '[' {
            int rb = _find_char(text, i + 2, ']')
            if rb >= 0 && rb + 1 < n && text.byte_at(rb + 1) == '(' {
                int rp = _find_char(text, rb + 2, ')')
                if rp >= 0 {
                    if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                    out.push(Image(text.substr(i + 2, rb - (i + 2)),
                                   text.substr(rb + 2, rp - (rb + 2))))
                    i = rp + 1
                    continue
                }
            }
            buf.push_byte(c); i = i + 1; continue
        }

        // link [text](url)
        if c == '[' {
            int rb = _find_char(text, i + 1, ']')
            if rb >= 0 && rb + 1 < n && text.byte_at(rb + 1) == '(' {
                int rp = _find_char(text, rb + 2, ')')
                if rp >= 0 {
                    if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                    out.push(Link(text.substr(i + 1, rb - (i + 1)),
                                  text.substr(rb + 2, rp - (rb + 2))))
                    i = rp + 1
                    continue
                }
            }
            buf.push_byte(c); i = i + 1; continue
        }

        // inline code `code`
        if c == '`' {
            int cl = _find_char(text, i + 1, '`')
            if cl >= 0 {
                if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                out.push(Code(text.substr(i + 1, cl - (i + 1))))
                i = cl + 1
                continue
            }
            buf.push_byte(c); i = i + 1; continue
        }

        // ***bold italic***
        if c == '*' && i + 2 < n && text.byte_at(i + 1) == '*' && text.byte_at(i + 2) == '*' {
            int cl = _find_triple(text, i + 3, '*')
            if cl >= 0 {
                if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                out.push(BoldItalic(text.substr(i + 3, cl - (i + 3))))
                i = cl + 3
                continue
            }
            buf.push_byte(c); i = i + 1; continue
        }

        // **bold**
        if c == '*' && i + 1 < n && text.byte_at(i + 1) == '*' {
            int cl = _find_double(text, i + 2, '*')
            if cl >= 0 {
                if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                out.push(Bold(text.substr(i + 2, cl - (i + 2))))
                i = cl + 2
                continue
            }
            buf.push_byte(c); i = i + 1; continue
        }

        // *italic*
        if c == '*' {
            int cl = _find_char(text, i + 1, '*')
            if cl >= 0 {
                if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                out.push(Italic(text.substr(i + 1, cl - (i + 1))))
                i = cl + 1
                continue
            }
            buf.push_byte(c); i = i + 1; continue
        }

        // _italic_
        if c == '_' {
            int cl = _find_char(text, i + 1, '_')
            if cl >= 0 {
                if buf.len() > 0 { out.push(Text(buf.copy())); buf = "" }
                out.push(Italic(text.substr(i + 1, cl - (i + 1))))
                i = cl + 1
                continue
            }
            buf.push_byte(c); i = i + 1; continue
        }

        buf.push_byte(c)
        i = i + 1
    }
    if buf.len() > 0 { out.push(Text(buf)) }
    return out
}

// Parse `input` into a list of blocks (internal; `parse` wraps it in MdDoc).
fn _parse_blocks(Str input) -> Vec(MdBlock) {
    Vec(MdBlock) blocks = {}
    Vec(Str) lines = input.lines()
    int nl = lines.len()
    int i = 0

    while i < nl {
        Str line = lines.get!(i)
        Str t = line.trim()

        if t.len() == 0 {
            i = i + 1
            continue
        }

        int hl = _heading_level(line)
        if hl > 0 {
            Str content = line.substr(hl + 1, line.len() - hl - 1)
            blocks.push(Heading(hl, _parse_inlines(content.trim())))
            i = i + 1
            continue
        }

        if t.starts_with?("```") {
            Str lang = t.substr(3, t.len() - 3).trim()
            Str code = ""
            bool firstc = true
            i = i + 1
            while i < nl {
                Str cl = lines.get!(i)
                if cl.trim().starts_with?("```") { i = i + 1; break }
                if !firstc { code.push_str("\n") }
                code.push_str(cl)
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

        if line.starts_with?("- ") || line.starts_with?("* ") {
            Vec(Vec(MdInline)) items = {}
            while i < nl {
                Str li = lines.get!(i)
                if li.starts_with?("- ") || li.starts_with?("* ") {
                    items.push(_parse_inlines(li.substr(2, li.len() - 2).trim()))
                    i = i + 1
                } else {
                    break
                }
            }
            blocks.push(UnorderedList(items))
            continue
        }

        if _ordered_prefix_len(line) > 0 {
            Vec(Vec(MdInline)) items = {}
            while i < nl {
                Str li = lines.get!(i)
                int p = _ordered_prefix_len(li)
                if p > 0 {
                    items.push(_parse_inlines(li.substr(p, li.len() - p).trim()))
                    i = i + 1
                } else {
                    break
                }
            }
            blocks.push(OrderedList(items))
            continue
        }

        if line.starts_with?("> ") || t == ">" {
            Str inner = ""
            bool firstq = true
            while i < nl {
                Str li = lines.get!(i)
                Str lt = li.trim()
                if li.starts_with?("> ") {
                    if !firstq { inner.push_str("\n") }
                    inner.push_str(li.substr(2, li.len() - 2))
                    firstq = false
                    i = i + 1
                } else if lt == ">" {
                    if !firstq { inner.push_str("\n") }
                    firstq = false
                    i = i + 1
                } else {
                    break
                }
            }
            blocks.push(Blockquote(_parse_blocks(inner)))
            continue
        }

        if t.starts_with?("|") && i + 1 < nl && _is_table_sep(lines.get!(i + 1)) {
            Vec(Str) headers = _split_table_row(line)
            int ncols = headers.len
            i = i + 2
            Vec(Vec(Str)) rows = {}
            while i < nl {
                Str rl = lines.get!(i)
                if !rl.trim().starts_with?("|") { break }
                Vec(Str) rc = _split_table_row(rl)
                Vec(Str) row = {}
                int k = 0
                while k < ncols {
                    if k < rc.len { row.push(rc.get!(k)) }
                    else { row.push("") }
                    k = k + 1
                }
                rows.push(row)
                i = i + 1
            }
            blocks.push(Table(headers, rows))
            continue
        }

        Str para = ""
        bool firstp = true
        while i < nl {
            Str pl = lines.get!(i)
            Str pt = pl.trim()
            if pt.len() == 0 { break }
            if _heading_level(pl) > 0 { break }
            if pt.starts_with?("```") { break }
            if _is_hr(pt) { break }
            if pl.starts_with?("- ") || pl.starts_with?("* ") { break }
            if _ordered_prefix_len(pl) > 0 { break }
            if pl.starts_with?("> ") { break }
            if pt.starts_with?("|") { break }
            if !firstp { para.push_str(" ") }
            para.push_str(pt)
            firstp = false
            i = i + 1
        }
        if para.len() > 0 {
            blocks.push(Paragraph(_parse_inlines(para)))
        } else {
            i = i + 1
        }
    }

    return blocks
}

fn parse(Str input) -> MdDoc {
    return MdDoc { blocks: _parse_blocks(input) }
}

// ====================================================================
// Extraction helpers (Phase C): plain text + headings + links.
// ====================================================================

fn _inline_plain(MdInline x) -> Str {
    match x {
        Text(c)       => { return c.copy() }
        Bold(c)       => { return c.copy() }
        Italic(c)     => { return c.copy() }
        BoldItalic(c) => { return c.copy() }
        Code(c)       => { return c.copy() }
        Link(t, u)    => { return t.copy() }
        Image(a, u)   => { return a.copy() }
    }
}

fn _inlines_plain(Vec(MdInline) inls) -> Str {
    Str out = ""
    int i = 0
    while i < inls.len {
        out.push_str(_inline_plain(inls.get!(i)))
        i = i + 1
    }
    return out
}

// Return the URL of a Link/Image inline, or "". Per-variant `_` (not a bare
// catch-all) so each ignored payload is dropped.
fn _inline_link_of(MdInline x) -> Str {
    match x {
        Link(_, u)    => { return u.copy() }
        Image(_, u)   => { return u.copy() }
        Text(_)       => { return "" }
        Bold(_)       => { return "" }
        Italic(_)     => { return "" }
        BoldItalic(_) => { return "" }
        Code(_)       => { return "" }
    }
}

fn _inline_links(Vec(MdInline) inls) -> Vec(Str) {
    Vec(Str) out = {}
    int i = 0
    while i < inls.len {
        Str u = _inline_link_of(inls.get!(i))
        if u.len() > 0 { out.push(u) }
        i = i + 1
    }
    return out
}

fn _block_links(MdBlock b) -> Vec(Str) {
    match b {
        // L-012 ③ fixed: returning a heap-value call result directly from a
        // match arm now moves the temp out (no spurious clone+leak).
        Heading(_, c)    => { return _inline_links(c) }
        Paragraph(c)     => { return _inline_links(c) }
        UnorderedList(items) => {
            Vec(Str) out = {}
            int i = 0
            while i < items.len {
                Vec(Str) ls = _inline_links(items.get!(i))
                int j = 0
                while j < ls.len { out.push(ls.get!(j)); j = j + 1 }
                i = i + 1
            }
            return out
        }
        OrderedList(items) => {
            Vec(Str) out = {}
            int i = 0
            while i < items.len {
                Vec(Str) ls = _inline_links(items.get!(i))
                int j = 0
                while j < ls.len { out.push(ls.get!(j)); j = j + 1 }
                i = i + 1
            }
            return out
        }
        Blockquote(ch) => {
            Vec(Str) out = {}
            int i = 0
            while i < ch.len {
                Vec(Str) ls = _block_links(ch.get!(i))
                int j = 0
                while j < ls.len { out.push(ls.get!(j)); j = j + 1 }
                i = i + 1
            }
            return out
        }
        CodeBlock(_, _) => { Vec(Str) e = {}; return e }
        Table(_, _)     => { Vec(Str) e = {}; return e }
        HorizontalRule  => { Vec(Str) e = {}; return e }
    }
}

fn _block_plain(MdBlock b) -> Str {
    match b {
        Heading(_, c)         => { return _inlines_plain(c) }
        Paragraph(c)          => { return _inlines_plain(c) }
        CodeBlock(_, code)    => { return code.copy() }
        UnorderedList(items)  => {
            Str s = ""
            int i = 0
            while i < items.len {
                s.push_str(_inlines_plain(items.get!(i)))
                s.push_str("\n")
                i = i + 1
            }
            return s
        }
        OrderedList(items) => {
            Str s = ""
            int i = 0
            while i < items.len {
                s.push_str(_inlines_plain(items.get!(i)))
                s.push_str("\n")
                i = i + 1
            }
            return s
        }
        Blockquote(ch) => {
            Str s = ""
            int i = 0
            while i < ch.len {
                s.push_str(_block_plain(ch.get!(i)))
                s.push_str("\n")
                i = i + 1
            }
            return s
        }
        Table(_, rows) => {
            Str s = ""
            int r = 0
            while r < rows.len {
                Vec(Str) row = rows.get!(r)
                int c = 0
                while c < row.len {
                    s.push_str(row.get!(c))
                    s.push_str(" ")
                    c = c + 1
                }
                s.push_str("\n")
                r = r + 1
            }
            return s
        }
        HorizontalRule => { return "" }
    }
}

// Heading's plain text, or "" if `b` is not a heading (owned-param + return).
fn _heading_text(MdBlock b) -> Str {
    match b {
        Heading(_, c)    => { return _inlines_plain(c) }
        Paragraph(_)     => { return "" }
        CodeBlock(_, _)  => { return "" }
        UnorderedList(_) => { return "" }
        OrderedList(_)   => { return "" }
        Blockquote(_)    => { return "" }
        Table(_, _)      => { return "" }
        HorizontalRule   => { return "" }
    }
}

// Collect each heading's plain text, in document order. (Empty headings skipped.)
fn extract_headings(&MdDoc d) -> Vec(Str) {
    Vec(Str) out = {}
    int i = 0
    while i < d.blocks.len {
        Str h = _heading_text(d.blocks.get!(i))
        if h.len() > 0 { out.push(h) }
        i = i + 1
    }
    return out
}

// Collect every link/image URL (recurses into blockquotes).
fn extract_links(&MdDoc d) -> Vec(Str) {
    Vec(Str) out = {}
    int i = 0
    while i < d.blocks.len {
        Vec(Str) ls = _block_links(d.blocks.get!(i))
        int j = 0
        while j < ls.len { out.push(ls.get!(j)); j = j + 1 }
        i = i + 1
    }
    return out
}

// Whole document as plain text (markup stripped).
fn to_plain_text(&MdDoc d) -> Str {
    Str out = ""
    int i = 0
    while i < d.blocks.len {
        out.push_str(_block_plain(d.blocks.get!(i)))
        out.push_str("\n")
        i = i + 1
    }
    return out
}

// ---- String fragment helpers (do not touch MdDoc) ----

fn fmt_heading(int level, Str text) -> Str {
    int lv = level
    if lv < 1 { lv = 1 }
    if lv > 6 { lv = 6 }
    Str r = _repeat_char('#', lv)
    r.push_str(" ")
    r.push_str(text)
    return r
}

fn fmt_bold(Str s) -> Str {
    Str r = "**"; r.push_str(s); r.push_str("**"); return r
}

fn fmt_italic(Str s) -> Str {
    Str r = "_"; r.push_str(s); r.push_str("_"); return r
}

fn fmt_code(Str s) -> Str {
    Str r = "`"; r.push_str(s); r.push_str("`"); return r
}

fn fmt_link(Str text, Str url) -> Str {
    Str r = "["; r.push_str(text); r.push_str("]("); r.push_str(url); r.push_str(")")
    return r
}

fn fmt_image(Str alt, Str url) -> Str {
    Str r = "!["; r.push_str(alt); r.push_str("]("); r.push_str(url); r.push_str(")")
    return r
}

// ====================================================================
// Phase H3 — Markdown -> HTML. Parses the source into MdBlock/MdInline,
// then emits HTML strings directly (no HtmlNode intermediate). Kept inside
// std.md so the conversion has zero cross-module dependency on std.html
// (per docs/plan_std_html.md §5). HTML escaping is inlined here.
// ====================================================================

fn _html_escape(Str s) -> Str {
    Str r = ""
    int i = 0
    int n = s.len()
    while i < n {
        int c = s.byte_at(i)
        if c == '&' { r.push_str("&amp;") }
        else if c == '<' { r.push_str("&lt;") }
        else if c == '>' { r.push_str("&gt;") }
        else { r.push_byte(c) }
        i = i + 1
    }
    return r
}

fn _html_escape_attr(Str s) -> Str {
    Str r = ""
    int i = 0
    int n = s.len()
    while i < n {
        int c = s.byte_at(i)
        if c == '&' { r.push_str("&amp;") }
        else if c == '<' { r.push_str("&lt;") }
        else if c == '>' { r.push_str("&gt;") }
        else if c == '"' { r.push_str("&quot;") }
        else { r.push_byte(c) }
        i = i + 1
    }
    return r
}

fn _html_inline(MdInline x) -> Str {
    match x {
        Text(c)       => { return _html_escape(c) }
        Bold(c)       => {
            Str r = "<strong>"; r.push_str(_html_escape(c)); r.push_str("</strong>"); return r
        }
        Italic(c)     => {
            Str r = "<em>"; r.push_str(_html_escape(c)); r.push_str("</em>"); return r
        }
        BoldItalic(c) => {
            Str r = "<strong><em>"; r.push_str(_html_escape(c)); r.push_str("</em></strong>"); return r
        }
        Code(c)       => {
            Str r = "<code>"; r.push_str(_html_escape(c)); r.push_str("</code>"); return r
        }
        Link(t, u)    => {
            Str r = "<a href=\""; r.push_str(_html_escape_attr(u)); r.push_str("\">")
            r.push_str(_html_escape(t)); r.push_str("</a>")
            return r
        }
        Image(a, u)   => {
            Str r = "<img src=\""; r.push_str(_html_escape_attr(u)); r.push_str("\" alt=\"")
            r.push_str(_html_escape_attr(a)); r.push_str("\">")
            return r
        }
    }
}

fn _html_inlines(Vec(MdInline) inls) -> Str {
    Str out = ""
    int i = 0
    while i < inls.len {
        out.push_str(_html_inline(inls.get!(i)))
        i = i + 1
    }
    return out
}

fn _html_table(Vec(Str) headers, Vec(Vec(Str)) rows) -> Str {
    Str r = "<table>\n<thead>\n<tr>"
    int c = 0
    while c < headers.len {
        r.push_str("<th>")
        r.push_str(_html_escape(headers.get!(c)))
        r.push_str("</th>")
        c = c + 1
    }
    r.push_str("</tr>\n</thead>\n<tbody>\n")
    int rr = 0
    while rr < rows.len {
        Vec(Str) row = rows.get!(rr)
        r.push_str("<tr>")
        int cc = 0
        while cc < headers.len {
            Str cell = ""
            if cc < row.len { cell = row.get!(cc) }
            r.push_str("<td>")
            r.push_str(_html_escape(cell))
            r.push_str("</td>")
            cc = cc + 1
        }
        r.push_str("</tr>\n")
        rr = rr + 1
    }
    r.push_str("</tbody>\n</table>\n")
    return r
}

fn _html_block(MdBlock b) -> Str {
    match b {
        Heading(level, content) => {
            Str tag = f"h{level}"
            Str r = "<"; r.push_str(tag); r.push_str(">")
            r.push_str(_html_inlines(content))
            r.push_str("</"); r.push_str(tag); r.push_str(">\n")
            return r
        }
        Paragraph(content) => {
            Str r = "<p>"
            r.push_str(_html_inlines(content))
            r.push_str("</p>\n")
            return r
        }
        CodeBlock(lang, code) => {
            Str r = "<pre><code"
            if lang.len() > 0 {
                r.push_str(" class=\"language-")
                r.push_str(_html_escape_attr(lang))
                r.push_str("\"")
            }
            r.push_str(">")
            r.push_str(_html_escape(code))
            r.push_str("</code></pre>\n")
            return r
        }
        UnorderedList(items) => {
            Str r = "<ul>\n"
            int i = 0
            while i < items.len {
                r.push_str("<li>")
                r.push_str(_html_inlines(items.get!(i)))
                r.push_str("</li>\n")
                i = i + 1
            }
            r.push_str("</ul>\n")
            return r
        }
        OrderedList(items) => {
            Str r = "<ol>\n"
            int i = 0
            while i < items.len {
                r.push_str("<li>")
                r.push_str(_html_inlines(items.get!(i)))
                r.push_str("</li>\n")
                i = i + 1
            }
            r.push_str("</ol>\n")
            return r
        }
        Blockquote(children) => {
            Str r = "<blockquote>\n"
            int i = 0
            while i < children.len {
                r.push_str(_html_block(children.get!(i)))
                i = i + 1
            }
            r.push_str("</blockquote>\n")
            return r
        }
        Table(headers, rows) => {
            return _html_table(headers, rows)
        }
        HorizontalRule => {
            return "<hr>\n"
        }
    }
}

// Render an already-parsed document to an HTML fragment. Borrows the doc
// (mirrors `render`), so the owning frame drops it exactly once.
fn render_html(&MdDoc d) -> Str {
    Str out = ""
    int i = 0
    while i < d.blocks.len {
        out.push_str(_html_block(d.blocks.get!(i)))
        i = i + 1
    }
    return out
}

// Convert a Markdown source string to an HTML fragment (no <html> wrapper).
fn to_html(Str source) -> Str {
    MdDoc d = parse(source)
    return render_html(d)
}

// Convert a Markdown source to a full HTML document with <!DOCTYPE> + <head>.
fn to_html_full(Str source, Str title) -> Str {
    Str body = to_html(source)
    Str r = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>"
    r.push_str(_html_escape(title))
    r.push_str("</title>\n</head>\n<body>\n")
    r.push_str(body)
    r.push_str("</body>\n</html>\n")
    return r
}
