// sim.intel.semantics — built-in instruction-semantics DB + symbolic executor +
// auto-renderer.  Architecture the user asked for:
//
//   assembly (+ control-register constants)  ->  [this library]  ->  text view
//
// NOTHING about a specific kernel is hardcoded. The library carries a declarative
// SPEC TABLE (the "built-in encoding") describing each instruction's class, element
// width, lane behaviour and operand roles; a BIT-LEVEL symbolic executor that runs
// the instruction on a register state using the control constants; and a renderer
// that, driven by the spec, auto-selects the right view (byte lane-map / bit-level).
//
// Width-generic: register width is taken from the state length (128/256/512-bit).
// Element-generic: byte/word/dword/qword permutes share one element-permute engine.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.regview as rv
import sim.core.ir as ir
import sim.intel.decode as decode

// =============================================================================
// 1. SYMBOLIC STATE — a register is `nbits` bit-slots, each a SOURCE BIT id:
//    >= 0  -> came from input bit `id`;  -1 -> zero/dead;  -2 -> unknown.
// =============================================================================
struct State { Vec(int) bits }

def mk_input(int nbits) -> State {
    Vec(int) b = {}
    for i in 0..nbits { b.push(i) }     // input register: bit i has identity i
    return State { bits: b }
}
// like mk_input but bit i carries identity i+off — used to give a SECOND source
// register distinct byte identities (off=512 -> its bytes read as 64..127, a separate
// colour family) so two-input ops (unpack/blend/align) show which src each byte came from.
def mk_input_off(int nbits, int off) -> State {
    Vec(int) b = {}
    for i in 0..nbits { b.push(i + off) }
    return State { bits: b }
}
def mk_dead(int nbits) -> State {
    Vec(int) b = {}
    for i in 0..nbits { b.push(-1) }
    return State { bits: b }
}
def st_copy(&State s) -> State {
    Vec(int) b = {}
    int n = s.bits.len()
    for i in 0..n { b.push(s.bits.get!(i)) }
    return State { bits: b }
}

// per-byte source: for each byte, the SRC byte index if all 8 bits came contiguously
// from one source byte (a clean byte move), -1 if dead, -2 if sub-byte/mixed (a shift
// crossed byte boundaries -> needs the bit view, not the byte view).
def byte_src(&State s) -> Vec(int) {
    Vec(int) out = {}
    int nb = s.bits.len() / 8
    for b in 0..nb {
        int base = b * 8
        int first = s.bits.get!(base)
        int kind = 0                 // 0 clean, 1 dead, 2 mixed
        if first < 0 { kind = 1 }
        else { if first % 8 != 0 { kind = 2 } }
        int sb = -1
        if kind == 0 { sb = first / 8 }
        // verify the other 7 bits are first+1..first+7 (contiguous, same byte)
        for k in 1..8 {
            int v = s.bits.get!(base + k)
            if kind == 0 {
                if v != first + k { kind = 2 }
            }
        }
        if kind == 1 { out.push(-1) }
        else { if kind == 2 { out.push(-2) } else { out.push(sb) } }
    }
    return out
}

// per-byte liveness (any live bit) -> for lane_map_ids (id>=0 live).
def live_bytes(&State s) -> Vec(int) {
    Vec(int) out = {}
    int nb = s.bits.len() / 8
    for b in 0..nb {
        int live = -1
        for k in 0..8 { if s.bits.get!(b * 8 + k) >= 0 { live = b } }
        out.push(live)
    }
    return out
}

// =============================================================================
// 2. CONTROL CONSTANTS — control-register values supplied with the assembly
//    (these come from the constant loads; they are part of the program input).
//    Stored as parallel name/value lists; values are per-ELEMENT (the element
//    width is taken from the instruction's spec).
// =============================================================================
struct Consts { Vec(Str) names; Vec(Vec(int)) vals }
def consts_new() -> Consts { Vec(Str) n = {}; Vec(Vec(int)) v = {}; return Consts { names: n, vals: v } }
def consts_put(&!Consts c, Str name, Vec(int) val) { c.names.push(name); c.vals.push(val) }
def consts_get(&Consts c, &Str name) -> Vec(int) {
    int n = c.names.len()
    for i in 0..n {
        &Str nm = c.names.get_ref(i)
        if nm.eq?(name) { return c.vals.get!(i) }
    }
    Vec(int) empty = {}
    return empty
}
def consts_has(&Consts c, &Str name) -> bool {
    int n = c.names.len()
    for i in 0..n {
        &Str nm = c.names.get_ref(i)
        if nm.eq?(name) { return true }
    }
    return false
}

// =============================================================================
// 3. SPEC TABLE — the declarative "built-in encoding" (instruction database).
//
// Each row: { mnemonic, op-class, element bits, cross-lane?, ctrl-operand index,
//             class name, one-line semantics }. Adding an instruction = adding a
//     row (data), never new code. op classes split into DATA-MOVEMENT (op < 20,
//     symbolically executed + movement-rendered) and COMPUTE (op >= 20, classified
//     and structurally described — identity tracking does not apply to new values).
// ctrl_pos = which SOURCE operand (0 = first src after dst, 1 = second, ...) carries
//     the control mask / shift amount; -1 = none (or imm-encoded).  This captures
//     that e.g. vpermb's control is src0 but vpshufb's is src1.
// =============================================================================
// --- movement classes ---
def OP_PERMUTE() -> int { return 0 }    // cross/in-lane element gather dst[e]=data[ctrl[e]]
def OP_SHIFT_VL() -> int { return 1 }   // per-element variable shift left
def OP_SHIFT_VR() -> int { return 2 }   // per-element variable shift right
def OP_SHIFT_IL() -> int { return 3 }   // per-element immediate shift left
def OP_SHIFT_IR() -> int { return 4 }   // per-element immediate shift right
def OP_BROADCAST() -> int { return 5 }  // replicate one element across the register
def OP_UNPACK_LO() -> int { return 6 }  // interleave low halves
def OP_UNPACK_HI() -> int { return 7 }  // interleave high halves
def OP_BLEND() -> int { return 8 }      // select a or b per element (mask/imm)
def OP_ALIGN() -> int { return 9 }      // concat+byte-shift (valignr)
def OP_LOGIC_OR() -> int { return 10 }  // bitwise OR (treated as disjoint merge)
def OP_STORE() -> int { return 11 }     // masked store
def OP_LOAD() -> int { return 12 }      // (masked) load
// --- compute classes (classified, not movement) ---
def OP_INT_ALU() -> int { return 20 }   // add/sub/min/max/abs
def OP_INT_MUL() -> int { return 21 }   // multiply / multiply-add (dot)
def OP_FP_ALU() -> int { return 22 }    // fp add/sub/mul/div
def OP_FMA() -> int { return 23 }       // fused multiply-add
def OP_FP_FUNC() -> int { return 24 }   // sqrt/rcp/rsqrt/scalef/getexp...
def OP_CMP() -> int { return 25 }       // compare -> mask
def OP_CONVERT() -> int { return 26 }   // cvt / sign-zero-extend / truncate
def OP_TERNLOG() -> int { return 27 }   // vpternlog
def OP_LOGIC() -> int { return 28 }     // and/andn/xor (general logic)
def OP_BITCOUNT() -> int { return 29 }  // popcnt/lzcnt/conflict
def OP_CRYPTO() -> int { return 30 }    // aes/clmul/gf2p8
def OP_MASKOP() -> int { return 31 }    // k-register ops
def OP_UNKNOWN() -> int { return 99 }

def is_movement(int op) -> bool { return op < 20 }

def class_name(int op) -> Str {
    if op == 0  { return "permute" }
    if op == 1  { return "shift-var-left" }
    if op == 2  { return "shift-var-right" }
    if op == 3  { return "shift-imm-left" }
    if op == 4  { return "shift-imm-right" }
    if op == 5  { return "broadcast" }
    if op == 6  { return "unpack-lo" }
    if op == 7  { return "unpack-hi" }
    if op == 8  { return "blend" }
    if op == 9  { return "align" }
    if op == 10 { return "logic-or" }
    if op == 11 { return "store" }
    if op == 12 { return "load" }
    if op == 20 { return "int-alu" }
    if op == 21 { return "int-mul/dot" }
    if op == 22 { return "fp-alu" }
    if op == 23 { return "fma" }
    if op == 24 { return "fp-func" }
    if op == 25 { return "compare" }
    if op == 26 { return "convert" }
    if op == 27 { return "ternlog" }
    if op == 28 { return "logic" }
    if op == 29 { return "bitcount" }
    if op == 30 { return "crypto" }
    if op == 31 { return "mask-op" }
    return "unknown"
}

// nsrc = how many SOURCE vector tables the index draws from (a property of the index
// mask): 1 for ordinary permutes (vpermb/d/w/q/ps/pd), 2 for the two-table family
// vpermi2*/vpermt2* (index reaches across src1++src2; the top index bit selects the table).
struct SpecRow { Str mn; int op; int elem_bits; bool cross; int ctrl_pos; Str sem; int nsrc }
def perm_table_count(&Str mn) -> int {
    Str i2 = "rmi2"
    Str t2 = "rmt2"
    if mn.contains?(&i2) { return 2 }   // vpermi2b/d/w/q/ps/pd
    if mn.contains?(&t2) { return 2 }   // vpermt2b/d/w/q/ps/pd
    return 1
}
def spec_row(Str mn, int op, int ew, bool cross, int ctrl, Str sem) -> SpecRow {
    int ns = perm_table_count(&mn)
    return SpecRow { mn: mn, op: op, elem_bits: ew, cross: cross, ctrl_pos: ctrl, sem: sem, nsrc: ns }
}

// the built-in table. ew: 8/16/32/64 element bits. ctrl_pos per Intel operand order.

// look up a mnemonic; UNKNOWN row if absent.
def lookup(&Vec(SpecRow) tbl, &Str mn) -> SpecRow {
    int n = tbl.len()
    for i in 0..n {
        &SpecRow r = tbl.get_ref(i)
        if r.mn.eq?(mn) { return spec_row(r.mn.copy(), r.op, r.elem_bits, r.cross, r.ctrl_pos, r.sem.copy()) }
    }
    return spec_row(mn.copy(), 99, 8, false, -1, "(unmodeled)")
}

// =============================================================================
// 4. SYMBOLIC EXECUTOR — run one instruction on the state(s) using the ctrl const.
// =============================================================================
// element permute: dst element e <- data element ctrl[e]. cross-lane: ctrl is the
// global element index; in-lane (vpshufb): ctrl is lane-relative, top-bit -> zero.
def exec_permute(&State data, &Vec(int) ctrl, int elem_bits, bool cross) -> State {
    int nbits = data.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    int lane_elems = 128 / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    int cn = ctrl.len()
    for e in 0..nelem {
        int c = -1
        if e < cn { c = ctrl.get!(e) }
        int se = -1
        if c >= 0 {
            if cross { se = c }
            else {
                if ew == 8 { if c >= 128 { se = -1 } else { se = (e / lane_elems) * lane_elems + (c % lane_elems) } }
                else { se = (e / lane_elems) * lane_elems + (c % lane_elems) }
            }
        }
        if se >= 0 {
            int db = e * ew
            int sb = se * ew
            for k in 0..ew {
                if db + k < nbits { if sb + k < nbits { ob.set(db + k, data.bits.get!(sb + k)) } }
            }
        }
    }
    return State { bits: ob }
}

// two-table permute (vpermi2*/vpermt2*): the index reaches across a++b (2*nelem elements).
// Per element the low `idx_bits` bits choose the element within a table; bit[idx_bits]
// chooses the table (0 = a, 1 = b). Higher bits are ignored.
def exec_permute2(&State a, &State b, &Vec(int) ctrl, int elem_bits, int idx_bits) -> State {
    int nbits = a.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    int within_mask = (1 << idx_bits) - 1
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    int cn = ctrl.len()
    for e in 0..nelem {
        int c = -1
        if e < cn { c = ctrl.get!(e) }
        if c >= 0 {
            int within = c & within_mask
            int tbl = (c >> idx_bits) & 1
            int db = e * ew
            int sb = within * ew
            for k in 0..ew {
                int v = -1
                if tbl == 0 { if sb + k < nbits { v = a.bits.get!(sb + k) } }
                else { if sb + k < nbits { v = b.bits.get!(sb + k) } }
                if db + k < nbits { ob.set(db + k, v) }
            }
        }
    }
    return State { bits: ob }
}

// per-element variable left shift: element w shifted left by amt[w] bits, fill 0.
def exec_shift_l(&State data, &Vec(int) amt, int elem_bits) -> State {
    int nbits = data.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    int an = amt.len()
    for e in 0..nelem {
        int s = 0
        if e < an { s = amt.get!(e) }
        for b in 0..ew {
            int srcb = b - s                 // dst bit b <- src bit (b - shift)
            int base = e * ew
            if srcb >= 0 { ob.set(base + b, data.bits.get!(base + srcb)) }
        }
    }
    return State { bits: ob }
}

// bitwise OR of two data regs (assumes disjoint live bits, true for bit-field packing):
// dst bit = a if a is live, else b.
def exec_or(&State a, &State b) -> State {
    int nbits = a.bits.len()
    Vec(int) ob = {}
    for i in 0..nbits {
        int av = a.bits.get!(i)
        if av >= 0 { ob.push(av) } else { ob.push(b.bits.get!(i)) }
    }
    return State { bits: ob }
}

// per-element variable RIGHT shift: element w shifted right by amt[w] bits (logical,
// fill 0). Used for vpsrlv* (variable) and vpsrl*/vpsra* with a uniform amount.
def exec_shift_r(&State data, &Vec(int) amt, int elem_bits) -> State {
    int nbits = data.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    int an = amt.len()
    for e in 0..nelem {
        int s = 0
        if e < an { s = amt.get!(e) }
        for b in 0..ew {
            int srcb = b + s                 // dst bit b <- src bit (b + shift)
            int base = e * ew
            if srcb < ew { ob.set(base + b, data.bits.get!(base + srcb)) }
        }
    }
    return State { bits: ob }
}

// broadcast: replicate source element 0 across every element (vpbroadcast*).
def exec_broadcast(&State data, int elem_bits) -> State {
    int nbits = data.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    for e in 0..nelem {
        for k in 0..ew { ob.set(e * ew + k, data.bits.get!(k)) }   // src element 0
    }
    return State { bits: ob }
}

// unpack/interleave low (hi=false) or high (hi=true) halves of each 128-bit lane from
// two sources a,b: dst even element <- a, odd element <- b (vpunpckl/h*).
def exec_unpack(&State a, &State b, int elem_bits, bool hi) -> State {
    int nbits = a.bits.len()
    int ew = elem_bits
    int lane_elems = 128 / ew
    int half = lane_elems / 2
    int nlanes = nbits / 128
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    for l in 0..nlanes {
        int lbe = l * lane_elems
        int off = 0
        if hi { off = half }
        for j in 0..half {
            int ae = lbe + off + j
            int de0 = lbe + 2 * j
            int de1 = lbe + 2 * j + 1
            for k in 0..ew { ob.set(de0 * ew + k, a.bits.get!(ae * ew + k)) }
            for k in 0..ew { ob.set(de1 * ew + k, b.bits.get!(ae * ew + k)) }
        }
    }
    return State { bits: ob }
}

// blend: per element, sel[e]==1 picks b, else a (vpblend* mask/imm).
def exec_blend(&State a, &State b, &Vec(int) sel, int elem_bits) -> State {
    int nbits = a.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    for e in 0..nelem {
        int s = 0
        if e < sel.len() { s = sel.get!(e) }
        for k in 0..ew {
            int v = a.bits.get!(e * ew + k)
            if s == 1 { v = b.bits.get!(e * ew + k) }
            ob.set(e * ew + k, v)
        }
    }
    return State { bits: ob }
}

// valignr: per 128-bit lane, concatenate b(high):a(low) = 32 bytes, shift right by
// `imm` bytes, keep low 16. dst byte i <- concat[i+imm].
// Generalized align (Intel VALIGN*/VPALIGNR): within each `lane_bytes`-wide region the
// concatenation is a:b with a (src1) HIGH and b (src2) LOW, then shifted right `byte_shift`
// bytes, keeping the low `lane_bytes`. So dst byte 0 comes from b (the SECOND source).
//   vpalignr      -> lane_bytes=16 (per 128-bit lane), byte_shift=imm
//   valignd/valignq -> lane_bytes=whole register, byte_shift=imm*(elem_bytes)
def exec_align(&State a, &State b, int byte_shift, int lane_bytes) -> State {
    int nbits = a.bits.len()
    int nbytes = nbits / 8
    int lb = lane_bytes
    if lb < 1 { lb = nbytes }
    int nlanes = nbytes / lb
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    for l in 0..nlanes {
        int base = l * lb
        for i in 0..lb {
            int idx = i + byte_shift
            for k in 0..8 {
                int v = -1
                if idx < lb { v = b.bits.get!((base + idx) * 8 + k) }       // b (src2) is the LOW half
                else { if idx < lb * 2 { v = a.bits.get!((base + idx - lb) * 8 + k) } }   // a (src1) is HIGH
                ob.set((base + i) * 8 + k, v)
            }
        }
    }
    return State { bits: ob }
}

// compress: active elements (keep[e]==1) packed CONTIGUOUSLY into the low end of dst
// (the write-mask k drives vcompress*/vpcompress*); inactive slots become dead.
def exec_compress(&State data, &Vec(int) keep, int elem_bits) -> State {
    int nbits = data.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    int dpos = 0
    for e in 0..nelem {
        int k = 0
        if e < keep.len() { k = keep.get!(e) }
        if k == 1 {
            for b in 0..ew { ob.set(dpos * ew + b, data.bits.get!(e * ew + b)) }
            dpos = dpos + 1
        }
    }
    return State { bits: ob }
}

// expand: inverse of compress — consecutive source elements are scattered to the
// positions where keep[e]==1 (vexpand*/vpexpand*); other slots become dead.
def exec_expand(&State data, &Vec(int) keep, int elem_bits) -> State {
    int nbits = data.bits.len()
    int ew = elem_bits
    int nelem = nbits / ew
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(-1) }
    int spos = 0
    for e in 0..nelem {
        int k = 0
        if e < keep.len() { k = keep.get!(e) }
        if k == 1 {
            for b in 0..ew { ob.set(e * ew + b, data.bits.get!(spos * ew + b)) }
            spos = spos + 1
        }
    }
    return State { bits: ob }
}

// masked store: keep only bytes whose mask bit is set (mask = per-byte 0/1 list).
def exec_store(&State data, &Vec(int) mask) -> State {
    int nbits = data.bits.len()
    Vec(int) ob = {}
    for i in 0..nbits { ob.push(data.bits.get!(i)) }
    int nb = nbits / 8
    for b in 0..nb {
        int m = 0
        if b < mask.len() { m = mask.get!(b) }
        if m == 0 { for k in 0..8 { ob.set(b * 8 + k, -1) } }
    }
    return State { bits: ob }
}

// =============================================================================
// 5. AUTO-RENDER — driven by the spec's op class, pick the right view.
// =============================================================================
struct Run { int dlo; int dhi; int slo; int shi }
def _runs(&Vec(int) src) -> Vec(Run) {
    Vec(Run) rs = {}
    int n = src.len()
    int i = 0
    while i < n {
        int m = src.get!(i)
        if m < 0 { i = i + 1; continue }
        int j = i
        while j + 1 < n {
            if src.get!(j + 1) == src.get!(j) + 1 { j = j + 1 } else { break }
        }
        rs.push(Run { dlo: i, dhi: j, slo: m, shi: src.get!(j) })
        i = j + 1
    }
    return rs
}
def _render_runs(&Vec(int) bsrc) -> Str {
    Vec(Run) rs = _runs(bsrc)
    Str out = "  byte moves (auto-derived, dst<-src, MSB-first):\n"
    int nr = rs.len()
    int ri = nr - 1
    while ri >= 0 {
        Run r = rs.get!(ri)
        Str d = f"dst[{r.dhi}..{r.dlo}]".pad_right(13, 32)
        Str s = f"<- src[{r.shi}..{r.slo}]".pad_right(15, 32)
        Str tag = "in-lane"
        if r.dlo / 16 != r.slo / 16 { tag = "CROSS-LANE" }
        out = f"{out}    {d} {s} {tag}\n"
        ri = ri - 1
    }
    return out
}

// integer list -> "a b c" (for showing shift amounts / mask values, MSB-first)
def _ints_msb(&Vec(int) v, int lo, int hi) -> Str {
    Str s = ""
    int i = hi
    while i >= lo {
        Str cell = f"{v.get!(i)}"
        s = f"{s}{cell.pad_left(3, 32)}"
        i = i - 1
    }
    return s
}

// =============================================================================
// 6. DRIVER (partial) — apply the built-in table to an assembly listing. For each
//    instruction the library reports its class / element width / movement-vs-compute
//    / semantics straight from the spec table (no per-kernel hardcoding). The full
//    symbolic data-movement render builds on exec_* (see sim.intel.movement).
// =============================================================================
def classify_listing(Str asm, &Vec(SpecRow) tbl) -> Str {
    Vec(ir.Inst) prog = decode.parse_listing(&asm, 0x400 as i64)
    Str out = "  instruction classification (from the built-in spec table):\n"
    int n = prog.len()
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        Str mn = ins.mnemonic.copy()
        SpecRow r = lookup(tbl, &mn)
        Str cls = class_name(r.op)
        Str mv = "compute "
        if is_movement(r.op) { mv = "movement" }
        Str sem_s = r.sem.copy()
        Str ew_s = f"ew{r.elem_bits}"
        Str line = f"    {mn.pad_right(13, 32)} {cls.pad_right(16, 32)} {ew_s.pad_right(6, 32)} {mv}  {sem_s}"
        out = f"{out}{line}\n"
    }
    return out
}
