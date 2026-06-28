// sim_movement_gallery_test.ls — full-ISA end-to-end visual gallery.
//
// The driver feeds NOTHING instruction-specific: sim.gallery enumerates EVERY modelled
// instruction, shows a functional summary + the meaning of each operand, and for
// control-shuffle instructions binds a concrete mask, displays the mask's values, and
// renders SRC (before) / DST (after). Immediates are auto-decoded (vpshufd 0x1B -> dword
// selectors). Compute/crypto show summary + operands (no pseudocode).
//
// HTML -> tmp/sim_instruction_gallery.html (KEPT; ctest only removes AOT exes).

import sim.gallery as gallery
import std.sys.io as io
import std.core.str
import std.core.vec

def check(bool cond, Str name) -> bool {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
    return cond
}

def main() {
    bool all = true

    Str html = gallery.build_gallery()
    all = check(html.len() > 40000, "gallery HTML is large (whole ISA, >40KB)") && all

    // functional summary + operand meanings for every instruction
    Str s1 = "<b>summary:</b>"
    all = check(html.contains?(&s1), "functional summary shown") && all
    Str s2 = "<b>operands:</b>"
    all = check(html.contains?(&s2), "operand meanings shown") && all

    // NO pseudocode (it was unreadable)
    Str np = "Operation (Intel SDM pseudocode)"
    all = check(!html.contains?(&np), "pseudocode removed") && all

    // concrete mask values displayed (bound to the control register)
    Str m1 = "MASK (example:"
    all = check(html.contains?(&m1), "concrete bound mask values displayed") && all
    Str m2 = "MASK (decoded from immediate)"
    all = check(html.contains?(&m2), "immediate-decoded mask values displayed") && all
    Str d1 = "dword selectors"
    all = check(html.contains?(&d1), "vpshufd immediate auto-decoded") && all

    // k-register predicate masks render as a flat N-bit number with data granularity
    Str k1 = "each bit selects one"
    all = check(html.contains?(&k1), "k-mask shows data granularity") && all
    Str k2 = "64-bit element (qword)"
    all = check(html.contains?(&k2), "vcompresspd k-mask granularity = 64-bit qword") && all
    Str k3 = "class='kb on'"
    all = check(html.contains?(&k3), "k-mask rendered as flat bits (not lane grid)") && all

    // data type always carries the precise bit size
    Str dt = "data type:"
    all = check(html.contains?(&dt), "data type line present") && all
    Str dt2 = "64-bit float (double precision)"
    all = check(html.contains?(&dt2), "vcompresspd data type = 64-bit float") && all

    // memory-form compress/expand: vertical memory view, one element per row
    Str mv1 = "Compress-store"
    all = check(html.contains?(&mv1), "memory-dst compress form modeled") && all
    Str mv2 = "class='memrow'"
    all = check(html.contains?(&mv2), "vertical memory rows emitted") && all
    Str mv3 = "[rdi + 8]"
    all = check(html.contains?(&mv3), "memory rows show real base + ascending addr") && all

    // SRC before / DST after
    Str g1 = "SRC (before)"
    all = check(html.contains?(&g1), "SRC (before) view") && all
    Str g2 = "DST (after)"
    all = check(html.contains?(&g2), "DST (after) view") && all

    // masks are not all "reverse all 64 bytes"
    Str rev = "reverse all 64 bytes"
    all = check(!html.contains?(&rev), "masks are realistic (not reverse-all)") && all

    // GFNI present with a readable summary (not pseudocode)
    Str gf = "Galois Field"
    all = check(html.contains?(&gf), "GFNI shown with functional summary") && all

    Str path = "tmp/sim_instruction_gallery.html"
    Result(int, Str) wr = io.write_file(path, html)
    match wr {
        Ok(nbytes) => { @print(f"wrote {nbytes} bytes -> {path}") }
        Err(e) => { @print(f"write failed: {e}"); all = false }
    }

    // plain-text (monospace, regview) gallery — same shared data, text presentation
    Str txt = gallery.build_gallery_text()
    all = check(txt.len() > 20000, "text gallery is large (whole ISA)") && all
    Str tx1 = "FULL-ISA INSTRUCTION GALLERY (plain text)"
    all = check(txt.contains?(&tx1), "text gallery banner present") && all
    Str tx2 = "summary:"
    all = check(txt.contains?(&tx2), "text gallery shows summaries") && all
    Str tx3 = "DST (dst <- source"
    all = check(txt.contains?(&tx3), "text gallery shows element provenance") && all
    Str tx4 = "changed"
    all = check(txt.contains?(&tx4), "text gallery shows shift bit changes") && all
    Str tpath = "tmp/sim_instruction_gallery.txt"
    Result(int, Str) twr = io.write_file(tpath, txt)
    match twr {
        Ok(nb) => { @print(f"wrote {nb} bytes -> {tpath}") }
        Err(e) => { @print(f"text write failed: {e}"); all = false }
    }

    if all { @print("SIM MOVEMENT GALLERY DONE") }
    else { @print("SIM MOVEMENT GALLERY FAIL") }
}
