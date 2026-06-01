// std/html.ls — HTML reader/writer for LS.
// Pure LS; recursive render + (Phase H2) hand-written recursive-descent parser,
// mirroring std/json.ls and std/md.ls.
//
// Design: docs/plan_std_html.md
//  - HtmlNode is a self-recursive enum whose Element variant holds a
//    vec(Attr) of attributes and a vec(HtmlNode) of children.
//  - Attributes use vec(Attr), NOT map(string,string): map key-iteration does
//    not work in LS (see json.ls, which keeps a parallel vec(string) keys for
//    the same reason). vec(Attr) iterates correctly AND preserves insertion
//    order, so render/parse round-trip is byte-stable.
//  - Construction is bottom-up (build children first, then compose) — LS has
//    value semantics, so there is no "insert then mutate" the way a reference
//    language would do it.
//  - HtmlDoc is a forest: vec(HtmlNode) roots (handles DOCTYPE + <html>, and
//    bare fragments).

import io

// ---- Core types ----

struct Attr {
    string key
    string val      // "" means a valueless (boolean) attribute, e.g. disabled
}

enum HtmlNode {
    Element(string tag, vec(Attr) attrs, vec(HtmlNode) children)
    Text(string content)        // text between tags (escaped on render)
    Comment(string content)     // <!-- ... -->; "DOCTYPE html" renders as <!DOCTYPE html>
    RawText(string content)     // <script>/<style> body: not escaped, not parsed
}

struct HtmlDoc {
    vec(HtmlNode) roots
}

// ---- Constructors (bottom-up, functional) ----

fn text(string s) -> HtmlNode { return Text(s.copy()) }
fn comment(string s) -> HtmlNode { return Comment(s.copy()) }
fn raw(string s) -> HtmlNode { return RawText(s.copy()) }

// Single attribute.
fn attr(string k, string v) -> Attr { return Attr { key: k.copy(), val: v.copy() } }

// Build an attribute list from [[key, value], ...] pairs (LS has no named args).
fn attrs(vec(vec(string)) pairs) -> vec(Attr) {
    vec(Attr) out = []
    int i = 0
    while i < pairs.length {
        vec(string) p = pairs.get(i)
        if p.length >= 2 {
            out.push(Attr { key: p.get(0).copy(), val: p.get(1).copy() })
        } else if p.length == 1 {
            out.push(Attr { key: p.get(0).copy(), val: "" })   // boolean attribute
        }
        i = i + 1
    }
    return out
}

// Element with no attributes. `children` is moved into the node.
fn elem(string tag, vec(HtmlNode) children) -> HtmlNode {
    vec(Attr) a = []
    return Element(tag.copy(), a, children)
}

// Element with attributes. Both `a` and `children` are moved into the node.
fn elem_attr(string tag, vec(Attr) a, vec(HtmlNode) children) -> HtmlNode {
    return Element(tag.copy(), a, children)
}

// ---- Convenience constructors ----

fn div(vec(HtmlNode) children) -> HtmlNode { return elem("div", children) }
fn span(vec(HtmlNode) children) -> HtmlNode { return elem("span", children) }
fn ul(vec(HtmlNode) children) -> HtmlNode { return elem("ul", children) }
fn ol(vec(HtmlNode) children) -> HtmlNode { return elem("ol", children) }
fn li(vec(HtmlNode) children) -> HtmlNode { return elem("li", children) }

// <hN>text</hN>
fn h(int level, string s) -> HtmlNode {
    int lv = level
    if lv < 1 { lv = 1 }
    if lv > 6 { lv = 6 }
    string tag = f"h{lv}"
    vec(Attr) a = []
    vec(HtmlNode) c = []
    c.push(Text(s.copy()))
    return Element(tag, a, c)
}

// <p>text</p>
fn p(string s) -> HtmlNode {
    vec(Attr) a = []
    vec(HtmlNode) c = []
    c.push(Text(s.copy()))
    return Element("p", a, c)
}

// <a href="url">text</a>
fn a(string anchor_text, string url) -> HtmlNode {
    vec(Attr) at = []
    at.push(Attr { key: "href", val: url.copy() })
    vec(HtmlNode) c = []
    c.push(Text(anchor_text.copy()))
    return Element("a", at, c)
}

// <img src="src" alt="alt"> (void)
fn img(string src, string alt) -> HtmlNode {
    vec(Attr) at = []
    at.push(Attr { key: "src", val: src.copy() })
    at.push(Attr { key: "alt", val: alt.copy() })
    vec(HtmlNode) c = []
    return Element("img", at, c)
}

fn br() -> HtmlNode {
    vec(Attr) a = []
    vec(HtmlNode) c = []
    return Element("br", a, c)
}

fn hr() -> HtmlNode {
    vec(Attr) a = []
    vec(HtmlNode) c = []
    return Element("hr", a, c)
}

// ---- Document wrappers ----

fn document(vec(HtmlNode) roots) -> HtmlDoc { return HtmlDoc { roots: roots } }
fn fragment(vec(HtmlNode) nodes) -> HtmlDoc { return HtmlDoc { roots: nodes } }

// ---- Escaping ----

// Escape text content: & < >  (read-only by-value param, like md.ls _pad_right)
fn escape(string s) -> string {
    string r = ""
    int i = 0
    int n = s.length
    while i < n {
        int ch = s.at(i)
        if ch == '&' { r.append("&amp;") }
        else if ch == '<' { r.append("&lt;") }
        else if ch == '>' { r.append("&gt;") }
        else { r.append(ch) }
        i = i + 1
    }
    return r
}

// Escape an attribute value: also encodes the double quote.
fn _escape_attr(string s) -> string {
    string r = ""
    int i = 0
    int n = s.length
    while i < n {
        int ch = s.at(i)
        if ch == '&' { r.append("&amp;") }
        else if ch == '<' { r.append("&lt;") }
        else if ch == '>' { r.append("&gt;") }
        else if ch == '"' { r.append("&quot;") }
        else { r.append(ch) }
        i = i + 1
    }
    return r
}

// ---- Void (self-closing) tag set ----

fn _is_void(string tag) -> bool {
    return tag == "br" || tag == "hr" || tag == "img" || tag == "input"
        || tag == "meta" || tag == "link" || tag == "area" || tag == "base"
        || tag == "col" || tag == "embed" || tag == "source" || tag == "wbr"
}

// ---- Render (compact) ----

fn _render_node(HtmlNode n) -> string {
    match n {
        Element(tag, attrs, children) => {
            string r = "<"
            r.append(tag)
            int j = 0
            while j < attrs.length {
                Attr at = attrs.get(j)
                r.append(" ")
                r.append(at.key)
                if at.val.length > 0 {
                    r.append("=\"")
                    r.append(_escape_attr(at.val))
                    r.append("\"")
                }
                j = j + 1
            }
            if _is_void(tag) {
                r.append(">")
                return r
            }
            r.append(">")
            int i = 0
            while i < children.length {
                r.append(_render_node(children.get(i)))
                i = i + 1
            }
            r.append("</")
            r.append(tag)
            r.append(">")
            return r
        }
        Text(c)    => { return escape(c) }
        Comment(c) => {
            if c == "DOCTYPE html" { return "<!DOCTYPE html>" }
            string r = "<!--"
            r.append(c)
            r.append("-->")
            return r
        }
        RawText(c) => { return c.copy() }
    }
}

fn render(&HtmlDoc d) -> string {
    string out = ""
    int i = 0
    while i < d.roots.length {
        out.append(_render_node(d.roots.get(i)))
        i = i + 1
    }
    return out
}

// ---- Render (pretty / indented) ----
// Simple indentation: each node on its own line at depth*step spaces. Not
// inline-aware (every Element breaks), but readable for debugging/inspection.

fn _spaces(int n) -> string {
    string s = ""
    int i = 0
    while i < n {
        s.append(" ")
        i = i + 1
    }
    return s
}

fn _render_node_pretty(HtmlNode n, int depth, int step) -> string {
    string pad = _spaces(depth * step)
    match n {
        Element(tag, attrs, children) => {
            string r = pad.copy()
            r.append("<")
            r.append(tag)
            int j = 0
            while j < attrs.length {
                Attr at = attrs.get(j)
                r.append(" ")
                r.append(at.key)
                if at.val.length > 0 {
                    r.append("=\"")
                    r.append(_escape_attr(at.val))
                    r.append("\"")
                }
                j = j + 1
            }
            if _is_void(tag) {
                r.append(">\n")
                return r
            }
            r.append(">\n")
            int i = 0
            while i < children.length {
                r.append(_render_node_pretty(children.get(i), depth + 1, step))
                i = i + 1
            }
            r.append(pad)
            r.append("</")
            r.append(tag)
            r.append(">\n")
            return r
        }
        Text(c) => {
            string r = pad.copy()
            r.append(escape(c))
            r.append("\n")
            return r
        }
        Comment(c) => {
            string r = pad.copy()
            if c == "DOCTYPE html" {
                r.append("<!DOCTYPE html>\n")
                return r
            }
            r.append("<!--")
            r.append(c)
            r.append("-->\n")
            return r
        }
        RawText(c) => {
            string r = pad.copy()
            r.append(c)
            r.append("\n")
            return r
        }
    }
}

fn render_pretty(&HtmlDoc d, int step) -> string {
    string out = ""
    int i = 0
    while i < d.roots.length {
        out.append(_render_node_pretty(d.roots.get(i), 0, step))
        i = i + 1
    }
    return out
}

// ---- Pure-string helper (no tree) ----

// fmt_tag("a", [["href","u"]], "txt") -> <a href="u">txt</a>
fn fmt_tag(string tag, vec(vec(string)) attr_pairs, string inner) -> string {
    string r = "<"
    r.append(tag)
    int i = 0
    while i < attr_pairs.length {
        vec(string) pr = attr_pairs.get(i)
        if pr.length >= 1 {
            r.append(" ")
            r.append(pr.get(0))
            if pr.length >= 2 {
                r.append("=\"")
                r.append(_escape_attr(pr.get(1)))
                r.append("\"")
            }
        }
        i = i + 1
    }
    if _is_void(tag) {
        r.append(">")
        return r
    }
    r.append(">")
    r.append(escape(inner))
    r.append("</")
    r.append(tag)
    r.append(">")
    return r
}
