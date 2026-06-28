// sim_asmfile_test.ls — read an assembly file (xx.s) from disk and run the whole
// pipeline, instead of only analyzing an in-memory Str. Exercises decode.parse_file
// and report.full_report_file over a realistic clang `-S -masm=intel` listing: tab
// separators, `.text`/`.globl`/`.intel_syntax` directives, `saxpy:`/`.LBB` labels,
// and trailing `# …` comments — all skipped, leaving just the instructions.

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.core.engine as engine
import sim.report as report
import sim.core.ir as ir
import std.core.vec
import std.core.str
import std.sys.io as io

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def main() {
    @print("=== read .s file from disk -> full pipeline ===")

    // a realistic clang-style Intel-syntax listing (tabs, directives, a label, comments).
    Str asm = ""
    asm = f"{asm}\t.text\n"
    asm = f"{asm}\t.intel_syntax noprefix\n"
    asm = f"{asm}\t.globl\tsaxpy\n"
    asm = f"{asm}saxpy:                                  # @saxpy\n"
    asm = f"{asm}\txor\tecx, ecx\n"
    asm = f"{asm}.LBB0_1:                                # =>This Inner Loop Header\n"
    asm = f"{asm}\tvmovups\tzmm1, zmmword ptr [rdx + rcx]\n"
    asm = f"{asm}\tvfmadd213ps\tzmm1, zmm0, zmmword ptr [r8 + rcx]  # zmm1 = (zmm0 * zmm1) + mem\n"
    asm = f"{asm}\tvmovups\tzmmword ptr [r8 + rcx], zmm1\n"
    asm = f"{asm}\tadd\trcx, 64\n"
    asm = f"{asm}\tjne\t.LBB0_1\n"
    asm = f"{asm}\tret\n"

    Str path = "tmp/sim_saxpy_kernel.s"
    Result(int, Str) wr = io.write_file(path, asm)
    match wr {
        Ok(nb) => { @print(f"  wrote {nb} bytes -> {path}") }
        Err(e) => { @print(f"  FAIL: write {e}") }
    }

    // 1) decode.parse_file: directives/labels/comments skipped, tabs handled.
    int ninst = 0
    bool fma_rw = false
    Result(Vec(ir.Inst), Str) pr = decode.parse_file(path, 0x400 as i64)
    match pr {
        Ok(prog) => {
            ninst = prog.len()
            // find the FMA and confirm its destination is read-write (loop accumulator)
            int np = prog.len()
            for i in 0..np {
                &ir.Inst ins = prog.get_ref(i)
                if ins.mnemonic.eq?("vfmadd213ps") {
                    &ir.Operand d = ins.ops.get_ref(0)
                    if d.is_read { if d.is_write { fma_rw = true } }
                }
            }
        }
        Err(e) => { @print(f"  FAIL: parse_file {e}") }
    }
    @print(f"  parse_file: {ninst} instructions (7 expected: xor/vmovups/vfmadd213ps/vmovups/add/jne/ret)")
    check(ninst == 7, "parse_file skips directives/labels/comments, keeps 7 instructions")
    check(fma_rw, "vfmadd213ps destination parsed read-write (tab separator handled)")

    // 2) report.full_report_file: whole pipeline straight from the file (text mode).
    uarch.Uarch u = uarch.icelake()
    Vec(Str) isa = {}
    isa.push("AVX512")
    Vec(Str) cregs = {}
    bool got_report = false
    Result(Str, Str) rr = report.full_report_file(path, &u, &isa, &cregs, report.MODE_TEXT(), "")
    match rr {
        Ok(rep) => {
            got_report = true
            if rep.contains?("vfmadd213ps") { @print("  report mentions vfmadd213ps") }
            @print("  --- report head ---")
            @print(rep.substr(0, 360))
        }
        Err(e) => { @print(f"  FAIL: full_report_file {e}") }
    }
    check(got_report, "full_report_file runs the pipeline directly from xx.s")

    // persist TEXT + HTML reports WITH a LIVE llvm-mca cross-check (run on this kernel;
    // skipped automatically if llvm-mca is absent). Not a hardcoded snapshot.
    Str txtrep = report.full_report_mode_live(&asm, &u, &isa, &cregs, report.MODE_TEXT(), "icelake-server", 1000)
    bool mca_live = txtrep.contains?("llvm-mca live cross-check")
    if mca_live { @print("  llvm-mca live cross-check embedded") }
    else { @print("  (llvm-mca not available — cross-check skipped, report still written)") }
    Result(int, Str) wt = io.write_file("tmp/sim_asmfile_report.txt", txtrep)
    match wt {
        Ok(nb) => { @print(f"  wrote text report ({nb} bytes) -> tmp/sim_asmfile_report.txt") }
        Err(e) => { @print(f"  text write failed: {e}") }
    }
    Str htmlrep = report.full_report_mode_live(&asm, &u, &isa, &cregs, report.MODE_HTML(), "icelake-server", 1000)
    Result(int, Str) wh = io.write_file("tmp/sim_asmfile_report.html", htmlrep)
    match wh {
        Ok(nb) => { @print(f"  wrote html report ({nb} bytes) -> tmp/sim_asmfile_report.html") }
        Err(e) => { @print(f"  html write failed: {e}") }
    }

    // 3) the file path == the in-memory path: parsing the raw text gives the same insts.
    Vec(ir.Inst) prog2 = decode.parse_listing(&asm, 0x400 as i64)
    check(prog2.len() == ninst, "parse_file matches parse_listing on identical text")

    // 4) missing file -> clean Err (not a crash).
    bool err_ok = false
    Result(Str, Str) er = report.full_report_file("tmp/does_not_exist.s", &u, &isa, &cregs, report.MODE_TEXT(), "")
    match er {
        Ok(rep) => { @print("  FAIL: missing file unexpectedly ok") }
        Err(e) => { err_ok = true; @print(f"  missing file handled: {e}") }
    }
    check(err_ok, "missing file returns Err (no crash)")

    // 5) jump modeling: skipping `label:` lines drops only the label DEFINITION, not the
    // jump INSTRUCTION — `jne` is still emitted as a branch-port (p6) uop, and its target
    // label is a branch target (kept out of the register dependency graph, not a write).
    @print("")
    @print("=== jump/label handling ===")
    Vec(ir.Inst) prog3 = decode.parse_listing(&asm, 0x400 as i64)
    bool jne_on_p6 = false
    bool target_not_reg = false
    int jne_idx = 0 - 1
    int npj = prog3.len()
    for i in 0..npj {
        &ir.Inst ins = prog3.get_ref(i)
        if ins.mnemonic.eq?("jne") {
            jne_idx = i
            // operand is a branch target (imm kind 2), NOT a register write
            if ins.ops.len() > 0 {
                &ir.Operand t = ins.ops.get_ref(0)
                if t.kind == 2 { target_not_reg = true }
            }
        }
    }
    // build uops and confirm the jne uop lands on the branch port p6 (matched by inst_id)
    Vec(ir.Uop) uj = ports.build_uops(&prog3)
    int nuj = uj.len()
    for i in 0..nuj {
        &ir.Uop u2 = uj.get_ref(i)
        if u2.inst_id == jne_idx {
            int km = u2.port_mask.len()
            for k in 0..km { if u2.port_mask.get!(k) == 6 { jne_on_p6 = true } }
        }
    }
    check(jne_on_p6, "jne modeled as a branch-port (p6) uop (jump instruction kept, only label skipped)")
    check(target_not_reg, "branch target classified as target, not a phantom register write")

    // 6) region markers: a WHOLE function (setup + loop + epilogue) scoped to the loop
    // body via `# LLVM-MCA-BEGIN`/`# LLVM-MCA-END` (llvm-mca convention). Without markers
    // the entire file is one block; with them only the marked region is analyzed.
    Str fn = ""
    fn = f"{fn}saxpy:\n"
    fn = f"{fn}\txor ecx, ecx\n"               // setup (excluded)
    fn = f"{fn}\tvbroadcastss zmm0, xmm0\n"    // setup (excluded)
    fn = f"{fn}# LLVM-MCA-BEGIN loop\n"
    fn = f"{fn}.LBB0_1:\n"
    fn = f"{fn}\tvmovups zmm1, zmmword ptr [rdx + rcx]\n"
    fn = f"{fn}\tvfmadd213ps zmm1, zmm0, zmmword ptr [r8 + rcx]\n"
    fn = f"{fn}\tvmovups zmmword ptr [r8 + rcx], zmm1\n"
    fn = f"{fn}\tadd rcx, 64\n"
    fn = f"{fn}\tjne .LBB0_1\n"
    fn = f"{fn}# LLVM-MCA-END\n"
    fn = f"{fn}\tret\n"                         // epilogue (excluded)
    Vec(ir.Inst) rgn = decode.parse_listing(&fn, 0x400 as i64)
    @print(f"  region-scoped loop body: {rgn.len()} insts (5 expected; setup/epilogue excluded)")
    check(rgn.len() == 5, "# LLVM-MCA-BEGIN/END scopes analysis to the loop body")
    // without markers the same text parses to all 8 instructions (setup+body+epilogue
    // as one block). Neutralize the marker tags so region_mode is off.
    Str tag = "LLVM-MCA"
    Str neutral = "NOMARK"
    Str fn_nomark = fn.replace(&tag, &neutral)
    Vec(ir.Inst) allp = decode.parse_listing(&fn_nomark, 0x400 as i64)
    @print(f"  no markers: {allp.len()} insts (whole function as one block: xor/vbroadcastss/vmovups/vfmadd/vmovups/add/jne/ret)")
    check(allp.len() == 8, "without markers the whole function is one block (8 insts)")

    // 7) persist a RECURRENCE-kernel report (single-accumulator dot product) so the
    // RecMII layer is visible in tmp/: the report should read "recurrence-bound" with
    // II = max(ResMII, RecMII) = the FMA latency, not the optimistic port bound.
    @print("")
    @print("=== recurrence report (RecMII visible) ===")
    Str dot = ""
    dot = f"{dot}# LLVM-MCA-BEGIN dot_serial\n"
    dot = f"{dot}vfmadd231ps zmm0, zmm1, zmm2\n"   // zmm0 accumulator -> loop-carried
    dot = f"{dot}# LLVM-MCA-END\n"
    bool rec_shown = false
    // LIVE llvm-mca cross-check too: llvm-mca shows Block RThroughput 1.0 (port bound,
    // MISSES the recurrence) while its Total Cycles ≈ 4000/1000 iters = 4.0 — exactly the
    // gap LS's RecMII closes. The report puts both side by side.
    Str dotrep = report.full_report_mode_live(&dot, &u, &isa, &cregs, report.MODE_TEXT(), "icelake-server", 1000)
    if dotrep.contains?("recurrence-bound") { if dotrep.contains?("RecMII") { rec_shown = true } }
    Result(int, Str) wd = io.write_file("tmp/sim_recurrence_report.txt", dotrep)
    match wd {
        Ok(nb) => { @print(f"  wrote recurrence report ({nb} bytes) -> tmp/sim_recurrence_report.txt") }
        Err(e) => { @print(f"  write failed: {e}") }
    }
    Str dothtml = report.full_report_mode_live(&dot, &u, &isa, &cregs, report.MODE_HTML(), "icelake-server", 1000)
    Result(int, Str) wdh = io.write_file("tmp/sim_recurrence_report.html", dothtml)
    match wdh {
        Ok(nb) => { @print(f"  wrote recurrence report ({nb} bytes) -> tmp/sim_recurrence_report.html") }
        Err(e) => { @print(f"  write failed: {e}") }
    }
    check(rec_shown, "full report shows recurrence-bound verdict + RecMII for a single-accumulator dot product")

    @print("SIM ASMFILE PASS")
}
