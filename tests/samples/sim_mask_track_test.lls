// sim_mask_track_test.ls — PURE end-to-end: a real asm sequence where the write-mask is
// set by a constant (mov + kmov) must be tracked and rendered with its ACTUAL value.
//
// This goes through the standard render path (render_kernel) — no gallery synthesis, no
// special routing. decode parses the memory operand and the {k1} decoration; the renderer
// statically tracks `mov al, 51` -> `kmovd k1, eax` and resolves k1 = 51, then renders the
// compress-store with the real active elements (bits 0,1,4,5 of 51).

import sim.render as render
import std.core.str
import std.core.vec

def check(bool cond, Str name) -> bool {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
    return cond
}

def main() {
    bool all = true
    // a real compiler-emitted sequence (Intel syntax)
    Str asm = ""
    asm = f"{asm}vpmovsxdq zmm0, ymm0\n"
    asm = f"{asm}mov al, 51\n"
    asm = f"{asm}kmovd k1, eax\n"
    asm = asm + "vcompresspd zmmword ptr [rsp + 16] {k1}, zmm0\n"
    asm = f"{asm}mov rsi, qword ptr [rsp + 72]\n"

    Str html = render.render_kernel(asm, render.MODE_HTML())
    all = check(html.len() > 2000, "render produced HTML") && all

    // the mask constant 51 is tracked from mov+kmov (NOT a representative pattern)
    Str t1 = "k1 = 51 (tracked from kmov"
    all = check(html.contains?(&t1), "k1 = 51 statically tracked from mov/kmov") && all
    Str t2 = "representative"
    all = check(!html.contains?(&t2), "no representative fallback (real value used)") && all

    // memory-dst compress-store rendered vertically with the active elements
    Str t3 = "Compress-store"
    all = check(html.contains?(&t3), "memory-dst compress-store modeled") && all
    Str t4 = "[rsp + 16 + 24]"
    all = check(html.contains?(&t4), "vertical memory rows for the 4 active elements") && all

    if all { @print("SIM MASK TRACK PASS") }
    else { @print("SIM MASK TRACK FAIL") }
}
