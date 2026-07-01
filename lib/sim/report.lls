// sim.report — the unified analysis entry point: one call runs the whole pipeline
// (plan §3.1) over a text listing and produces a complete report:
//   instruction listing -> front-end model -> engine-1 bottleneck (+ port heatmap)
//   -> engine-2 cycle Gantt -> operand provenance -> advisor suggestions.
//
// This is the "tool" surface: decode + model + both engines + analysis + advisor,
// stitched together. Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import std.sys.io as io
import std.sys.proc as proc
import std.sys.env as env
import sim.core.ir as ir
import sim.core.engine as engine
import sim.core.engine2 as engine2
import sim.core.analysis as analysis
import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.intel.frontend as frontend
import sim.intel.patterns as patterns
import sim.render as render
import sim.htmlview as hv

def MODE_TEXT() -> int { return 0 }
def MODE_HTML() -> int { return 1 }

// ---- live llvm-mca cross-check (optional high-accuracy backend, plan §10.2 #2) -----
// We do NOT hardcode llvm-mca output. run_llvm_mca shells out to the tool ON THE KERNEL
// being reported and returns its fresh summary; if llvm-mca is absent or the kernel does
// not assemble, it returns Err and the caller omits the cross-check (the report still
// renders). The binary is $LLVM_MCA or the conventional install path.

def _mca_bin() -> Str {
    match env.get("LLVM_MCA") {
        Some(p) => { return p }
        None => {}
    }
    return "C:/llvm/bin/llvm-mca.exe"
}

// keep only the comparison-relevant lines: Iterations / Total Cycles / Block RThroughput
// and the "Resource pressure per iteration" table (header + data rows until a blank line).
def _mca_summary(&Str raw) -> Str {
    Vec(Str) lns = raw.lines()
    Str out = ""
    bool in_press = false
    int n = lns.len()
    for i in 0..n {
        &Str ln = lns.get_ref(i)
        bool keep = false
        if ln.contains?("Iterations:") { keep = true }
        if ln.contains?("Total Cycles:") { keep = true }
        if ln.contains?("Block RThroughput:") { keep = true }
        if ln.contains?("Resource pressure per iteration:") { keep = true; in_press = true }
        else {
            if in_press {
                Str t = ln.trim()
                if t.len() == 0 { in_press = false } else { keep = true }
            }
        }
        if keep { out = f"{out}{ln}\n" }
    }
    return out
}

// run llvm-mca live on `listing` (Intel syntax). Returns Ok(summary) or Err (tool missing
// / kernel won't assemble) — callers treat Err as "skip the cross-check".
def run_llvm_mca(&Str listing, Str cpu, int iters) -> Result(Str, Str) {
    Str src = f".intel_syntax noprefix\n{listing}"
    Str sp = "tmp/_mca_probe.s"
    Result(int, Str) wr = io.write_file(sp, src)
    match wr {
        Ok(nb) => {}
        Err(e) => { return Err(e) }
    }
    Str bin = _mca_bin()
    // On Windows popen routes through cmd.exe: a QUOTED forward-slash program path is
    // mangled by cmd's leading-quote rule, but a quoted BACKSLASH path works — so convert
    // '/'->'\' and quote it (handles a $LLVM_MCA path with spaces, e.g. "Program Files").
    // The file arg stays forward-slash (llvm-mca parses it, cmd never touches it).
    Str fwd = "/"
    Str back = "\\"
    Str binw = bin.replace(&fwd, &back)
    Str q = ""
    q.push_byte(34)                      // '"'
    Str cmd = f"{q}{binw}{q} -mcpu={cpu} -iterations={iters} {sp}"
    Result(Str, Str) r = proc.exec(cmd)
    match r {
        Ok(out) => {
            if out.contains?("Block RThroughput") { return Ok(_mca_summary(&out)) }
            return Err("llvm-mca produced no throughput line (assembly error?)")
        }
        Err(out) => { return Err("llvm-mca unavailable or non-zero exit") }
    }
}

def _isa_has(&Vec(Str) isa, Str feat) -> bool {
    for f in &isa {
        if f.eq?(&feat) { return true }
    }
    return false
}

// append all of `src`'s suggestions onto `dst` (cloning so dst owns them)
def _merge(&!Vec(patterns.Suggestion) dst, &Vec(patterns.Suggestion) src) {
    int n = src.len()
    for i in 0..n {
        &patterns.Suggestion c = src.get_ref(i)
        dst.push(patterns.Suggestion { id: c.id.copy(), title: c.title.copy(),
            rationale: c.rationale.copy(), suggested: c.suggested.copy(),
            expected_gain: c.expected_gain.copy(), is_antipattern: c.is_antipattern })
    }
}

// one call runs the whole pipeline; mode picks the renderer (text or html). HTML adds
// the instruction-level data-movement visualization (sim.render) after the analysis.
def full_report_mode(&Str listing, &uarch.Uarch u, &Vec(Str) isa, &Vec(Str) const_regs, int mode, Str mca_text) -> Str {
    Vec(ir.Inst) prog = decode.parse_listing(listing, 0x400 as i64)
    Vec(ir.Uop) uops = ports.build_uops(&prog)
    // loop-carried edges so the report's bottleneck reflects RecMII (recurrence layer),
    // not just the port bound — a single-accumulator reduction reads recurrence-bound with
    // the true II instead of an over-optimistic ResMII.
    Vec(ir.Carried) carried = ports.carried_edges(&prog)
    frontend.FrontendModel fm = frontend.analyze_frontend(&prog, u)
    int efe = frontend.effective_fe_width(&fm)
    engine.Bottleneck b = engine.analyze_rec(&uops, &carried, u.num_ports, efe)
    Vec(ir.Port) pset = uarch.port_set(u)
    Vec(ir.UopTrace) tr = engine2.simulate(&uops, u.num_ports)

    // advisor: bottleneck-gated rules + provenance-aware complex multiply + LICM
    int bk = patterns.bk_frontend()
    int bport = -1
    if b.port_id >= 0 { bk = patterns.bk_port(); bport = b.port_id }
    Vec(patterns.Suggestion) sg = patterns.advise(&prog, bk, bport, isa, u.avx512_downclock)
    bool has_fp16 = _isa_has(isa, "FP16")
    Vec(patterns.Suggestion) cm = patterns.advise_cmul(&prog, const_regs, has_fp16)
    _merge(&!sg, &cm)
    Vec(patterns.Suggestion) lc = patterns.advise_licm(&prog, const_regs)
    _merge(&!sg, &lc)

    if mode == MODE_HTML() {
        Str body = ""
        body = body + hv.h_banner("① simulation — bottleneck & cycle timeline")
        body = body + hv.h_listing(listing.copy())
        body = body + hv.h_para("microarchitecture", uarch.summary(u))
        body = body + hv.h_section("front-end model (MITE / DSB / LSD delivery)", frontend.report(&fm))
        body = body + hv.h_bottleneck(&b, &uops, &pset)
        if mca_text.len() > 0 {
            body = body + hv.h_section("llvm-mca cross-check (reference analyzer; differences = the sim's hand-seeded port-model gaps -> TableGen)", mca_text)
        }
        body = body + hv.h_gantt(&tr, &prog)
        body = body + hv.h_gantt_iters(&uops, &prog, u.num_ports)
        body = body + hv.h_banner("② analysis — provenance & advisor")
        body = body + hv.h_section("operand provenance (loop-invariant vs live-in vs produced)", analysis.report_provenance(&prog, const_regs))
        body = body + hv.h_section("advisor suggestions (bottleneck-gated)", patterns.render(&sg))
        body = body + hv.h_banner("③ instruction-level data movement")
        body = body + hv.h_legend()
        body = body + render.render_steps_rich(&prog, &uops, render.MODE_HTML())
        return hv.page("sim — full kernel report", body)
    }

    // text
    Str out = ir.dump_insts(&prog)
    out = f"{out}\n{uarch.summary(u)}\n\n"
    out = f"{out}{frontend.report(&fm)}\n"
    out = f"{out}{engine.report(&b, &uops, &pset)}\n"
    if mca_text.len() > 0 {
        out = f"{out}=== llvm-mca live cross-check ({u.name}) — differences = the sim's hand-seeded port-model gaps ===\n{mca_text}\n"
    }
    out = f"{out}{engine2.gantt(&tr, &prog)}\n"
    out = f"{out}{analysis.report_provenance(&prog, const_regs)}\n"
    out = f"{out}{patterns.render(&sg)}"
    return out
}

// back-compat text entry point.
def full_report(&Str listing, &uarch.Uarch u, &Vec(Str) isa, &Vec(Str) const_regs) -> Str {
    return full_report_mode(listing, u, isa, const_regs, MODE_TEXT(), "")
}

// full report with a LIVE llvm-mca cross-check: run llvm-mca on `listing` and embed its
// fresh output (Err -> skip the cross-check, report still renders). `cpu`/`iters` are the
// llvm-mca knobs (e.g. "icelake-server", 1000). Not hardcoded — the tool runs each call.
def full_report_mode_live(&Str listing, &uarch.Uarch u, &Vec(Str) isa, &Vec(Str) const_regs, int mode, Str cpu, int iters) -> Str {
    Str mca = ""
    match run_llvm_mca(listing, cpu, iters) {
        Ok(t) => { mca = t }
        Err(e) => { mca = "" }      // llvm-mca absent / kernel won't assemble -> no section
    }
    return full_report_mode(listing, u, isa, const_regs, mode, mca)
}

// run the whole pipeline directly from an assembly file on disk (`xx.s`) instead of an
// in-memory Str: read it, then hand the raw text to full_report_mode (parse_listing
// skips directives/labels/comments, so a clang `-S -masm=intel` listing works as-is;
// the HTML/text report's instruction listing shows the original file content). Returns
// Err(message) if the file cannot be read. AT&T syntax is not supported — use -masm=intel.
def full_report_file(Str path, &uarch.Uarch u, &Vec(Str) isa, &Vec(Str) const_regs, int mode, Str mca_text) -> Result(Str, Str) {
    Result(Str, Str) r = io.read_file(path)
    match r {
        Ok(text) => {
            Str rep = full_report_mode(&text, u, isa, const_regs, mode, mca_text)
            return Ok(rep)
        }
        Err(e) => { return Err(e) }
    }
}

// ============================================================================
// Bundled options — the model knobs in ONE struct, with a sensible default config.
// Most callers just want `default_opts()` (Ice Lake + AVX-512) and one of the analyze*
// entries below; the individual `full_report_*` functions above remain for fine control.
// ============================================================================
struct SimOpts {
    uarch.Uarch chip        // microarchitecture model — drives the cycle / port numbers
    Vec(Str) isa            // ISA feature flags ("AVX2"/"AVX512"/"VBMI"/"FP16") — gate the advisor
    Vec(Str) const_regs     // loop-invariant registers — a hint for provenance + LICM/cmul advice
    bool mca                // run a LIVE llvm-mca cross-check? (off by default)
    Str mca_cpu             // llvm-mca CPU name when mca == true (e.g. "icelake-server")
    int mca_iters           // llvm-mca iteration count
}

// The normal config: Ice Lake server, AVX2 + AVX-512, no loop-invariant hints, no llvm-mca.
// Tweak the fields afterwards for advanced use — e.g. `o.chip = uarch.skylake_x()`,
// `o.isa.push("VBMI")`, `o.const_regs.push("zmm0")`, or `o.mca = true` for the cross-check.
def default_opts() -> SimOpts {
    Vec(Str) isa = {}
    isa.push("AVX2")
    isa.push("AVX512")
    Vec(Str) cr = {}
    return SimOpts { chip: uarch.icelake(), isa: isa, const_regs: cr, mca: false, mca_cpu: "icelake-server", mca_iters: 1000 }
}

// Analyze an in-memory listing. `mode` = MODE_TEXT() / MODE_HTML(). When `o.mca` is set, a
// live llvm-mca cross-check is run and embedded (skipped cleanly if llvm-mca is not found).
def analyze(&Str asm, &SimOpts o, int mode) -> Str {
    if o.mca { return full_report_mode_live(asm, &o.chip, &o.isa, &o.const_regs, mode, o.mca_cpu, o.mca_iters) }
    return full_report_mode(asm, &o.chip, &o.isa, &o.const_regs, mode, "")
}

// Analyze a .s file on disk (honors `o.mca` just like analyze). Err on a missing / unreadable file.
def analyze_file(Str path, &SimOpts o, int mode) -> Result(Str, Str) {
    Result(Str, Str) r = io.read_file(path)
    match r {
        Ok(text) => { return Ok(analyze(&text, o, mode)) }
        Err(e) => { return Err(e) }
    }
}
