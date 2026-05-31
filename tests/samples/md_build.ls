// Phase A end-to-end: build a document with every builder, render, print output.
import std.md as md
import io

fn main() {
    md.MdDoc doc = md.document()

    md.h1(&!doc, "Report")
    md.h2(&!doc, "Intro")
    md.paragraph(&!doc, "Plain text with **bold** kept verbatim.")
    md.code_block(&!doc, "ls", "fn main() { print(42) }")

    vec(string) items = ["First", "Second", "Third"]
    md.ul(&!doc, items)

    vec(string) steps = ["Step one", "Step two"]
    md.ol(&!doc, steps)

    md.blockquote(&!doc, "A quoted line.")

    vec(string) headers = ["Name", "Score"]
    vec(vec(string)) rows = []
    vec(string) row0 = ["Alice", "9.5"]
    rows.push(row0)
    vec(string) row1 = ["Bob", "8.1"]
    rows.push(row1)
    md.table(&!doc, headers, rows)

    md.hr(&!doc)

    string out = md.render(doc)
    print(out)

    print("---FRAGMENTS---")
    print(md.fmt_heading(3, "Title"))
    print(md.fmt_bold("important"))
    print(md.fmt_italic("emphasis"))
    print(md.fmt_code("x + 1"))
    print(md.fmt_link("text", "url"))
    print(md.fmt_image("alt", "img.png"))
}
