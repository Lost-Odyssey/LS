// Phase B: parse Markdown into MdDoc, verify block count, round-trip.
// NOTE: VR-LIM-018 prevents Vec method calls on cross-module types;
// uses .len field + Str-based block inspection instead.

import std.vec
import std.md as md
import io
import std.str

fn check(bool cond, Str label) {
    if cond { print(f"ok {label}") } else { print(f"FAIL {label}") }
}

fn main() {
    Str src = "# Title\n\nA paragraph of text.\n\n## Section\n\n- one\n- two\n- three\n\n1. first\n2. second\n\n> a quote\n\n```ls\nfn main() {}\n```\n\n| Name | Score |\n| --- | --- |\n| Alice | 9.5 |\n| Bob | 8.1 |\n\n---\n"

    md.MdDoc doc = md.parse(src)
    check(doc.blocks.len == 8, "block count")

    Str rendered = md.render(doc)

    // Verify key elements are present in the rendered output
    check(rendered.contains?("# Title"), "has h1")
    check(rendered.contains?("## Section"), "has h2")
    check(rendered.contains?("- one"), "has ul")
    check(rendered.contains?("1. first"), "has ol")
    check(rendered.contains?("a quote"), "has blockquote")
    check(rendered.contains?("```"), "has code block")
    check(rendered.contains?("Alice"), "has table")
    check(rendered.contains?("---"), "has hr")
    check(rendered.contains?("Paragraph"), "has paragraph")

    // Round-trip test: re-parse the rendered output and verify same block count
    md.MdDoc doc2 = md.parse(rendered)
    check(doc2.blocks.len == 8, "round-trip")

    print("md_parse PASS")
}
