// Phase B: parse Markdown into MdDoc, inspect block structure, round-trip.
import std.md as md
import io

fn main() {
    string src = "# Title\n\nA paragraph of text.\n\n## Section\n\n- one\n- two\n- three\n\n1. first\n2. second\n\n> a quote\n\n```ls\nfn main() {}\n```\n\n| Name | Score |\n| --- | --- |\n| Alice | 9.5 |\n| Bob | 8.1 |\n\n---\n"

    md.MdDoc doc = md.parse(src)
    print(f"blocks: {doc.blocks.length}")

    int i = 0
    while i < doc.blocks.length {
        md.MdBlock b = doc.blocks.get(i)
        match b {
            Heading(lvl, c)        => { print(f"Heading {lvl}") }
            Paragraph(c)           => { print("Paragraph") }
            CodeBlock(lang, code)  => { print(f"CodeBlock {lang}") }
            UnorderedList(items)   => { print(f"UnorderedList {items.length}") }
            OrderedList(items)     => { print(f"OrderedList {items.length}") }
            Blockquote(ch)         => { print(f"Blockquote {ch.length}") }
            Table(h, cells)        => { print(f"Table cols={h.length} cells={cells.length}") }
            HorizontalRule         => { print("HorizontalRule") }
        }
        i = i + 1
    }

    print("---ROUNDTRIP---")
    print(md.render(doc))
}
