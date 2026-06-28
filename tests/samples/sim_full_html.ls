// sim_full_html.ls — end-to-end HTML report over a COMPLETE clean-room BFP8 block
// compressor. One block = 32 int16 samples sharing one exponent; output = 1 exponent
// byte + 32 int8 mantissa bytes. The shared exponent is found by a hoist-scan (early-
// exit MSB test). ONE call runs the whole pipeline (simulation + analysis + the
// instruction-level data-movement visualization). Caller feeds only assembly + chip + mode.
//
// Clean-room: byte-aligned BFP8 written from the block-floating-point definition; the
// hoist-scan exponent search is a generic normalization technique. No third-party code.

import sim.report as report
import sim.intel.uarch as uarch
import std.sys.io as io
import std.core.vec
import std.core.str

def main() {
    // PURE per-block kernel. Live-in: zmm0 = 32 int16 samples (the block);
    // zmm16 = 0x8000 MSB mask — LOOP-INVARIANT, loaded ONCE in the loop preamble
    // (mov eax,0x8000 / vpbroadcastw zmm16,eax hoisted out; not re-done per block).
    Str asm = ""
    asm = f"{asm}vmovdqu64 zmm0, [rdi]\n"         // load 32 int16 samples of the block
    asm = f"{asm}vpabsw   zmm1, zmm0\n"           // |sample| magnitude of every word
    asm = f"{asm}xor      ecx, ecx\n"             // k = 0 (hoist-scan shift counter)
    // --- hoist-scan: find bitlen(block max) by early-exit MSB test ---
    asm = f"{asm}vptestmw k1, zmm1, zmm16\n"      // any word's bit15 set? (block-max MSB?)
    asm = f"{asm}kortestw k1, k1\n"
    asm = f"{asm}jnz      found\n"                // yes -> exit; bitlen = 16 - k
    asm = f"{asm}vpsllw   zmm1, zmm1, 1\n"        // no -> expose the next bit
    asm = f"{asm}inc      ecx\n"                  // k++
    asm = f"{asm}cmp      ecx, 9\n"
    asm = f"{asm}jl       scan\n"                 // loop while k < 9
    // --- found: shared exponent  exp = max(0, 9 - k) ---
    asm = f"{asm}mov      edx, 9\n"
    asm = f"{asm}sub      edx, ecx\n"             // edx = exp (shared by the whole block)
    // --- quantize: broadcast shared exp, arithmetic-shift every sample right ---
    asm = f"{asm}vmovd    xmm3, edx\n"            // exp -> xmm low (uniform shift count)
    asm = f"{asm}vpsraw   zmm4, zmm0, xmm3\n"     // mantissa = sample >> exp (uniform, all 32 words)
    // --- pack mantissas + store the exponent AND the mantissas ---
    asm = f"{asm}vpmovwb  ymm5, zmm4\n"           // 32 int16 -> 32 int8 signed mantissas
    asm = f"{asm}mov      [rdi], dl\n"            // store the 1-byte shared exponent
    asm = f"{asm}vmovdqu8 [rdi+1], ymm5\n"        // store the 32 mantissa bytes

    uarch.Uarch u = uarch.icelake()
    Vec(Str) isa = {}
    isa.push("AVX2")
    isa.push("AVX512")
    isa.push("VBMI")
    Vec(Str) cregs = {}
    cregs.push("zmm16")                 // the MSB mask is loop-invariant

    // LIVE llvm-mca cross-check: run llvm-mca on THIS kernel (not a hardcoded snapshot).
    // The BFP8 scan kernel references labels (found/scan), so make it assemblable for
    // llvm-mca by giving them definitions — LS decode skips `label:` lines, so the
    // analyzed instruction stream is identical with or without them. If llvm-mca is
    // absent OR the kernel still won't assemble, full_report_mode_live skips the section.
    Str asm_mca = ""
    asm_mca = f"{asm_mca}vmovdqu64 zmm0, [rdi]\n"
    asm_mca = f"{asm_mca}vpabsw   zmm1, zmm0\n"
    asm_mca = f"{asm_mca}xor      ecx, ecx\n"
    asm_mca = f"{asm_mca}scan:\n"
    asm_mca = f"{asm_mca}vptestmw k1, zmm1, zmm16\n"
    asm_mca = f"{asm_mca}kortestw k1, k1\n"
    asm_mca = f"{asm_mca}jnz      found\n"
    asm_mca = f"{asm_mca}vpsllw   zmm1, zmm1, 1\n"
    asm_mca = f"{asm_mca}inc      ecx\n"
    asm_mca = f"{asm_mca}cmp      ecx, 9\n"
    asm_mca = f"{asm_mca}jl       scan\n"
    asm_mca = f"{asm_mca}found:\n"
    asm_mca = f"{asm_mca}mov      edx, 9\n"
    asm_mca = f"{asm_mca}sub      edx, ecx\n"
    asm_mca = f"{asm_mca}vmovd    xmm3, edx\n"
    asm_mca = f"{asm_mca}vpsraw   zmm4, zmm0, xmm3\n"
    asm_mca = f"{asm_mca}vpmovwb  ymm5, zmm4\n"
    asm_mca = f"{asm_mca}mov      [rdi], dl\n"
    asm_mca = f"{asm_mca}vmovdqu8 [rdi+1], ymm5\n"
    Str mca = ""
    match report.run_llvm_mca(&asm_mca, "icelake-server", 1000) {
        Ok(t) => { mca = t; @print("llvm-mca live cross-check embedded") }
        Err(e) => { mca = ""; @print(f"(llvm-mca skipped: {e})") }
    }

    // HTML report (kept in tmp/ for inspection — ctest does not delete it).
    Str html = report.full_report_mode(&asm, &u, &isa, &cregs, report.MODE_HTML(), mca)
    Str path = "tmp/sim_full_report.html"
    Result(int, Str) wr = io.write_file(path, html)
    match wr {
        Ok(nb) => { @print(f"wrote {nb} bytes -> {path}") }
        Err(e) => { @print(f"write failed: {e}") }
    }

    // TEXT report of the SAME kernel (so both formats are available side by side).
    Str txt = report.full_report_mode(&asm, &u, &isa, &cregs, report.MODE_TEXT(), mca)
    Str tpath = "tmp/sim_full_report.txt"
    Result(int, Str) wt = io.write_file(tpath, txt)
    match wt {
        Ok(nb) => { @print(f"wrote {nb} bytes -> {tpath}") }
        Err(e) => { @print(f"write failed: {e}") }
    }
    @print("SIM FULL HTML DONE")
}
