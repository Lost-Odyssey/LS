// sim.render — the single kernel-render entry point (the architecture the user asked
// for): feed ONLY the assembly + a render mode; the library does everything else.
//
//     render_kernel(assembly, mode)   ->   text  or  html
//
// The library parses the listing, runs engine-1 for the bottleneck, and for EACH
// instruction derives its picture FROM THE INSTRUCTION ITSELF using the built-in
// spec table (sim.intel.semantics) + a small set of control-derivation rules:
//   * immediate-controlled shuffle (vpshufd ...) -> decode the immediate into the
//     element gather, render the byte-movement grid;
//   * down-convert pack (vpmovwb/vpmovdb/...) -> derive the low-byte gather from the
//     src/dst element widths, render the cross-lane pack grid;
//   * compute / shift / store -> a role card straight from the spec class + ports.
// Nothing about a specific kernel is hand-written by the caller (no masks, no ports,
// no roles, no step labels). Mode picks the renderer (text vs html); same analysis.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import sim.intel.decode as decode
import sim.intel.ports as ports
import sim.intel.uarch as uarch
import sim.core.engine as engine
import sim.intel.semantics as sem
import sim.intel.isa_table as isa
import sim.intel.movement as mv
import sim.htmlview as hv
import sim.core.ir as ir
import sim.intel.specdoc as specdoc
import sim.gallery as gallery
import std.core.vec
import std.core.str
import std.core.map

def MODE_TEXT() -> int { return 0 }
def MODE_HTML() -> int { return 1 }

// a class-appropriate one-line description for a non-grid instruction — so scalar /
// control / mask / broadcast / compare / store ops are NOT mislabelled "element-wise".
// first_is_mem = the destination operand is memory (a store, not a load).
// ASCII only (avoid em-dash / middot: they mojibake under cp936; see viz-text note).
def _describe(int op, &Str mn, &Str asmline, bool first_is_mem) -> Str {
    int c0 = mn.byte_at(0)
    if mn.eq?("vmovd") { return "move scalar GPR -> vector register (sets the uniform shift count)" }
    if mn.eq?("vmovq") { return "move scalar GPR -> vector register" }
    if c0 == 106 { return "control flow: conditional branch (data-dependent here)" }  // 'j'
    if c0 == 107 { return "k-mask register op (reduces the per-lane predicate mask)" } // 'k'
    if op == 5  { return "broadcast: replicate one value across every lane (AVX-512)" }
    if op == 25 { return "compare -> k-mask: per-lane predicate, all lanes in parallel (AVX-512)" }
    if op == 31 { return "k-mask register operation" }
    if op == 11 { if first_is_mem { return "memory store" } return "vector store/load" }
    if op == 12 { if first_is_mem { return "memory store" } return "memory load" }
    if op == 1  { return "per-lane variable shift left (AVX-512, all lanes)" }
    if op == 2  { return "per-lane variable shift right (AVX-512, all lanes)" }
    if op == 3  { return "per-lane shift left, every lane in parallel (AVX-512)" }
    if op == 4  { return "per-lane shift right, every lane in parallel (AVX-512)" }
    if op == 26 { return "element convert" }
    if op == 99 {
        bool vec = false
        Str z = "zmm"
        if asmline.contains?(&z) { vec = true }
        Str y = "ymm"
        if asmline.contains?(&y) { vec = true }
        Str x = "xmm"
        if asmline.contains?(&x) { vec = true }
        if vec { return "vector op (unmodeled in the spec table)" }
        return "scalar register op (loop counter / exponent arithmetic)"
    }
    if op >= 20 { return "element-wise compute: per-lane, positions unchanged (AVX-512)" }
    return "data movement"
}

// port_mask [0,1,5] -> "p0/1/5"
def _port_str(&Vec(int) pm) -> Str {
    int n = pm.len()
    if n == 0 { return "-" }
    Str s = "p"
    for i in 0..n {
        int p = pm.get!(i)
        if i == 0 { s = f"{s}{p}" } else { s = f"{s}/{p}" }
    }
    return s
}

// the uop for instruction inst_id -> its port string (built-in port seed).
def _port_for(&Vec(ir.Uop) uops, int inst_id) -> Str {
    int n = uops.len()
    for k in 0..n {
        &ir.Uop uo = uops.get_ref(k)
        if uo.inst_id == inst_id { return _port_str(&uo.port_mask) }
    }
    return "-"
}

// compact list of the live gather indices (dst order), capped so the line stays short.
def _mask_compact(&Vec(int) mask) -> Str {
    Str s = ""
    int n = mask.len()
    int shown = 0
    int i = 0
    while i < n {
        int v = mask.get!(i)
        if v >= 0 {
            if shown >= 40 { s = f"{s},..."; i = n; }
            else {
                if shown > 0 { s = f"{s},{v}" } else { s = f"{v}" }
                shown = shown + 1
            }
        }
        i = i + 1
    }
    return s
}

// down-convert pack family: src/dst element bits, 0 if not a pack. (built-in encoding)
def _pack_src_ew(&Str mn) -> int {
    if mn.eq?("vpmovwb") { return 16 }
    if mn.eq?("vpmovdb") { return 32 }
    if mn.eq?("vpmovdw") { return 32 }
    if mn.eq?("vpmovqb") { return 64 }
    if mn.eq?("vpmovqw") { return 64 }
    if mn.eq?("vpmovqd") { return 64 }
    return 0
}
def _pack_dst_ew(&Str mn) -> int {
    if mn.eq?("vpmovwb") { return 8 }
    if mn.eq?("vpmovdb") { return 8 }
    if mn.eq?("vpmovdw") { return 16 }
    if mn.eq?("vpmovqb") { return 8 }
    if mn.eq?("vpmovqw") { return 16 }
    if mn.eq?("vpmovqd") { return 32 }
    return 0
}

// immediate-controlled element shuffle (vpshufd-style): each `ew`-bit element in a
// 128-bit lane is selected by a 2-bit field of `imm`. Returns a 64-byte lane-relative
// mask (movement's in-lane path resolves it per lane).
def _imm_shuffle_mask(int imm, int ew) -> Vec(int) {
    int lane_elems = 128 / ew
    int ebytes = ew / 8
    Vec(int) mask = {}
    for l in 0..4 {
        for e in 0..lane_elems {
            int field = (imm >> (e * 2)) & 3
            for kb in 0..ebytes { mask.push(field * ebytes + kb) }
        }
    }
    return mask
}

// down-convert pack: dst element i (dst_ew) <- low dst_ew bits of src element i
// (src_ew). In bytes over a 64-byte register: dst byte gets src byte; high dst dead.
def _pack_mask(int src_ew, int dst_ew, int out_elems) -> Vec(int) {
    int dbytes = dst_ew / 8
    int sbytes = src_ew / 8
    Vec(int) mask = {}
    for i in 0..64 {
        int de = i / dbytes
        int bo = i % dbytes
        if de < out_elems { mask.push(de * sbytes + bo) }
        else { mask.push(0 - 1) }
    }
    return mask
}

// ============================================================================
// render JUST the per-instruction data-movement section (no listing/bottleneck) —
// the library derives each instruction's picture from the instruction itself. Shared
// by render_kernel (standalone) and report.full_report (embedded). mode picks text/html.
// ============================================================================
// the canonical per-instruction renderer. HTML uses the rich, instruction-driven
// view (sim.gallery.render_one: summary + operand meanings + data type + byte/bit/mask/
// memory SRC->DST), shared by render_kernel AND report.full_report so ANY parsed asm —
// e.g. `vcompresspd [rdi] {k1}, zmm1` straight from a .s file — gets the full treatment
// end-to-end (no gallery-only path). Text mode keeps the compact legacy view.
def render_steps_rich(&Vec(ir.Inst) prog, &Vec(ir.Uop) uops, int mode) -> Str {
    if mode == MODE_HTML() {
        Vec(sem.SpecRow) tbl = isa.build_table()
        Vec(specdoc.SpecDoc) sd = specdoc.build_specdoc_table()
        Map(Str, int) maskvals = gallery._track_mask_consts(prog)   // resolve k1 from mov/kmov
        Str body = ""
        int n = prog.len()
        for i in 0..n {
            &ir.Inst ins = prog.get_ref(i)
            Str mn = ins.mnemonic.copy()
            sem.SpecRow r = sem.lookup(&tbl, &mn)
            body = body + gallery.render_one(ins, &r, &sd, &maskvals, "")
        }
        return body
    }
    return render_steps(prog, uops, mode)
}

def render_steps(&Vec(ir.Inst) prog, &Vec(ir.Uop) uops, int mode) -> Str {
    Vec(sem.SpecRow) tbl = isa.build_table()
    mv.ByteReg iota = mv.reg_iota()
    bool html = mode == MODE_HTML()
    Str body = ""

    int n = prog.len()
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        Str mn = ins.mnemonic.copy()
        sem.SpecRow r = sem.lookup(&tbl, &mn)
        Str port = _port_for(uops, i)
        Str idx = f"[{i + 1}]"
        Str cls = sem.class_name(r.op)

        // rebuild a compact "mnemonic op0, op1, ..." from the parsed operands.
        Str asmline = mn.copy()
        int no = ins.ops.len()
        for k in 0..no {
            &ir.Operand o = ins.ops.get_ref(k)
            Str t = o.text.copy()
            if k == 0 { asmline = f"{asmline} {t}" } else { asmline = f"{asmline}, {t}" }
        }
        // find an immediate operand (kind 2) -> int, -1 if none.
        int imm = 0 - 1
        for k in 0..no {
            &ir.Operand o2 = ins.ops.get_ref(k)
            if o2.kind == 2 {
                Str it = o2.text.copy()
                imm = it.to_int().unwrap_or(0 - 1)
            }
        }

        int psrc = _pack_src_ew(&mn)
        bool is_imm_perm = false
        if r.op == sem.OP_PERMUTE() { if imm >= 0 { is_imm_perm = true } }
        bool is_pack = psrc > 0

        if is_imm_perm {
            Vec(int) mask = _imm_shuffle_mask(imm, r.elem_bits)
            Str title = f"{idx} {asmline} — {cls} (immediate-controlled, {port})"
            Str mvals = _mask_compact(&mask)
            Str mdesc = f"mask = the immediate {imm} (a per-lane {r.elem_bits}-bit element selector); gather index per dst byte (lane-relative): [{mvals}]"
            if html { body = body + hv.h_permute(title, &mn, &iota, &mask, mdesc) }
            else {
                mv.StepResult sr = mv.step_permute(f"  {title}", &mn, &iota, &mask)
                body = f"{body}{sr.view}\n"
            }
        } else {
            if is_pack {
                int pdst = _pack_dst_ew(&mn)
                int oute = 512 / psrc
                Vec(int) mask = _pack_mask(psrc, pdst, oute)
                Str pmn = "vpermb"
                Str title = f"{idx} {asmline} — down-convert pack {psrc}->{pdst} bit ({port})"
                Str mvals = _mask_compact(&mask)
                Str mdesc = f"mask = none (vpmovwb is a fixed pattern: keep the low {pdst} bits of each {psrc}-bit element); gather: dst byte i <- src byte [{mvals}]"
                if html { body = body + hv.h_permute(title, &pmn, &iota, &mask, mdesc) }
                else {
                    mv.StepResult sr = mv.step_permute(f"  {title}", &pmn, &iota, &mask)
                    body = f"{body}{sr.view}\n"
                }
            } else {
                // a WORD-WIDE vector test/shift (the hoist-scan mechanics) -> show all
                // N lanes in parallel, so it never "looks scalar".
                bool is_ts = false
                if r.op == sem.OP_CMP() { is_ts = true }
                if r.op >= 1 { if r.op <= 4 { is_ts = true } }   // shift classes 1..4
                Str zz = "zmm"
                bool has_vec = asmline.contains?(&zz)
                if is_ts && has_vec {
                    int ew = r.elem_bits
                    if ew < 8 { ew = 16 }
                    int n = 512 / ew
                    bool msb = r.op == sem.OP_CMP()
                    int topbit = ew - 1
                    Str note = ""
                    if msb {
                        note = f"{mn} (word granularity): AND every {ew}-bit word with the MSB mask 0x8000 and test bit{topbit} -> a {n}-LANE k-mask (one bit per word, not 32-bit data), all {n} words in ONE instruction (the hoist-scan MSB test)"
                    } else {
                        // direction + kind + count-source derived from the mnemonic/operands
                        Str sll = "sll"
                        Str sra = "sra"
                        bool left = mn.contains?(&sll)
                        bool arith = mn.contains?(&sra)
                        Str dir = "left"
                        if !left { if arith { dir = "right (arithmetic, sign-extending)" } else { dir = "right (logical)" } }
                        Str cnt = "by the count in the xmm/vector operand (here = the shared exponent)"
                        if imm >= 0 { cnt = f"by the immediate {imm}" }
                        note = f"{mn}: shift all {n} x {ew}-bit words {dir} {cnt}, in parallel — ONE instruction"
                        if left { if imm == 1 { note = f"{note} (hoist-scan: expose the next bit each iteration)" } }
                    }
                    Str title = f"{idx} {asmline}  ({port})"
                    if html { body = body + hv.h_words(title, n, msb, note) }
                    else {
                        body = f"{body}    {idx} {asmline} ({port}) — {n} words in parallel: {note}\n"
                    }
                } else {
                    bool first_mem = false
                    if no > 0 {
                        &ir.Operand o0 = ins.ops.get_ref(0)
                        if o0.kind == 1 { first_mem = true }    // dst is memory -> store
                    }
                    Str role = _describe(r.op, &mn, &asmline, first_mem)
                    if html { body = body + hv.h_compute(idx, asmline, port, role) }
                    else {
                        Str ap = asmline.pad_right(28, 32)
                        Str pp = port.pad_right(8, 32)
                        body = f"{body}    {idx} {ap} {pp} {role}\n"
                    }
                }
            }
        }
    }

    return body
}

// ============================================================================
// standalone entry point: assembly + mode in, full page out. Caller writes nothing.
// ============================================================================
def render_kernel(Str asm, int mode) -> Str {
    Vec(ir.Inst) prog = decode.parse_listing(&asm, 0x400 as i64)
    Vec(ir.Uop) uops = ports.build_uops(&prog)
    uarch.Uarch u = uarch.icelake()
    engine.Bottleneck b = engine.analyze(&uops, u.num_ports, u.fe_width)
    Vec(ir.Port) pset = uarch.port_set(&u)

    if mode == MODE_HTML() {
        Str body = ""
        body = body + hv.h_listing(asm)
        body = body + hv.h_legend()
        body = body + hv.h_bottleneck(&b, &uops, &pset)
        body = body + render_steps_rich(&prog, &uops, mode)
        return hv.page("kernel instruction analysis (auto-rendered from assembly)", body)
    }
    Str out = f"{ir.dump_insts(&prog)}\n{engine.report(&b, &uops, &pset)}\n  per-instruction movement:\n"
    return f"{out}{render_steps_rich(&prog, &uops, mode)}"
}

// render a C-intrinsic kernel: lower intrinsics -> assembly, then the same pipeline as
// render_kernel. The listing shown is the resolved assembly (the intrinsic->mnemonic
// mapping made explicit). Feed ONLY the intrinsic source + a mode.
def render_intrinsics(Str src, int mode) -> Str {
    Str asm = decode.lower_intrinsics(&src)
    return render_kernel(asm, mode)
}
