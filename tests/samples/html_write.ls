// Phase H1: HTML generation (functional bottom-up construction) + render.
// Self-verifying: prints "HTML PASS" only if every check holds.

import std.vec
import std.html as html
import io

fn check(bool cond, int id) -> bool {
    if !cond {
        print(id)
        print("HTML FAIL")
    }
    return cond
}

fn frag1(html.HtmlNode n) -> html.HtmlDoc {
    Vec(html.HtmlNode) v = {}
    v.push(n)
    return html.fragment(v)
}

fn main() {
    bool ok = true

    // --- escape ---
    string e = html.escape("a<b>&c")
    if !check(e == "a&lt;b&gt;&amp;c", 1) { ok = false }

    // --- leaf text node (escaped) ---
    html.HtmlDoc d2 = frag1(html.text("x & y"))
    if !check(html.render(d2) == "x &amp; y", 2) { ok = false }

    // --- simple element ---
    html.HtmlDoc d3 = frag1(html.p("hello"))
    if !check(html.render(d3) == "<p>hello</p>", 3) { ok = false }

    // --- nested element bottom-up + attribute ---
    Vec(html.HtmlNode) span_kids = {}
    span_kids.push(html.text("hi"))
    html.HtmlNode sp = html.elem("span", span_kids)

    Vec(Vec(string)) ap = {}
    Vec(string) ap0 = {}
    ap0.push("class")
    ap0.push("box")
    ap.push(ap0)
    Vec(html.Attr) divAttrs = html.attrs(ap)

    Vec(html.HtmlNode) div_kids = {}
    div_kids.push(sp)
    div_kids.push(html.comment(" note "))
    html.HtmlNode dv = html.elem_attr("div", divAttrs, div_kids)
    html.HtmlDoc d4 = frag1(dv)
    if !check(html.render(d4) == "<div class=\"box\"><span>hi</span><!-- note --></div>", 4) { ok = false }

    // --- void element (img); attr order is insertion order (vec(Attr)) ---
    html.HtmlDoc d5 = frag1(html.img("p.jpg", "alt"))
    if !check(html.render(d5) == "<img src=\"p.jpg\" alt=\"alt\">", 5) { ok = false }

    // --- link + heading ---
    html.HtmlDoc d6 = frag1(html.a("Click", "u"))
    if !check(html.render(d6) == "<a href=\"u\">Click</a>", 6) { ok = false }
    html.HtmlDoc d7 = frag1(html.h(2, "Title"))
    if !check(html.render(d7) == "<h2>Title</h2>", 7) { ok = false }

    // --- DOCTYPE comment round-trips to <!DOCTYPE html> ---
    html.HtmlDoc d8 = frag1(html.comment("DOCTYPE html"))
    if !check(html.render(d8) == "<!DOCTYPE html>", 8) { ok = false }

    // --- attribute value escaping ---
    Vec(Vec(string)) qap = {}
    Vec(string) qap0 = {}
    qap0.push("title")
    qap0.push("a\"b<c")
    qap.push(qap0)
    Vec(html.Attr) qa = html.attrs(qap)
    Vec(html.HtmlNode) qkids = {}
    html.HtmlNode qn = html.elem_attr("p", qa, qkids)
    html.HtmlDoc d9 = frag1(qn)
    if !check(html.render(d9) == "<p title=\"a&quot;b&lt;c\"></p>", 9) { ok = false }

    // --- fmt_tag pure-string helper ---
    Vec(Vec(string)) ftp = {}
    Vec(string) ftp0 = {}
    ftp0.push("href")
    ftp0.push("u")
    ftp.push(ftp0)
    string ft = html.fmt_tag("a", ftp, "x<y")
    if !check(ft == "<a href=\"u\">x&lt;y</a>", 10) { ok = false }

    // --- document with nested tree + render ---
    Vec(html.HtmlNode) body_kids = {}
    body_kids.push(html.h(1, "T"))
    body_kids.push(html.p("para"))
    html.HtmlNode body = html.elem("body", body_kids)
    Vec(html.HtmlNode) htmlkids = {}
    htmlkids.push(body)
    Vec(html.HtmlNode) roots = {}
    roots.push(html.comment("DOCTYPE html"))
    roots.push(html.elem("html", htmlkids))
    html.HtmlDoc doc = html.document(roots)
    if !check(html.render(doc) == "<!DOCTYPE html><html><body><h1>T</h1><p>para</p></body></html>", 11) { ok = false }

    if ok { print("HTML PASS") }
}
