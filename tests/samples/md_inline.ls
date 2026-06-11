// Phase C: inline parsing + extract helpers + round-trip.
import std.vec
import std.md as md
import std.str
import io

fn main() {
    string src = "# Title with `code`\n\nText with **bold**, _italic_, ***both***, `c`, a [link](http://x.com) and ![img](pic.png).\n\n- item with [l2](http://y.com)\n\n> quoted [l3](http://z.com)\n"

    md.MdDoc doc = md.parse(src)

    // round-trip render (structured inlines must reproduce the markup)
    print("---RENDER---")
    print(md.render(doc))

    print("---HEADINGS---")
    Vec(Str) hs = md.extract_headings(doc)
    int i = 0
    while i < hs.len { print(hs.get(i)); i = i + 1 }

    print("---LINKS---")
    Vec(Str) ls = md.extract_links(doc)
    i = 0
    while i < ls.len { print(ls.get(i)); i = i + 1 }

    print("---PLAIN---")
    print(md.to_plain_text(doc))
}
