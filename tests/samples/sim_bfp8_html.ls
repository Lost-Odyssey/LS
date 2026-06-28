// sim_bfp8_html.ls — the HTML-output option, driven the right way: the caller feeds
// ONLY the assembly + the render mode. sim.render.render_kernel does everything else
// (parse, engine analysis, per-instruction movement derivation, layout). No masks,
// ports, roles, or step labels are hand-written here. Writes tmp/sim_bfp8_viz.html.

import sim.render as render
import std.sys.io as io
import std.core.str

def main() {
    // PURE per-block kernel of a clean-room BFP8 compressor (32 int16/block share one
    // exponent; hoist-scan finds it; store exp + mantissas). Live-in: zmm0 = the block;
    // zmm16 = 0x8000 MSB mask, LOOP-INVARIANT (loaded once in the preamble, hoisted out).
    Str asm = ""
    asm = f"{asm}vpabsw   zmm1, zmm0\n"           // |sample| magnitude
    asm = f"{asm}xor      ecx, ecx\n"             // k = 0 (hoist-scan shift counter)
    asm = f"{asm}vptestmw k1, zmm1, zmm16\n"      // any word's bit15 set? (block-max MSB?)
    asm = f"{asm}kortestw k1, k1\n"
    asm = f"{asm}jnz      found\n"                // yes -> exit; bitlen = 16 - k
    asm = f"{asm}vpsllw   zmm1, zmm1, 1\n"        // no -> expose the next bit
    asm = f"{asm}inc      ecx\n"                  // k++
    asm = f"{asm}cmp      ecx, 9\n"
    asm = f"{asm}jl       scan\n"                 // loop while k < 9
    asm = f"{asm}mov      edx, 9\n"
    asm = f"{asm}sub      edx, ecx\n"             // exp = max(0, 9 - k) (shared by the block)
    asm = f"{asm}vmovd    xmm3, edx\n"            // exp -> xmm low (uniform shift count)
    asm = f"{asm}vpsraw   zmm4, zmm0, xmm3\n"     // mantissa = sample >> exp (uniform, all words)
    asm = f"{asm}vpmovwb  ymm5, zmm4\n"           // 32 int16 -> 32 int8 signed mantissas
    asm = f"{asm}mov      [rdi], dl\n"            // store the 1-byte shared exponent
    asm = f"{asm}vmovdqu8 [rdi+1], ymm5\n"        // store the 32 mantissa bytes

    // render? yes. mode? html. that's the entire decision surface.
    Str html = render.render_kernel(asm, render.MODE_HTML())

    Str path = "tmp/sim_bfp8_viz.html"
    Result(int, Str) wr = io.write_file(path, html)
    match wr {
        Ok(nbytes) => { @print(f"wrote {nbytes} bytes -> {path}") }
        Err(e) => { @print(f"write failed: {e}") }
    }
    @print("SIM BFP8 HTML DONE")
}
