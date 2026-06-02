// Phase H3: Markdown -> HTML conversion (std.md.to_html / to_html_full).
// Self-verifying: prints "HTML PASS" only if every check holds.

import std.md as md
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

    // --- headings ---
    if !check(md.to_html("# Hello") == "<h1>Hello</h1>\n", 1) { ok = false }
    if !check(md.to_html("### Three") == "<h3>Three</h3>\n", 2) { ok = false }

    // --- paragraph ---
    if !check(md.to_html("plain text") == "<p>plain text</p>\n", 3) { ok = false }

    // --- inline: bold / italic / code ---
    if !check(md.to_html("a **bold** b") == "<p>a <strong>bold</strong> b</p>\n", 4) { ok = false }
    if !check(md.to_html("x _em_ y") == "<p>x <em>em</em> y</p>\n", 5) { ok = false }
    if !check(md.to_html("use `code` here") == "<p>use <code>code</code> here</p>\n", 6) { ok = false }

    // --- inline: link ---
    if !check(md.to_html("[t](http://x)") == "<p><a href=\"http://x\">t</a></p>\n", 7) { ok = false }

    // --- HTML escaping of text ---
    if !check(md.to_html("escape < & >") == "<p>escape &lt; &amp; &gt;</p>\n", 8) { ok = false }

    // --- unordered list ---
    if !check(md.to_html("- a\n- b") == "<ul>\n<li>a</li>\n<li>b</li>\n</ul>\n", 9) { ok = false }

    // --- ordered list ---
    if !check(md.to_html("1. one\n2. two") == "<ol>\n<li>one</li>\n<li>two</li>\n</ol>\n", 10) { ok = false }

    // --- fenced code block with language class (code is escaped) ---
    if !check(md.to_html("```py\na < b\n```") == "<pre><code class=\"language-py\">a &lt; b</code></pre>\n", 11) { ok = false }

    // --- horizontal rule ---
    if !check(md.to_html("---") == "<hr>\n", 12) { ok = false }

    // --- full document wrapper ---
    string full = md.to_html_full("# Hi", "My Title")
    if !check(full.starts_with("<!DOCTYPE html>\n<html>\n<head>\n"), 13) { ok = false }
    if !check(full.contains("<title>My Title</title>"), 14) { ok = false }
    if !check(full.contains("<h1>Hi</h1>"), 15) { ok = false }
    if !check(full.contains("</body>\n</html>\n"), 16) { ok = false }

    // --- title is HTML-escaped in the head ---
    string ft = md.to_html_full("x", "A < B")
    if !check(ft.contains("<title>A &lt; B</title>"), 17) { ok = false }

    if ok { print("HTML PASS") }
}
