// Phase H2: HTML parsing (recursive-descent, tolerant) + round-trip + queries.
// Self-verifying: prints "HTML PASS" only if every check holds.

import std.html as html
import io

fn check(bool cond, int id) -> bool {
    if !cond {
        print(id)
        print("HTML FAIL")
    }
    return cond
}

fn main() {
    bool ok = true

    // --- simple element round-trip ---
    html.HtmlDoc d1 = html.parse("<p>hello</p>")
    if !check(html.render(d1) == "<p>hello</p>", 1) { ok = false }

    // --- nested element + quoted attribute ---
    html.HtmlDoc d2 = html.parse("<div class=\"box\"><span>hi</span></div>")
    if !check(html.render(d2) == "<div class=\"box\"><span>hi</span></div>", 2) { ok = false }

    // --- void tag + self-closing tag ---
    html.HtmlDoc d3 = html.parse("<img src=\"a.png\" alt=\"x\"><br/>")
    if !check(html.render(d3) == "<img src=\"a.png\" alt=\"x\"><br>", 3) { ok = false }

    // --- entity decode in text (&lt; &amp; &gt;) ---
    html.HtmlDoc d4 = html.parse("<p>a &lt; b &amp; c &gt; d</p>")
    if !check(html.to_text(d4) == "a < b & c > d", 4) { ok = false }

    // --- boolean attribute + comment ---
    html.HtmlDoc d5 = html.parse("<input disabled><!-- note -->")
    if !check(html.render(d5) == "<input disabled><!-- note -->", 5) { ok = false }

    // --- DOCTYPE + document structure round-trip ---
    html.HtmlDoc d6 = html.parse("<!DOCTYPE html><html><body><p>x</p></body></html>")
    if !check(html.render(d6) == "<!DOCTYPE html><html><body><p>x</p></body></html>", 6) { ok = false }

    // --- <script> body kept verbatim (raw, not escaped/parsed) ---
    html.HtmlDoc d7 = html.parse("<script>if (a < b && c) x();</script>")
    if !check(html.render(d7) == "<script>if (a < b && c) x();</script>", 7) { ok = false }

    // --- single-quoted + unquoted attribute values normalize to quoted ---
    html.HtmlDoc d8 = html.parse("<a href='u' id=main>t</a>")
    if !check(html.render(d8) == "<a href=\"u\" id=\"main\">t</a>", 8) { ok = false }

    // --- numeric entities (decimal + hex) ---
    html.HtmlDoc d9 = html.parse("<p>&#60;&#x3E;&#38;</p>")
    if !check(html.to_text(d9) == "<>&", 9) { ok = false }

    // --- attribute value entity decode ---
    html.HtmlDoc d10 = html.parse("<a href=\"a&amp;b\">t</a>")
    vec(html.HtmlNode) a10 = html.find_by_tag(d10, "a")
    if !check(a10.length == 1, 10) { ok = false }
    if !check(html.get_attr(a10.get(0), "href") == "a&b", 11) { ok = false }

    // --- extract_links across nesting ---
    html.HtmlDoc d12 = html.parse("<a href=\"u1\">x</a><div><a href=\"u2\">y</a></div>")
    vec(string) links = html.extract_links(d12)
    if !check(links.length == 2, 12) { ok = false }
    if !check(links.get(0) == "u1", 13) { ok = false }
    if !check(links.get(1) == "u2", 14) { ok = false }

    // --- find_by_tag collects all matches (pre-order) ---
    html.HtmlDoc d15 = html.parse("<ul><li>a</li><li>b</li><li>c</li></ul>")
    vec(html.HtmlNode) lis = html.find_by_tag(d15, "li")
    if !check(lis.length == 3, 15) { ok = false }

    // --- tolerant: mismatched close tag auto-closes inner element ---
    html.HtmlDoc d16 = html.parse("<div><b>bold</div>")
    if !check(html.render(d16) == "<div><b>bold</b></div>", 16) { ok = false }

    // --- tolerant: unclosed elements at EOF ---
    html.HtmlDoc d17 = html.parse("<ul><li>a<li>b")
    vec(html.HtmlNode) lis2 = html.find_by_tag(d17, "li")
    if !check(lis2.length == 2, 17) { ok = false }

    // --- round-trip: parse -> render -> parse renders identically ---
    string src = "<div id=\"main\"><p>one</p><p>two</p></div>"
    html.HtmlDoc rt1 = html.parse(src)
    string once = html.render(rt1)
    html.HtmlDoc rt2 = html.parse(once)
    if !check(html.render(rt2) == once, 18) { ok = false }
    if !check(once == src, 19) { ok = false }

    // --- to_text concatenates nested text, skips comments ---
    html.HtmlDoc d20 = html.parse("<div>Hi <b>there</b><!--c--> world</div>")
    if !check(html.to_text(d20) == "Hi there world", 20) { ok = false }

    if ok { print("HTML PASS") }
}
