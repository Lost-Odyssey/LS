// sim.intel.patterns — x86/AVX-512 optimization advisor rule library.
//
// This is the project's biggest differentiator (plan §6): llvm-mca/uiCA only
// MEASURE; this advisor RECOMMENDS. The rule knowledge here is 100% original
// (zero license burden) — distilled from general SIMD optimization practice
// and plan §6.4 (idiom catalog) / §6.5
// (anti-pattern catalog).
//
// Design: rules are DATA (enumerable, filterable). A rule fires when
//   gate matches the engine's bottleneck verdict  (plan §6.0: advice only where
//                                                   it lands on the bottleneck)
//   AND its trigger mnemonics appear in the kernel (empty trigger = no gate)
//   AND the target chip has the required ISA features.
// No closures-in-structs — detection is a pure data predicate, so the module is
// memcheck-clean and trivially extensible.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.core.ir as ir
import sim.core.analysis as an

// ---- bottleneck kind (caller maps engine.Bottleneck -> these) ----------------
def bk_frontend() -> int { return 0 }
def bk_port()     -> int { return 1 }
def bk_dependency() -> int { return 2 }
def bk_other()    -> int { return 3 }

// ---- rule gate: when is a rule relevant? (ties into §6.1 bottleneck diagnosis) ----
def gate_any()      -> int { return 0 }   // always relevant (under trigger/isa)
def gate_port()     -> int { return 1 }   // any port-bound
def gate_port5()    -> int { return 2 }   // specifically the shuffle port p5
def gate_dep()      -> int { return 3 }   // dependency-bound (critical path)
def gate_frontend() -> int { return 4 }   // frontend-bound

// ============================================================================
// SimdRule — one advisor rule (pure data)
// ============================================================================
struct SimdRule {
    Str id
    Str title
    Str rationale        // why it's faster / why it's a problem (ports/latency/uops)
    Str suggested        // recommended instruction sequence / fix
    Str expected_gain    // estimate (x / saved insts / freed port)
    Vec(Str) requires_isa  // all must be in the target feature set; empty = none
    Vec(Str) trigger     // mnemonics that make the rule relevant; empty = always
    int  gate            // gate_* — only fires when bottleneck matches
    bool is_antipattern  // true = penalty/anti-pattern warning; false = idiom
    int  min_downclock   // μarch gate: fire only if uarch downclock >= this (-1 = ignore)
    bool needs_zmm       // μarch gate: fire only if the kernel actually uses 512b (zmm)
    int  min_triggers    // fire only if >= this many trigger mnemonics appear (fusion rules)
}

def rule(Str id, Str title, Str why, Str fix, Str gain,
         Vec(Str) isa, Vec(Str) trig, int gate, bool anti) -> SimdRule {
    return SimdRule { id: id, title: title, rationale: why, suggested: fix,
                      expected_gain: gain, requires_isa: isa, trigger: trig,
                      gate: gate, is_antipattern: anti, min_downclock: -1,
                      needs_zmm: false, min_triggers: 1 }
}

// fusion variant: fire only when at least `k` distinct trigger mnemonics appear
// (e.g. >=2 logic ops -> vpternlogd; mul + add present -> vfmadd).
def with_min_triggers(SimdRule ru, int k) -> SimdRule {
    ru.min_triggers = k
    return ru
}

// μarch-conditional variant: fire only when the target's AVX-512 downclock is at
// least `dc` AND the kernel uses zmm registers (§6.5 — μarch-conditional warning).
def with_downclock(SimdRule ru, int dc) -> SimdRule {
    ru.min_downclock = dc
    ru.needs_zmm = true
    return ru
}

// small Vec(Str) builders (avoid the array-literal->Vec coercion edge cases)
def feats0() -> Vec(Str) { Vec(Str) v = {}; return v }
def feats1(Str a) -> Vec(Str) { Vec(Str) v = {}; v.push(a); return v }
def feats2(Str a, Str b) -> Vec(Str) { Vec(Str) v = {}; v.push(a); v.push(b); return v }
def feats3(Str a, Str b, Str c) -> Vec(Str) { Vec(Str) v = {}; v.push(a); v.push(b); v.push(c); return v }
def feats4(Str a, Str b, Str c, Str d) -> Vec(Str) {
    Vec(Str) v = {}; v.push(a); v.push(b); v.push(c); v.push(d); return v
}

// ============================================================================
// the rule registry — the knowledge asset
// ============================================================================
def all_rules() -> Vec(SimdRule) {
    Vec(SimdRule) r = {}

    // --- idioms (replacement recommendations) ---------------------------------

    // Generic bit-field pack/unpack idioms. The technique (staggered variable shifts
    // to bit-align sub-byte fields; vpmultishiftqb to extract unaligned fields) is
    // public (e.g. Lemire/FastPFor) and applies to any bit-packing format.
    r.push(rule("bitpack-stagger",
        "Pack sub-byte fields with parallel variable shifts, not scalar BMI2 pext",
        "BMI2 pext serializes on a single GPR and is slow on AMD Zen1/2; a SIMD stagger (per-element vpsllvw/vpsllvd to bit-align + byte gather) packs many fields per vector op with no BMI2.",
        "vpsllvw/vpsllvd bit-align -> vpshufb byte streams -> vpor -> vpermb gather -> masked store",
        "removes the serial pext dependency; scales with vector width, zero BMI2",
        feats1("AVX512"), feats1("pext"), gate_any(), false))

    r.push(rule("bitfield-unpack-multishift",
        "Extract non-aligned bit fields with vpmultishiftqb",
        "A scalar bit-unpack loop (shift+mask+accumulate) serializes per field; vpmultishiftqb selects any 8 contiguous bits from each qword in one instruction -- purpose-built for unaligned bit-field extraction.",
        "vpmultishiftqb with a per-byte bit-offset control vector",
        "one instruction replaces the whole bit-unpack loop",
        feats1("VBMI"), feats2("shr", "shl"), gate_any(), false))

    // --- anti-patterns / penalty warnings (§6.5) ------------------------------

    // The headline: shuffle port saturated. Fires whenever the engine says p5-bound.
    r.push(rule("shuffle-port-saturated",
        "Shuffle port p5 is saturated",
        "Most Intel shuffles/permutes issue only on p5; stacking them makes p5 the throughput limiter (ResMII) while p0/p1 sit idle.",
        "rebalance to p0/p1 where an in-lane equivalent exists (vpshufb stays p5, but vpand/vpor/vpsllvw run on p0/p1); cut avoidable cross-lane permutes; or fold adjacent permutes into one",
        "each p5 op removed lowers ResMII by ~1/(#p5-capable ports)",
        feats0(), feats0(), gate_port5(), true))

    // Cross-lane permute cost (§6.5).
    r.push(rule("cross-lane-permute-cost",
        "Cross-lane permute is ~3c and p5-bound",
        "vpermd/vpermq/vpermb/vpermw cross 128-bit lanes: ~3-cycle latency and p5 pressure. In-lane operations (vpshufb, vpunpck*) are 1c and cheaper.",
        "prefer in-lane vpshufb / vpunpcklbw / vpalignr when the data movement stays within a lane; reserve cross-lane permutes for genuine lane crossings",
        "1c vs ~3c per op + relieves p5",
        feats1("AVX2"), feats4("vpermd", "vpermq", "vpermb", "vpermw"), gate_any(), true))

    // Horizontal reduction via vhaddps (§6.5).
    r.push(rule("hadd-reduction",
        "Horizontal reduction with vhaddps/vphaddd is multi-uop",
        "vhaddps and vphaddd decode to several uops and pile onto p5; in a hot loop they are a classic throughput sink.",
        "use a shuffle+add reduction tree (vshufps/vpermilps + vaddps) or _mm512_reduce_add_* intrinsics",
        "fewer uops, less p5 pressure",
        feats0(), feats2("vhaddps", "vphaddd"), gate_any(), true))

    // gather overuse (§6.5).
    r.push(rule("gather-overuse",
        "Gather is multi-uop -- avoid when access is stride-1",
        "vpgatherdd/vpgatherqd are slow multi-uop ops; if the access pattern is actually contiguous, a plain load + shuffle is far cheaper.",
        "replace stride-1 gathers with vmovdqu + vpshufb/vpermb reshaping",
        "a contiguous load + shuffle replaces a slow gather",
        feats1("AVX2"), feats2("vpgatherdd", "vpgatherqd"), gate_any(), true))

    // byte/word compress-to-memory is microcoded (uops.info: vpcompressb [m]{k},zmm on
    // Ice Lake .. Emerald Rapids decodes to ~8 uops through the microcode sequencer,
    // complex-decoder-only, ~6c throughput -- far worse than the port count implies; not
    // de-microcoded on any Intel uarch to date, SPR/EMR included).
    r.push(rule("compress-store-microcoded",
        "Byte/word compress-to-memory is microcoded on Intel",
        "vpcompressb/vpcompressw with a memory destination go through the microcode sequencer (~8 uops, complex-decoder-only, ~6c) on every Intel uarch from Ice Lake through Emerald Rapids; the measured throughput is several times the naive port-count estimate. (The dword/qword compress vpcompressd/q is cheaper and not flagged.)",
        "if the mask is a FIXED/known pattern (e.g. packing fixed-width fields like BFP9), gather with vpermb + a masked store instead of compress-store; keep vpcompressb only when the mask is genuinely data-dependent (runtime sparse filtering)",
        "vpermb + masked store is ~1.5 p5 vs the microcoded compress-store's ~6c",
        feats1("AVX512"), feats2("vpcompressb", "vpcompressw"), gate_any(), true))

    // single-accumulator reduction (§6.5 / §6.4 D). Dependency-gated.
    r.push(rule("single-accumulator",
        "Reduction critical path is one accumulator deep",
        "A single accumulator chains every FMA back-to-back, so the loop runs at N x FMA-latency instead of FMA-throughput -- the dependency, not the ports, is the limit.",
        "unroll with >=8 independent accumulators to hide the ~4c FMA latency, then combine once after the loop",
        "throughput-bound instead of latency-bound (~FMA_lat / FMA_tput speedup)",
        feats0(), feats0(), gate_dep(), true))

    // μarch-conditional: AVX-512 license-based downclock (§6.5). Only fires on a
    // target with heavy downclock (Skylake-X/Cascade Lake) AND a 512-bit kernel.
    // dc value 2 = heavy (matches uarch.dc_heavy()).
    r.push(with_downclock(rule("avx512-downclock",
        "Heavy 512-bit code lowers core frequency on this microarchitecture",
        "On Skylake-X / Cascade Lake, sustained 512-bit vector work drops the core clock (license-based downclocking), which can erase the per-instruction win. Ice Lake is mild (~175MHz); Rocket Lake+ effectively removed it.",
        "on affected chips try the 256-bit (ymm) form and measure; on Rocket Lake+/Sapphire Rapids keep 512-bit -- there is no downclock penalty",
        "avoids a frequency cliff that uop/port counts alone cannot see",
        feats1("AVX512"), feats0(), gate_any(), true), 2))

    // === §6.4 catalog — fusion & presence idioms (cleanly detectable) ==========

    // B: fuse a 2-3 op bitwise logic chain into one vpternlogd (256-entry LUT imm8).
    r.push(with_min_triggers(rule("ternlog-fuse",
        "Fuse a bitwise logic chain into vpternlogd",
        "Any 3-input combination of AND/OR/XOR/ANDN/NOT collapses to a single vpternlogd with an 8-bit truth table; the chain of 2-3 logic ops becomes one p0/p5 op.",
        "vpternlogd zmm, zmm, zmm, imm8  (pick the imm8 truth table for your boolean)",
        "1 op replaces 2-3 logic ops",
        feats1("AVX512"), feats4("vpand", "vpor", "vpxor", "vpandn"), gate_any(), false), 2))

    // D: fuse separate multiply + add (single precision) into an FMA.
    r.push(with_min_triggers(rule("fma-fuse-ps",
        "Fuse vmulps + vaddps into vfmadd231ps",
        "A separate multiply then add is two ops and two roundings; vfmadd231ps does mul-add in one op (2/cycle) with a single rounding.",
        "vfmadd231ps zmm, zmm, zmm",
        "2 ops -> 1, half the rounding error, 2 FMAs/cycle",
        feats1("AVX2"), feats2("vmulps", "vaddps"), gate_any(), false), 2))

    // D: same for double precision.
    r.push(with_min_triggers(rule("fma-fuse-pd",
        "Fuse vmulpd + vaddpd into vfmadd231pd",
        "Separate multiply+add in double precision fuses into one vfmadd231pd (single rounding, 2/cycle).",
        "vfmadd231pd zmm, zmm, zmm",
        "2 ops -> 1, single rounding",
        feats1("AVX2"), feats2("vmulpd", "vaddpd"), gate_any(), false), 2))

    // C: int8 product-accumulate -> VNNI vpdpbusd.
    r.push(rule("vnni-int8-dotprod",
        "Fuse int8 multiply-accumulate into vpdpbusd (VNNI)",
        "vpmaddubsw + vpmaddwd + vpaddd is the pre-VNNI int8 dot-product; vpdpbusd does 64 int8 MACs into int32 in one op on p0+p5.",
        "vpdpbusd zmm_acc, zmm_a, zmm_b",
        "3 ops -> 1, 64 int8 MACs/cycle (quantized DL / vector search)",
        feats1("VNNI"), feats1("vpmaddubsw"), gate_any(), false))

    // C: int16 product-accumulate -> VNNI vpdpwssd (also fires on INT16 complex mult).
    r.push(rule("vnni-int16-dotprod",
        "Accumulate int16 products with vpdpwssd (VNNI)",
        "If vpmaddwd feeds a vpaddd accumulator, vpdpwssd fuses the multiply-add of 32 int16 pairs into int32 in one op.",
        "vpdpwssd zmm_acc, zmm_a, zmm_b",
        "fuses the int16 multiply-add accumulate",
        feats1("VNNI"), feats1("vpmaddwd"), gate_any(), false))

    // D: full division on the throughput path -> reciprocal + one Newton step.
    r.push(rule("div-to-rcp",
        "Replace full division with vrcpps/vrsqrtps + one Newton step",
        "vdivps/vdivpd are long-latency, low-throughput. An approximate reciprocal plus one Newton-Raphson refinement is far faster when full precision is not required.",
        "vrcpps + one Newton step (x' = x*(2 - d*x)); or vrsqrtps for 1/sqrt",
        "approximate+refine beats full division on throughput",
        feats1("AVX"), feats2("vdivps", "vdivpd"), gate_any(), true))

    // A: fold a broadcast into the consuming EVEX op via {1toN} embedded broadcast.
    r.push(rule("embed-broadcast",
        "Fold the broadcast into the EVEX op via {1toN}",
        "A standalone vbroadcastss/vpbroadcastd before an AVX-512 op can be replaced by the op's embedded {1toN} broadcast on a memory/register source, saving one uop and a register.",
        "vfmadd231ps zmm, zmm, dword ptr [mem]{1to16}",
        "-1 broadcast uop and -1 register",
        feats1("AVX512"), feats2("vbroadcastss", "vpbroadcastd"), gate_any(), false))

    // F: scalar CRC -> vectorized carryless-multiply folding.
    r.push(rule("crc-clmul",
        "Vectorize CRC with vpclmulqdq (carryless multiply)",
        "A scalar crc32 loop is byte-at-a-time; vpclmulqdq folds the polynomial over wide blocks, reaching GB/s.",
        "vpclmulqdq folding scheme (the standard CRC-via-PCLMUL reduction)",
        "byte-at-a-time -> GB/s polynomial folding",
        feats1("VPCLMULQDQ"), feats1("crc32"), gate_any(), false))

    // E/anti: scalarizing a vector via vpextr* serializes on p5 (a scalar-pack trap).
    r.push(rule("vpextr-scalarize",
        "Extracting vector lanes to GPRs serializes on the shuffle port p5",
        "vpextrb/vpextrw/vpextrd move one lane at a time to a GPR, all on p5. A loop that scalarizes a vector this way (e.g. to pack bits in scalar code) bottlenecks on p5 — exactly the trap a scalar bit-pack hits before going all-SIMD (a compiler can emit dozens of vpextrw/PRB, all on p5).",
        "keep the data in vectors: vpcompressb/vpermb to gather the wanted bytes then one masked store; or pack with the vpsllvw stagger (see bfp-pack-stagger)",
        "removes a string of p5 lane-extracts (dozens of vpextrw/PRB -> 0)",
        feats0(), feats3("vpextrb", "vpextrw", "vpextrd"), gate_any(), true))

    return r
}

// ============================================================================
// firing predicate (pure data) + advise()
// ============================================================================

def gate_matches(int gate, int bott_kind, int bott_port) -> bool {
    if gate == gate_any() { return true }
    if gate == gate_port() { return bott_kind == bk_port() }
    if gate == gate_port5() { return bott_kind == bk_port() && bott_port == 5 }
    if gate == gate_dep() { return bott_kind == bk_dependency() }
    if gate == gate_frontend() { return bott_kind == bk_frontend() }
    return false
}

def prog_has(&Vec(ir.Inst) prog, &Str mn) -> bool {
    for ins in &prog {
        if ins.mnemonic.eq?(mn) { return true }
    }
    return false
}

// how many distinct trigger mnemonics appear in the kernel
def count_triggers(&Vec(ir.Inst) prog, &Vec(Str) trig) -> int {
    int c = 0
    for t in &trig {
        if prog_has(prog, t) { c = c + 1 }
    }
    return c
}

// trigger satisfied? empty list = always relevant; else >= mincount distinct present
def trigger_met(&Vec(ir.Inst) prog, &Vec(Str) trig, int mincount) -> bool {
    if trig.len() == 0 { return true }
    return count_triggers(prog, trig) >= mincount
}

// all required features available? empty list = no requirement
def isa_all(&Vec(Str) isa, &Vec(Str) need) -> bool {
    for f in &need {
        bool found = false
        for h in &isa {
            if h.eq?(f) { found = true }
        }
        if !found { return false }
    }
    return true
}

// does the kernel touch 512-bit (zmm) registers? (μarch downclock gating)
def prog_uses_zmm(&Vec(ir.Inst) prog) -> bool {
    Str z = "zmm"
    for ins in &prog {
        for op in &ins.ops {
            if op.text.starts_with?(&z) { return true }
        }
    }
    return false
}

// the result: a fired rule (we copy the rule's strings out so callers own them)
struct Suggestion {
    Str id
    Str title
    Str rationale
    Str suggested
    Str expected_gain
    bool is_antipattern
}

// avx512_downclock = the target μarch's downclock severity (uarch.avx512_downclock).
def advise(&Vec(ir.Inst) prog, int bott_kind, int bott_port, &Vec(Str) isa,
           int avx512_downclock) -> Vec(Suggestion) {
    bool uses_zmm = prog_uses_zmm(prog)
    Vec(SimdRule) rules = all_rules()
    Vec(Suggestion) out = {}
    int n = rules.len()
    for i in 0..n {
        &SimdRule ru = rules.get_ref(i)
        bool dc_ok = true
        if ru.min_downclock >= 0 {
            if avx512_downclock < ru.min_downclock { dc_ok = false }
            if ru.needs_zmm { if !uses_zmm { dc_ok = false } }
        }
        if dc_ok {
            if gate_matches(ru.gate, bott_kind, bott_port) {
                if isa_all(isa, &ru.requires_isa) {
                    if trigger_met(prog, &ru.trigger, ru.min_triggers) {
                        out.push(Suggestion { id: ru.id.copy(), title: ru.title.copy(),
                            rationale: ru.rationale.copy(), suggested: ru.suggested.copy(),
                            expected_gain: ru.expected_gain.copy(),
                            is_antipattern: ru.is_antipattern })
                    }
                }
            }
        }
    }
    return out
}

// ---- text rendering ----------------------------------------------------------
def render(&Vec(Suggestion) sg) -> Str {
    Str out = "=== advisor suggestions ===\n"
    int n = sg.len()
    if n == 0 {
        out = f"{out}  (no rules fired for this kernel/bottleneck)\n"
        return out
    }
    for i in 0..n {
        &Suggestion s = sg.get_ref(i)
        Str tag = "IDIOM"
        if s.is_antipattern { tag = "WARN " }
        out = f"{out}  [{tag}] {s.id}: {s.title}\n"
        out = f"{out}         why: {s.rationale}\n"
        out = f"{out}         fix: {s.suggested}\n"
        out = f"{out}         gain: {s.expected_gain}\n"
    }
    return out
}

// ============================================================================
// Provenance-aware advice (plan §6.6d) — the example an opcode linter CANNOT give.
//
// A complex multiply's best lowering depends on whether the coefficient
// (FFT twiddle / FIR tap / known rotation) is a compile-time / loop-invariant
// CONSTANT or a per-iteration DYNAMIC value. Same opcode, different best answer —
// the advisor only knows which by consuming sim.core.analysis provenance facts.
//   * coefficient invariant -> INT16: pre-arrange [c,-d]/[d,c], 2x vpmaddwd + pack
//                              (high p0/p1 throughput, no FP16 hardware needed).
//   * both operands dynamic  -> FP16 native complex multiply vfmulcph (if available),
//                              else the 3-mul + shuffle scheme / F16C + fp32.
// ============================================================================

def _sug(Str id, Str title, Str why, Str fix, Str gain) -> Suggestion {
    return Suggestion { id: id, title: title, rationale: why, suggested: fix,
                        expected_gain: gain, is_antipattern: false }
}

def _is_mul(&Str mn) -> bool {
    if mn.eq?("vpmaddwd") { return true }
    if mn.eq?("vpmullw") { return true }
    if mn.eq?("vfmulcph") { return true }
    if mn.eq?("vmulps") { return true }
    if mn.eq?("vmulpd") { return true }
    return false
}

def has_complex_multiply(&Vec(ir.Inst) prog) -> bool {
    for ins in &prog {
        if _is_mul(&ins.mnemonic) { return true }
    }
    return false
}

// does the first multiply read a loop-invariant/constant source (the coefficient)?
// uses sim.core.analysis provenance — const_regs annotates the known-constant tables.
def coeff_invariant_in(&Vec(ir.Inst) prog, &Vec(Str) const_regs) -> bool {
    Vec(Str) written = an.written_regs(prog)
    int n = prog.len()
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        if _is_mul(&ins.mnemonic) {
            int m = ins.ops.len()
            for j in 0..m {
                &ir.Operand op = ins.ops.get_ref(j)
                if op.is_read {
                    if an.is_invariant(an.classify(op, &written, const_regs)) { return true }
                }
            }
        }
    }
    return false
}

def advise_complex_multiply(bool coeff_invariant, bool has_fp16) -> Suggestion {
    if coeff_invariant {
        return _sug("cmul-int16-const",
            "Complex multiply with a constant coefficient -> INT16 vpmaddwd",
            "The coefficient is loop-invariant (twiddle/tap), so pre-arrange it once outside the loop as crossed+signed vectors [c,-d,..] and [d,c,..]; each point's complex product is then 2x vpmaddwd + a pack, on the high-throughput p0/p1 ports.",
            "real = vpmaddwd(iq,[c,-d]); imag = vpmaddwd(iq,[d,c]); vpackssdw",
            "~fp16 throughput with no FP16 hardware; standard fixed-point FFT (IPP/Ne10) approach")
    }
    if has_fp16 {
        return _sug("cmul-fp16-native",
            "Complex multiply, both operands dynamic -> FP16 vfmulcph",
            "Both operands vary per iteration, so there is no constant to pre-arrange; native fp16 complex multiply does the (a+bi)(c+di) product in one instruction on adjacent fp16 pairs.",
            "vfmulcph zmm, zmm, zmm  (vfcmulcph for the conjugate product)",
            "one native op replaces the 3-mul + shuffle scheme (needs AVX512_FP16)")
    }
    return _sug("cmul-fallback",
        "Complex multiply, both dynamic, no FP16 -> 3-mul + shuffle / F16C",
        "Both operands dynamic and the target lacks AVX512_FP16; use the classic 3-multiply Karatsuba-style complex product with shuffles, or store as fp16 (F16C) and compute in fp32.",
        "3 vmulps + vaddsubps, or F16C load/store with fp32 math",
        "no native complex op available on this target")
}

// orchestrated: emit the §6.6d suggestion iff the kernel has a complex multiply.
def advise_cmul(&Vec(ir.Inst) prog, &Vec(Str) const_regs, bool has_fp16) -> Vec(Suggestion) {
    Vec(Suggestion) out = {}
    if has_complex_multiply(prog) {
        bool inv = coeff_invariant_in(prog, const_regs)
        out.push(advise_complex_multiply(inv, has_fp16))
    }
    return out
}

// LICM: an instruction whose inputs are all loop-invariant (via the §4.3 fixpoint)
// computes the same value every iteration and can be hoisted out of the loop. This
// is provenance-driven advice an opcode linter cannot give (it depends on which
// operands are constant), and the result is invariant transitively.
def advise_licm(&Vec(ir.Inst) prog, &Vec(Str) const_regs) -> Vec(Suggestion) {
    Vec(Str) inv = an.invariant_regs(prog, const_regs)
    Vec(Suggestion) out = {}
    int n = prog.len()
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        if an.inst_is_hoistable(ins, &inv) {
            Str mn = ins.mnemonic.copy()
            out.push(_sug("licm-hoist",
                f"Hoist loop-invariant '{mn}' out of the loop",
                "Every input of this instruction is loop-invariant (constant or computed only from constants), so it produces the same result on every iteration.",
                "compute it once before the loop and keep the result in a register",
                "removes a per-iteration op from the hot loop"))
        }
    }
    return out
}
