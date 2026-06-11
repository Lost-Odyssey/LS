// std/html.ls — HTML reader/writer for LS.
// Pure LS; recursive render + (Phase H2) hand-written recursive-descent parser,
// mirroring std/json.ls and std/md.ls.
//
// Design: docs/plan_std_html.md
//  - HtmlNode is a self-recursive enum whose Element variant holds a
//    Vec(Attr) of attributes and a Vec(HtmlNode) of children.
//  - Attributes use Vec(Attr) rather than a hash table: Vec(Attr) iterates
//    correctly AND preserves insertion
//    order, so render/parse round-trip is byte-stable.
//  - Construction is bottom-up (build children first, then compose) — LS has
//    value semantics, so there is no "insert then mutate" the way a reference
//    language would do it.
//  - HtmlDoc is a forest: Vec(HtmlNode) roots (handles DOCTYPE + <html>, and
//    bare fragments).
//
// P7-mig: all string workflows migrated to the pure-LS `Str` (std.str).
// By-value `Str` params arrive as owned clones, so the old `.copy()` calls on
// params/field-reads are gone (the clone already happened); explicit `.copy()`
// remains only where a borrowed match binder must yield an independent value.

import std.vec
import io
import std.str

// ---- Core types ----

struct Attr {
    Str key
    Str val      // "" means a valueless (boolean) attribute, e.g. disabled
}

enum HtmlNode {
    Element(Str tag, Vec(Attr) attrs, Vec(HtmlNode) children)
    Text(Str content)        // text between tags (escaped on render)
    Comment(Str content)     // <!-- ... -->; "DOCTYPE html" renders as <!DOCTYPE html>
    RawText(Str content)     // <script>/<style> body: not escaped, not parsed
}

struct HtmlDoc {
    Vec(HtmlNode) roots
}

// ---- Constructors (bottom-up, functional) ----

fn text(Str s) -> HtmlNode { return Text(s) }
fn comment(Str s) -> HtmlNode { return Comment(s) }
fn raw(Str s) -> HtmlNode { return RawText(s) }

// Single attribute.
fn attr(Str k, Str v) -> Attr { return Attr { key: k, val: v } }

// Build an attribute list from [[key, value], ...] pairs (LS has no named args).
fn attrs(Vec(Vec(Str)) pairs) -> Vec(Attr) {
    Vec(Attr) out = {}
    int i = 0
    while i < pairs.len {
        Vec(Str) p = pairs.get(i)
        if p.len >= 2 {
            out.push(Attr { key: p.get(0), val: p.get(1) })
        } else if p.len == 1 {
            out.push(Attr { key: p.get(0), val: "" })   // boolean attribute
        }
        i = i + 1
    }
    return out
}

// Element with no attributes. `children` is moved into the node.
fn elem(Str tag, Vec(HtmlNode) children) -> HtmlNode {
    Vec(Attr) a = {}
    return Element(tag, a, children)
}

// Element with attributes. Both `a` and `children` are moved into the node.
fn elem_attr(Str tag, Vec(Attr) a, Vec(HtmlNode) children) -> HtmlNode {
    return Element(tag, a, children)
}

// ---- Convenience constructors ----

fn div(Vec(HtmlNode) children) -> HtmlNode { return elem("div", children) }
fn span(Vec(HtmlNode) children) -> HtmlNode { return elem("span", children) }
fn ul(Vec(HtmlNode) children) -> HtmlNode { return elem("ul", children) }
fn ol(Vec(HtmlNode) children) -> HtmlNode { return elem("ol", children) }
fn li(Vec(HtmlNode) children) -> HtmlNode { return elem("li", children) }

// <hN>text</hN>
fn h(int level, Str s) -> HtmlNode {
    int lv = level
    if lv < 1 { lv = 1 }
    if lv > 6 { lv = 6 }
    Str tag = f"h{lv}"
    Vec(Attr) a = {}
    Vec(HtmlNode) c = {}
    c.push(Text(s))
    return Element(tag, a, c)
}

// <p>text</p>
fn p(Str s) -> HtmlNode {
    Vec(Attr) a = {}
    Vec(HtmlNode) c = {}
    c.push(Text(s))
    return Element("p", a, c)
}

// <a href="url">text</a>
fn a(Str anchor_text, Str url) -> HtmlNode {
    Vec(Attr) at = {}
    at.push(Attr { key: "href", val: url })
    Vec(HtmlNode) c = {}
    c.push(Text(anchor_text))
    return Element("a", at, c)
}

// <img src="src" alt="alt"> (void)
fn img(Str src, Str alt) -> HtmlNode {
    Vec(Attr) at = {}
    at.push(Attr { key: "src", val: src })
    at.push(Attr { key: "alt", val: alt })
    Vec(HtmlNode) c = {}
    return Element("img", at, c)
}

fn br() -> HtmlNode {
    Vec(Attr) a = {}
    Vec(HtmlNode) c = {}
    return Element("br", a, c)
}

fn hr() -> HtmlNode {
    Vec(Attr) a = {}
    Vec(HtmlNode) c = {}
    return Element("hr", a, c)
}

// ---- Document wrappers ----

fn document(Vec(HtmlNode) roots) -> HtmlDoc { return HtmlDoc { roots: roots } }
fn fragment(Vec(HtmlNode) nodes) -> HtmlDoc { return HtmlDoc { roots: nodes } }

// ---- Escaping ----

// Escape text content: & < >  (read-only by-value param, like md.ls _pad_right)
fn escape(Str s) -> Str {
    Str r = ""
    int i = 0
    int n = s.len()
    while i < n {
        int ch = s.byte_at(i)
        if ch == '&' { r.push_str("&amp;") }
        else if ch == '<' { r.push_str("&lt;") }
        else if ch == '>' { r.push_str("&gt;") }
        else { r.push_byte(ch) }
        i = i + 1
    }
    return r
}

// Escape an attribute value: also encodes the double quote.
fn _escape_attr(Str s) -> Str {
    Str r = ""
    int i = 0
    int n = s.len()
    while i < n {
        int ch = s.byte_at(i)
        if ch == '&' { r.push_str("&amp;") }
        else if ch == '<' { r.push_str("&lt;") }
        else if ch == '>' { r.push_str("&gt;") }
        else if ch == '"' { r.push_str("&quot;") }
        else { r.push_byte(ch) }
        i = i + 1
    }
    return r
}

// ---- Void (self-closing) tag set ----

fn _is_void(Str tag) -> bool {
    return tag == "br" || tag == "hr" || tag == "img" || tag == "input"
        || tag == "meta" || tag == "link" || tag == "area" || tag == "base"
        || tag == "col" || tag == "embed" || tag == "source" || tag == "wbr"
}

// ---- Render (compact) ----

fn _render_node(HtmlNode n) -> Str {
    match n {
        Element(tag, attrs, children) => {
            Str r = "<"
            r.push_str(tag)
            int j = 0
            while j < attrs.len {
                Attr at = attrs.get(j)
                r.push_str(" ")
                r.push_str(at.key)
                if at.val.len() > 0 {
                    r.push_str("=\"")
                    r.push_str(_escape_attr(at.val))
                    r.push_str("\"")
                }
                j = j + 1
            }
            if _is_void(tag) {
                r.push_str(">")
                return r
            }
            r.push_str(">")
            int i = 0
            while i < children.len {
                r.push_str(_render_node(children.get(i)))
                i = i + 1
            }
            r.push_str("</")
            r.push_str(tag)
            r.push_str(">")
            return r
        }
        Text(c)    => { return escape(c) }
        Comment(c) => {
            if c == "DOCTYPE html" { return "<!DOCTYPE html>" }
            Str r = "<!--"
            r.push_str(c)
            r.push_str("-->")
            return r
        }
        RawText(c) => { return c.copy() }
    }
}

fn render(&HtmlDoc d) -> Str {
    Str out = ""
    int i = 0
    while i < d.roots.len {
        out.push_str(_render_node(d.roots.get(i)))
        i = i + 1
    }
    return out
}

// ---- Render (pretty / indented) ----
// Simple indentation: each node on its own line at depth*step spaces. Not
// inline-aware (every Element breaks), but readable for debugging/inspection.

fn _spaces(int n) -> Str {
    Str s = ""
    int i = 0
    while i < n {
        s.push_byte(' ')
        i = i + 1
    }
    return s
}

fn _render_node_pretty(HtmlNode n, int depth, int step) -> Str {
    Str pad = _spaces(depth * step)
    match n {
        Element(tag, attrs, children) => {
            Str r = pad.copy()
            r.push_str("<")
            r.push_str(tag)
            int j = 0
            while j < attrs.len {
                Attr at = attrs.get(j)
                r.push_str(" ")
                r.push_str(at.key)
                if at.val.len() > 0 {
                    r.push_str("=\"")
                    r.push_str(_escape_attr(at.val))
                    r.push_str("\"")
                }
                j = j + 1
            }
            if _is_void(tag) {
                r.push_str(">\n")
                return r
            }
            r.push_str(">\n")
            int i = 0
            while i < children.len {
                r.push_str(_render_node_pretty(children.get(i), depth + 1, step))
                i = i + 1
            }
            r.push_str(pad)
            r.push_str("</")
            r.push_str(tag)
            r.push_str(">\n")
            return r
        }
        Text(c) => {
            Str r = pad.copy()
            r.push_str(escape(c))
            r.push_str("\n")
            return r
        }
        Comment(c) => {
            Str r = pad.copy()
            if c == "DOCTYPE html" {
                r.push_str("<!DOCTYPE html>\n")
                return r
            }
            r.push_str("<!--")
            r.push_str(c)
            r.push_str("-->\n")
            return r
        }
        RawText(c) => {
            Str r = pad.copy()
            r.push_str(c)
            r.push_str("\n")
            return r
        }
    }
}

fn render_pretty(&HtmlDoc d, int step) -> Str {
    Str out = ""
    int i = 0
    while i < d.roots.len {
        out.push_str(_render_node_pretty(d.roots.get(i), 0, step))
        i = i + 1
    }
    return out
}

// ---- Pure-string helper (no tree) ----

// fmt_tag("a", [["href","u"]], "txt") -> <a href="u">txt</a>
fn fmt_tag(Str tag, Vec(Vec(Str)) attr_pairs, Str inner) -> Str {
    Str r = "<"
    r.push_str(tag)
    int i = 0
    while i < attr_pairs.len {
        Vec(Str) pr = attr_pairs.get(i)
        if pr.len >= 1 {
            r.push_str(" ")
            r.push_str(pr.get(0))
            if pr.len >= 2 {
                r.push_str("=\"")
                r.push_str(_escape_attr(pr.get(1)))
                r.push_str("\"")
            }
        }
        i = i + 1
    }
    if _is_void(tag) {
        r.push_str(">")
        return r
    }
    r.push_str(">")
    r.push_str(escape(inner))
    r.push_str("</")
    r.push_str(tag)
    r.push_str(">")
    return r
}

// ============================================================================
// Phase H2 — Parser (recursive descent, tolerant). Mirrors std/json.ls:
//   HtmlParser holds the input + cursor; `&!HtmlParser` mutating helpers scan it;
//   _parse_nodes / _parse_element are mutually recursive (LS forward-declares
//   function signatures before compiling bodies, like json's _parse_value group).
// No Result type: parsing is tolerant (no errors) — unknown/mismatched markup is
// recovered from, mirroring how lenient HTML parsers behave.
// ============================================================================

struct HtmlParser {
    Str input
    int pos
    int len
}

fn _hp_new(Str input) -> HtmlParser {
    int n = input.len()
    return HtmlParser { input: input, pos: 0, len: n }
}

fn _hp_peek(&!HtmlParser p) -> int {
    if p.pos >= p.len { return 0 - 1 }
    return p.input.byte_at(p.pos)
}

fn _hp_peek2(&!HtmlParser p) -> int {
    if p.pos + 1 >= p.len { return 0 - 1 }
    return p.input.byte_at(p.pos + 1)
}

fn _hp_skip_ws(&!HtmlParser p) {
    while p.pos < p.len {
        int ch = p.input.byte_at(p.pos)
        if ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' {
            p.pos = p.pos + 1
        } else {
            return
        }
    }
}

// Does the input starting at p.pos match literal `s`? (no consume)
fn _hp_matches(&!HtmlParser p, Str s) -> bool {
    int n = s.len()
    if p.pos + n > p.len { return false }
    int i = 0
    while i < n {
        if p.input.byte_at(p.pos + i) != s.byte_at(i) { return false }
        i = i + 1
    }
    return true
}

// Case-insensitive variant of _hp_matches (`s` is assumed already lowercase).
fn _hp_matches_ci(&!HtmlParser p, Str s) -> bool {
    int n = s.len()
    if p.pos + n > p.len { return false }
    int i = 0
    while i < n {
        int c = p.input.byte_at(p.pos + i)
        if c >= 'A' && c <= 'Z' { c = c + 32 }
        if c != s.byte_at(i) { return false }
        i = i + 1
    }
    return true
}

fn _hp_lower(Str s) -> Str {
    return s.lower()
}

fn _hp_is_name_end(int c) -> bool {
    return c < 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r'
        || c == '>' || c == '/' || c == '='
}

fn _hp_scan_tag_name(&!HtmlParser p) -> Str {
    int start = p.pos
    while p.pos < p.len && !_hp_is_name_end(p.input.byte_at(p.pos)) {
        p.pos = p.pos + 1
    }
    return p.input.substr(start, p.pos - start)
}

// ---- Entity decoding (&amp; &lt; &gt; &quot; &apos; &#NN; &#xHH;) ----
// Numeric entities are emitted as a single byte (ASCII/Latin-1 range); code
// points > 255 are kept literally. Sufficient for the documented P1 subset.

fn _decode_numeric_entity(Str ent) -> int {
    // `ent` is the text between '&' and ';', starting with '#'.
    int n = ent.len()
    if n < 2 { return 0 - 1 }
    if ent.byte_at(1) == 'x' || ent.byte_at(1) == 'X' {
        int v = 0
        int i = 2
        if i >= n { return 0 - 1 }
        while i < n {
            int c = ent.byte_at(i)
            int d = 0 - 1
            if c >= '0' && c <= '9' { d = c - '0' }
            else if c >= 'a' && c <= 'f' { d = c - 'a' + 10 }
            else if c >= 'A' && c <= 'F' { d = c - 'A' + 10 }
            if d < 0 { return 0 - 1 }
            v = v * 16 + d
            i = i + 1
        }
        if v > 255 { return 0 - 1 }
        return v
    }
    int v = 0
    int i = 1
    while i < n {
        int c = ent.byte_at(i)
        if c < '0' || c > '9' { return 0 - 1 }
        v = v * 10 + (c - '0')
        i = i + 1
    }
    if v > 255 { return 0 - 1 }
    return v
}

fn _decode_entities(Str s) -> Str {
    Str r = ""
    int i = 0
    int n = s.len()
    while i < n {
        int c = s.byte_at(i)
        if c != '&' {
            r.push_byte(c)
            i = i + 1
            continue
        }
        // Find the terminating ';' within a small window.
        int semi = 0 - 1
        int j = i + 1
        int limit = i + 12
        if limit > n { limit = n }
        while j < limit {
            if s.byte_at(j) == ';' { semi = j; j = limit } else { j = j + 1 }
        }
        if semi < 0 {
            r.push_byte(c)               // lone '&'
            i = i + 1
            continue
        }
        Str ent = s.substr(i + 1, semi - i - 1)
        if ent == "amp" { r.push_byte('&') }
        else if ent == "lt" { r.push_byte('<') }
        else if ent == "gt" { r.push_byte('>') }
        else if ent == "quot" { r.push_byte('"') }
        else if ent == "apos" { r.push_byte('\'') }
        else if ent.len() >= 2 && ent.byte_at(0) == '#' {
            int code = _decode_numeric_entity(ent)
            if code >= 0 {
                r.push_byte(code)
            } else {
                r.push_byte('&')  r.push_str(ent)  r.push_byte(';')
            }
        }
        else {
            r.push_byte('&')  r.push_str(ent)  r.push_byte(';')   // unknown: keep literal
        }
        i = semi + 1
    }
    return r
}

// ---- Attribute parsing ----

fn _hp_is_attr_name_end(int c) -> bool {
    return c < 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r'
        || c == '=' || c == '>' || c == '/'
}

fn _hp_scan_attr_name(&!HtmlParser p) -> Str {
    int start = p.pos
    while p.pos < p.len && !_hp_is_attr_name_end(p.input.byte_at(p.pos)) {
        p.pos = p.pos + 1
    }
    return p.input.substr(start, p.pos - start)
}

fn _hp_scan_attr_value(&!HtmlParser p) -> Str {
    int q = _hp_peek(&!p)
    if q == '"' || q == '\'' {
        p.pos = p.pos + 1                 // consume opening quote
        int start = p.pos
        while p.pos < p.len && p.input.byte_at(p.pos) != q {
            p.pos = p.pos + 1
        }
        Str raw = p.input.substr(start, p.pos - start)
        if p.pos < p.len { p.pos = p.pos + 1 }   // consume closing quote
        return _decode_entities(raw)
    }
    // unquoted: scan until whitespace, '>' or '/'
    int start = p.pos
    while p.pos < p.len {
        int c = p.input.byte_at(p.pos)
        if c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>' || c == '/' {
            break
        }
        p.pos = p.pos + 1
    }
    Str raw = p.input.substr(start, p.pos - start)
    return _decode_entities(raw)
}

fn _parse_attrs(&!HtmlParser p) -> Vec(Attr) {
    Vec(Attr) out = {}
    while p.pos < p.len {
        _hp_skip_ws(&!p)
        int c = _hp_peek(&!p)
        if c < 0 || c == '>' || c == '/' { break }
        Str name = _hp_scan_attr_name(&!p)
        if name.len() == 0 {
            p.pos = p.pos + 1            // safety: skip stray char, avoid infinite loop
            continue
        }
        _hp_skip_ws(&!p)
        Str val = ""
        if _hp_peek(&!p) == '=' {
            p.pos = p.pos + 1            // consume '='
            _hp_skip_ws(&!p)
            val = _hp_scan_attr_value(&!p)
        }
        out.push(Attr { key: name, val: val })
    }
    return out
}

// ---- Node parsing ----

fn _parse_comment(&!HtmlParser p) -> HtmlNode {
    p.pos = p.pos + 4                    // consume "<!--"
    int start = p.pos
    while p.pos < p.len {
        if p.input.byte_at(p.pos) == '-' && _hp_matches(&!p, "-->") { break }
        p.pos = p.pos + 1
    }
    Str body = p.input.substr(start, p.pos - start)
    if _hp_matches(&!p, "-->") { p.pos = p.pos + 3 }
    return Comment(body)
}

fn _parse_doctype(&!HtmlParser p) -> HtmlNode {
    p.pos = p.pos + 2                    // consume "<!"
    int start = p.pos
    while p.pos < p.len && p.input.byte_at(p.pos) != '>' {
        p.pos = p.pos + 1
    }
    Str body = p.input.substr(start, p.pos - start)
    if p.pos < p.len { p.pos = p.pos + 1 }   // consume '>'
    Str low = _hp_lower(body)
    if low == "doctype html" { return Comment("DOCTYPE html") }
    return Comment(body)
}

fn _parse_text(&!HtmlParser p) -> HtmlNode {
    int start = p.pos
    while p.pos < p.len && p.input.byte_at(p.pos) != '<' {
        p.pos = p.pos + 1
    }
    Str raw = p.input.substr(start, p.pos - start)
    return Text(_decode_entities(raw))
}

// Read <script>/<style> body verbatim up to (and consuming) the matching close.
fn _hp_scan_raw_until_close(&!HtmlParser p, Str tag) -> Str {
    Str close = "</"
    close.push_str(tag)                  // "</script"
    int start = p.pos
    while p.pos < p.len {
        if p.input.byte_at(p.pos) == '<' && _hp_matches_ci(&!p, close) { break }
        p.pos = p.pos + 1
    }
    Str body = p.input.substr(start, p.pos - start)
    if _hp_matches_ci(&!p, close) {
        p.pos = p.pos + close.len()
        while p.pos < p.len && p.input.byte_at(p.pos) != '>' { p.pos = p.pos + 1 }
        if p.pos < p.len { p.pos = p.pos + 1 }   // consume '>'
    }
    return body
}

fn _parse_element(&!HtmlParser p) -> HtmlNode {
    p.pos = p.pos + 1                    // consume '<'
    Str tag = _hp_lower(_hp_scan_tag_name(&!p))
    Vec(Attr) attrs = _parse_attrs(&!p)  // stops at '/' or '>' or EOF

    bool self_closed = false
    if _hp_peek(&!p) == '/' {
        self_closed = true
        p.pos = p.pos + 1                // consume '/'
    }
    if _hp_peek(&!p) == '>' { p.pos = p.pos + 1 }   // consume '>'

    if self_closed || _is_void(tag) {
        Vec(HtmlNode) empty = {}
        return Element(tag, attrs, empty)
    }
    if tag == "script" || tag == "style" {
        Str raw = _hp_scan_raw_until_close(&!p, tag)
        Vec(HtmlNode) rc = {}
        rc.push(RawText(raw))
        return Element(tag, attrs, rc)
    }
    Vec(HtmlNode) children = _parse_nodes(&!p, tag)
    return Element(tag, attrs, children)
}

// Parse a run of sibling nodes until the matching `</close_tag>` (consumed) or
// EOF. `close_tag == ""` means top level: a stray close tag is skipped.
fn _parse_nodes(&!HtmlParser p, Str close_tag) -> Vec(HtmlNode) {
    Vec(HtmlNode) nodes = {}
    while p.pos < p.len {
        int c = p.input.byte_at(p.pos)
        if c == '<' {
            int nx = _hp_peek2(&!p)
            if nx == '/' {
                int mark = p.pos
                p.pos = p.pos + 2        // consume "</"
                _hp_skip_ws(&!p)
                Str cname = _hp_lower(_hp_scan_tag_name(&!p))
                _hp_skip_ws(&!p)
                if _hp_peek(&!p) == '>' { p.pos = p.pos + 1 }   // consume '>'
                if close_tag.len() > 0 {
                    if cname == close_tag {
                        return nodes      // matched close: consumed, level done
                    }
                    p.pos = mark          // mismatch: restore, let ancestor close
                    return nodes
                }
                continue                  // top-level stray close: already consumed, ignore
            } else if _hp_matches(&!p, "<!--") {
                nodes.push(_parse_comment(&!p))
            } else if _hp_matches(&!p, "<!") {
                nodes.push(_parse_doctype(&!p))
            } else {
                nodes.push(_parse_element(&!p))
            }
        } else {
            nodes.push(_parse_text(&!p))
        }
    }
    return nodes                          // EOF: unclosed elements auto-close (tolerant)
}

fn parse(Str input) -> HtmlDoc {
    HtmlParser p = _hp_new(input)
    Vec(HtmlNode) roots = _parse_nodes(&!p, "")
    return HtmlDoc { roots: roots }
}

// ---- Query helpers ----

// Read one attribute from a node ("" if absent or not an element).
fn get_attr(HtmlNode n, Str key) -> Str {
    match n {
        Element(tag, attrs, children) => {
            int i = 0
            while i < attrs.len {
                Attr at = attrs.get(i)
                if at.key == key { return at.val }
                i = i + 1
            }
            return ""
        }
        _ => { return "" }
    }
}

// Concatenate the text of every Text node in the subtree (skips RawText/Comment).
fn _node_text(HtmlNode n) -> Str {
    match n {
        Element(tag, attrs, children) => {
            Str r = ""
            int i = 0
            while i < children.len {
                r.push_str(_node_text(children.get(i)))
                i = i + 1
            }
            return r
        }
        Text(c)    => { return c.copy() }
        Comment(c) => { return "" }
        RawText(c) => { return "" }
    }
}

fn to_text(&HtmlDoc d) -> Str {
    Str r = ""
    int i = 0
    while i < d.roots.len {
        r.push_str(_node_text(d.roots.get(i)))
        i = i + 1
    }
    return r
}

// Collect every href value from <a> elements in the subtree.
fn _collect_links(HtmlNode n) -> Vec(Str) {
    Vec(Str) acc = {}
    match n {
        Element(tag, attrs, children) => {
            if tag == "a" {
                int j = 0
                while j < attrs.len {
                    Attr at = attrs.get(j)
                    if at.key == "href" { acc.push(at.val) }
                    j = j + 1
                }
            }
            int i = 0
            while i < children.len {
                Vec(Str) sub = _collect_links(children.get(i))
                int k = 0
                while k < sub.len { acc.push(sub.get(k)); k = k + 1 }
                i = i + 1
            }
            return acc
        }
        _ => { return acc }
    }
}

fn extract_links(&HtmlDoc d) -> Vec(Str) {
    Vec(Str) acc = {}
    int i = 0
    while i < d.roots.len {
        Vec(Str) sub = _collect_links(d.roots.get(i))
        int k = 0
        while k < sub.len { acc.push(sub.get(k)); k = k + 1 }
        i = i + 1
    }
    return acc
}

// Recursively collect (pre-order) every element with the given tag, as
// independent deep copies.
fn _collect_by_tag(HtmlNode n, Str tag) -> Vec(HtmlNode) {
    Vec(HtmlNode) acc = {}
    match n {
        Element(t, attrs, children) => {
            if t == tag {
                // vec.get already returns an owned deep clone, so push it
                // directly — no intermediate named local (which, being
                // loop-body-scoped, was not dropped per iteration → leak).
                Vec(Attr) acopy = {}
                int a = 0
                while a < attrs.len {
                    acopy.push(attrs.get(a))
                    a = a + 1
                }
                Vec(HtmlNode) ccopy = {}
                int cc = 0
                while cc < children.len {
                    ccopy.push(children.get(cc))
                    cc = cc + 1
                }
                acc.push(Element(t.copy(), acopy, ccopy))
            }
            int i = 0
            while i < children.len {
                Vec(HtmlNode) sub = _collect_by_tag(children.get(i), tag)
                int k = 0
                while k < sub.len { acc.push(sub.get(k)); k = k + 1 }
                i = i + 1
            }
            return acc
        }
        _ => { return acc }
    }
}

fn find_by_tag(&HtmlDoc d, Str tag) -> Vec(HtmlNode) {
    Vec(HtmlNode) acc = {}
    int i = 0
    while i < d.roots.len {
        Vec(HtmlNode) sub = _collect_by_tag(d.roots.get(i), tag)
        int k = 0
        while k < sub.len { acc.push(sub.get(k)); k = k + 1 }
        i = i + 1
    }
    return acc
}
