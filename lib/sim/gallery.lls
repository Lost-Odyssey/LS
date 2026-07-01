// sim.gallery — full-ISA visual gallery (the library does ALL the work).
//
// The DRIVER feeds nothing but "render the gallery". For EVERY modelled instruction the
// library:
//   * shows a one-line FUNCTIONAL SUMMARY (the SDM title) and the MEANING OF EACH OPERAND
//     (destination / source / control-mask / immediate), derived from the spec;
//   * for control/index-shuffle instructions (vpermd, vpshufb, vpshufd, ...) it BINDS a
//     concrete realistic mask to the control register, DISPLAYS the mask's actual values,
//     and renders the SRC (before) and DST (after) registers;
//   * other movement classes (broadcast/unpack/blend/align/shift/or/load/store/pack)
//     render their byte- or bit-level SRC->DST view;
//   * compute / crypto / convert / compare / ... show summary + operands only (no
//     pseudocode — it is unreadable after extraction).
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.intel.semantics as sem
import sim.intel.isa_table as isa
import sim.intel.specdoc as specdoc
import sim.intel.decode as decode
import sim.htmlview as hv
import sim.regview as rv
import sim.core.ir as ir
import std.core.map

def VL() -> int { return 512 }

def _src_a() -> sem.State { return sem.mk_input(VL()) }
def _src_b() -> sem.State { return sem.mk_input_off(VL(), VL()) }
def _ids(&sem.State s) -> Vec(int) { return sem.byte_src(s) }

// ---- concrete realistic example masks (bound to the control register) -------
// cross-lane element permute: rotate elements right by 1 (dst[e] <- src[(e-1) mod n]).
def _idx_rotate_cross(int nelem) -> Vec(int) {
    Vec(int) c = {}
    for e in 0..nelem { c.push((e + nelem - 1) % nelem) }
    return c
}
// two-table index example (vpermi2*/vpermt2*): interleave the two sources, reading the
// index that equals the element position so the indices span the WHOLE table (the upper
// half exceeds half the table — e.g. >31 for bytes). Even dst elements read src1 at
// 0,2,4,...; odd dst elements read src2 at 1,3,5,...
//   dst[0]=src1[0], dst[1]=src2[1], dst[2]=src1[2], dst[3]=src2[3], ...
def _idx_interleave2(int nelem, int idx_bits) -> Vec(int) {
    Vec(int) c = {}
    for e in 0..nelem {
        int tbl = e % 2
        int within = e          // index = element position -> spans 0..nelem-1
        c.push((tbl << idx_bits) | within)
    }
    return c
}
// in-lane element shuffle: rotate within each 128-bit lane by 3 (lane-relative selector).
def _idx_rotate_lane(int ew) -> Vec(int) {
    int le = 128 / ew
    int nelem = VL() / ew
    Vec(int) c = {}
    for e in 0..nelem { c.push(((e % le) + 3) % le) }
    return c
}
// blend selector: alternate A,B,A,B,... (per element).
def _sel_alt(int nelem) -> Vec(int) {
    Vec(int) s = {}
    for e in 0..nelem { s.push(e % 2) }
    return s
}
def _store_mask_low(int keep) -> Vec(int) {
    Vec(int) m = {}
    for b in 0..64 { if b < keep { m.push(1) } else { m.push(0) } }
    return m
}
// compress/expand write-mask example: keep even-indexed elements (1=active).
def _keep_even(int nelem) -> Vec(int) {
    Vec(int) m = {}
    for e in 0..nelem { if e % 2 == 0 { m.push(1) } else { m.push(0) } }
    return m
}
def _is_expand(&Str mn) -> bool {
    Str e = "expand"
    return mn.contains?(&e)
}
def _active_elems(&Vec(int) keep) -> Vec(int) {     // indices where keep==1, in order
    Vec(int) r = {}
    int n = keep.len()
    for e in 0..n { if keep.get!(e) == 1 { r.push(e) } }
    return r
}
def _dense_indices(int count) -> Vec(int) {
    Vec(int) r = {}
    for i in 0..count { r.push(i) }
    return r
}
def _has_mem(&ir.Inst ins) -> bool {
    int no = ins.ops.len()
    for k in 0..no { &ir.Operand o = ins.ops.get_ref(k); if o.kind == 1 { return true } }
    return false
}

// ---- static mask-constant tracking (real end-to-end: resolve k1 from the code) ----
// normalize a GPR name to its 64-bit base so `mov al, 51` then `kmovd k1, eax` links up.
def _reg_base(&Str reg) -> Str {
    Str r = reg.copy()
    if r.eq?("al") { return "rax" }
    if r.eq?("ah") { return "rax" }
    if r.eq?("ax") { return "rax" }
    if r.eq?("eax") { return "rax" }
    if r.eq?("rax") { return "rax" }
    if r.eq?("bl") { return "rbx" }
    if r.eq?("bx") { return "rbx" }
    if r.eq?("ebx") { return "rbx" }
    if r.eq?("rbx") { return "rbx" }
    if r.eq?("cl") { return "rcx" }
    if r.eq?("cx") { return "rcx" }
    if r.eq?("ecx") { return "rcx" }
    if r.eq?("rcx") { return "rcx" }
    if r.eq?("dl") { return "rdx" }
    if r.eq?("dx") { return "rdx" }
    if r.eq?("edx") { return "rdx" }
    if r.eq?("rdx") { return "rdx" }
    if r.eq?("esi") { return "rsi" }
    if r.eq?("rsi") { return "rsi" }
    if r.eq?("edi") { return "rdi" }
    if r.eq?("rdi") { return "rdi" }
    return r.copy()
}
// the k-register named in a `{kN}` decoration on any operand, "" if none.
def _mask_reg_of(&ir.Inst ins) -> Str {
    Str open = "{k"
    int no = ins.ops.len()
    for k in 0..no {
        &ir.Operand o = ins.ops.get_ref(k)
        Str t = o.text.copy()
        int p = t.find(&open)
        if p >= 0 {
            int start = p + 1               // at the 'k'
            int i = start + 1
            int n = t.len()
            while i < n {
                int c = t.byte_at(i)
                bool dig = false
                if c >= 48 { if c <= 57 { dig = true } }
                if !dig { break }
                i = i + 1
            }
            return t.substr(start, i - start)   // "k1"
        }
    }
    return ""
}
// single-borrow accessors (avoid consecutive `&`-led decls, which glue at stmt edges).
def _op_kind(&ir.Inst ins, int i) -> int {
    &ir.Operand o = ins.ops.get_ref(i)
    return o.kind
}
def _op_text(&ir.Inst ins, int i) -> Str {
    &ir.Operand o = ins.ops.get_ref(i)
    return o.text.copy()
}
// the address expression inside the memory operand's [ ... ] (e.g. "rdi", "rsp + 16").
def _mem_base(&ir.Inst ins) -> Str {
    Str lb = "["
    Str rb = "]"
    int no = ins.ops.len()
    for k in 0..no {
        Str t = _op_text(ins, k)
        int p = t.find(&lb)
        if p >= 0 {
            Str rest = t.substr(p + 1, t.len() - p - 1)
            int q = rest.find(&rb)
            if q >= 0 { Str inner = rest.substr(0, q); return inner.trim() }
        }
    }
    return "mem"
}
// scan the program: `mov gpr, imm` then `kmov k, gpr` -> k's constant value.
def _track_mask_consts(&Vec(ir.Inst) prog) -> Map(Str, int) {
    Map(Str, int) gpr = {}
    Map(Str, int) kreg = {}
    Str mov = "mov"
    Str kmov = "kmov"
    int n = prog.len()
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        Str mn = ins.mnemonic.copy()
        int no = ins.ops.len()
        if no >= 2 {
            if mn.eq?(mov) {
                if _op_kind(ins, 0) == 0 { if _op_kind(ins, 1) == 2 {
                    Str dt = _op_text(ins, 0)
                    Str base = _reg_base(&dt)
                    Str st = _op_text(ins, 1)
                    int v = st.to_int().unwrap_or(0)
                    gpr.set(base, v)
                } }
            }
            if mn.starts_with?(kmov) {
                if _op_kind(ins, 1) == 0 {
                    Str st2 = _op_text(ins, 1)
                    Str base2 = _reg_base(&st2)
                    match gpr.get(&base2) {
                        Some(v) => { Str dt2 = _op_text(ins, 0); kreg.set(dt2, v) }
                        None => {}
                    }
                }
            }
        }
    }
    return kreg
}
def _bits_from_int(int val, int n) -> Vec(int) {
    Vec(int) b = {}
    for e in 0..n { b.push((val >> e) & 1) }
    return b
}
def _bits_to_int(&Vec(int) bits) -> i64 {
    i64 v = 0 as i64
    int n = bits.len()
    for e in 0..n { if bits.get!(e) == 1 { v = v | ((1 as i64) << (e as i64)) } }
    return v
}
// per-element-width illustrative mask: qword (pd) uses consecutive pairs (...0b11 0b00),
// narrower widths use an INTERLEAVED pattern (every other element) so the catalog shows
// both shapes (the consecutive-pairs case is already demonstrated by the qword version).
def _example_kmask(int ew, int nelem) -> Vec(int) {
    Vec(int) m = {}
    for e in 0..nelem {
        int bit = 0
        if ew == 64 { if (e % 4) < 2 { bit = 1 } }   // consecutive pairs (pd / q)
        else { if e % 2 == 0 { bit = 1 } }            // interleaved (ps / d / w / b)
        m.push(bit)
    }
    return m
}
// vpcompressb is shown with a BFP9-pack example: the low 9 bytes of lanes 0/1/2 are
// valid (the 9-byte stagger-packed groups), the 7-byte gaps and all of lane 3 are
// skipped — compress-store then writes the 27 bytes densely to memory.
def _is_bfp9_demo(&ir.Inst ins) -> bool {
    Str mn = ins.mnemonic.copy()
    return mn.eq?("vpcompressb")
}
def _bfp9_kmask() -> Vec(int) {
    Vec(int) m = {}
    for b in 0..64 {
        int lane = b / 16
        int off = b % 16
        int bit = 0
        if lane < 3 { if off < 9 { bit = 1 } }   // low 9 bytes of L0/L1/L2; skip gaps + L3
        m.push(bit)
    }
    return m
}
// per-element mask bits: the tracked constant if statically known, else an illustrative
// example (BFP9 pattern for vpcompressb, otherwise picked by element width).
def _kmask_bits(&ir.Inst ins, int ew, int nelem, &Map(Str, int) maskvals) -> Vec(int) {
    Str mreg = _mask_reg_of(ins)
    if mreg.len() > 0 {
        match maskvals.get(&mreg) {
            Some(v) => { return _bits_from_int(v, nelem) }
            None => {}
        }
    }
    if _is_bfp9_demo(ins) { return _bfp9_kmask() }
    return _example_kmask(ew, nelem)
}
// the k-mask label: tracked value if known, else the example value. `note` is an
// optional caller-supplied annotation (default ""); the generic renderer passes "" so
// nothing extra shows, the gallery passes a BFP9-style note for its hardcoded mask.
def _kmask_text(&ir.Inst ins, int ew, int nelem, &Map(Str, int) maskvals, Str note) -> Str {
    Str mreg = _mask_reg_of(ins)
    Str m = "k"
    if mreg.len() > 0 { m = mreg.copy() }
    if mreg.len() > 0 {
        match maskvals.get(&mreg) {
            Some(v) => { return f"write-mask {m} = {v} (tracked from kmov; actual bits)" }
            None => {}
        }
    }
    Vec(int) ex = _kmask_bits(ins, ew, nelem, maskvals)   // same bits the renderer draws
    i64 val = _bits_to_int(&ex)
    if note.len() > 0 { return f"write-mask {m} = {val}  ({note})" }
    return f"write-mask {m} = {val}"
}
// gallery-only annotation for a hardcoded/example mask ("" = none). Lives at the gallery
// layer, NOT the shared renderer, so a real .s file never gets a fabricated example note.
// Covers the k-mask (compress) and the index-register example (cross/in-lane permute).
def _gallery_mask_note(&ir.Inst ins, &sem.SpecRow r) -> Str {
    Str mn = ins.mnemonic.copy()
    if mn.eq?("vpcompressb") { return "BFP9 pack example: low 9 bytes of L0/L1/L2 valid; 7-byte gaps and L3 skipped" }
    if r.op == sem.OP_PERMUTE() {
        if r.ctrl_pos >= 0 {
            bool has_imm = false
            int no = ins.ops.len()
            for k in 0..no { if _op_kind(ins, k) == 2 { has_imm = true } }
            if !has_imm {                       // register-controlled index shuffle
                if r.nsrc >= 2 { return "example: interleave the two sources — even dst elements read src1 at 0,2,4,..., odd read src2 at 1,3,5,...; index = position, so it spans the whole table" }
                if r.cross { return "example: dst[i] = src[i-1] (each element pulls from the next-lower index; index 0 wraps to the top)" }
                return "example: dst[i] = src[i+3] within the 128-bit lane (pulls from 3 higher, wrapping inside the lane)"
            }
        }
    }
    return ""
}

// ---- immediate decode (parse layer knows the imm encoding) ------------------
def _imm_fields(int imm, int n) -> Vec(int) {
    Vec(int) f = {}
    for i in 0..n { f.push((imm >> (i * 2)) & 3) }
    return f
}
def _imm_perm_ctrl(int imm, int ew) -> Vec(int) {     // lane-relative per-element selector
    int le = 128 / ew
    Vec(int) fields = _imm_fields(imm, le)
    int nelem = VL() / ew
    Vec(int) c = {}
    for e in 0..nelem { c.push(fields.get!(e % le)) }
    return c
}
// vpermilpd: 1 bit per qword (imm8[e] picks the low/high qword of element e's 128-bit
// lane), 8 qwords -> 8 distinct bits (NOT a repeated field like the dword shuffles).
def _imm_perm_ctrl_pd(int imm) -> Vec(int) {
    int nelem = VL() / 64
    Vec(int) c = {}
    for e in 0..nelem { c.push((imm >> e) & 1) }
    return c
}
def _fields_str(&Vec(int) f) -> Str {
    Str s = "["
    int n = f.len()
    for i in 0..n { if i == 0 { s = f"{s}{f.get!(i)}" } else { s = f"{s},{f.get!(i)}" } }
    return s + "]"
}

// ---- down-convert pack family ----------------------------------------------
def _pack_src_ew(&Str mn) -> int {
    if mn.eq?("vpmovwb") { return 16 }
    if mn.eq?("vpmovdb") { return 32 }
    if mn.eq?("vpmovdw") { return 32 }
    if mn.eq?("vpmovqb") { return 64 }
    if mn.eq?("vpmovqw") { return 64 }
    if mn.eq?("vpmovqd") { return 64 }
    if mn.eq?("vpmovswb") { return 16 }
    if mn.eq?("vpmovuswb") { return 16 }
    if mn.eq?("vpmovsdb") { return 32 }
    if mn.eq?("vpmovusdb") { return 32 }
    if mn.eq?("vpmovsdw") { return 32 }
    if mn.eq?("vpmovusdw") { return 32 }
    if mn.eq?("vpmovsqd") { return 64 }
    if mn.eq?("vpmovusqd") { return 64 }
    if mn.eq?("vpmovsqw") { return 64 }
    if mn.eq?("vpmovusqw") { return 64 }
    if mn.eq?("vpmovsqb") { return 64 }
    if mn.eq?("vpmovusqb") { return 64 }
    return 0
}
// down-convert flavor for the desc: truncating (low bits) vs signed/unsigned saturating.
//   vpmovs*  -> signed saturate; vpmovus* -> unsigned saturate; rest -> truncate.
def _pack_kind(&Str mn) -> Str {
    Str movu = "movu"
    Str movs = "movs"
    if mn.contains?(&movu) { return "unsigned-saturating" }
    if mn.contains?(&movs) { return "signed-saturating" }
    return "truncating"
}
// vector-to-mask: vpmovb2m / vpmovw2m / vpmovd2m / vpmovq2m extract the sign bit (MSB) of
// each element into a k-register (carry "2m"; the reverse vpmovm2* carry "m2").
def _is_vec2mask(&Str mn) -> bool {
    Str s = "2m"
    return mn.contains?(&s)
}
def _pack_dst_ew(&Str mn) -> int {
    Str db = "db"
    Str wb = "wb"
    Str dw = "dw"
    Str qw = "qw"
    Str qd = "qd"
    if mn.contains?(&db) { return 8 }
    if mn.contains?(&wb) { return 8 }
    if mn.contains?(&dw) { return 16 }
    if mn.contains?(&qw) { return 16 }
    if mn.contains?(&qd) { return 32 }
    return 8
}
def _exec_pack(&sem.State src, int src_ew, int dst_ew) -> sem.State {
    sem.State d = sem.mk_dead(VL())
    int sbytes = src_ew / 8
    int dbytes = dst_ew / 8
    int oute = VL() / src_ew
    for i in 0..oute {
        for b in 0..dbytes {
            for k in 0..8 {
                d.bits.set((i * dbytes + b) * 8 + k, src.bits.get!((i * sbytes + b) * 8 + k))
            }
        }
    }
    return d
}

// ---- precise element data type (bit size + name), from ew + the spec's type tag ----
// spec.sem begins with a type token (f64 / f32 / u8 / i32 / ...); combine with the
// element width so the data type ALWAYS carries its exact bit size, e.g.
// "64-bit float (double precision)", "8-bit integer (byte)".
def _elem_type(int ew, &Str sem) -> Str {
    Str fp = "f"
    bool isf = sem.starts_with?(fp)
    Str kind = "integer"
    if isf { kind = "float" }
    Str detail = "qword"
    if ew == 8 { detail = "byte" }
    if ew == 16 { if isf { detail = "half precision" } else { detail = "word" } }
    if ew == 32 { if isf { detail = "single precision" } else { detail = "dword" } }
    if ew == 64 { if isf { detail = "double precision" } else { detail = "qword" } }
    return f"{ew}-bit {kind} ({detail})"
}

// the ISA / CPUID feature set (AVX512F, AVX512_FP16, AVX512_VBMI, ...) — the second
// token of spec.sem ("f16 AVX512_FP16" -> "AVX512_FP16"); "" if none.
def _isa_of(&Str sem) -> Str {
    Str sp = " "
    int p = sem.find(&sp)
    if p < 0 { return "" }
    Str rest = sem.substr(p + 1, sem.len() - p - 1)
    return rest.trim()
}

// ---- operand meanings (role of each operand, from the spec) -----------------
def _ctrl_role(int op) -> Str {
    if op == sem.OP_BLEND() { return "blend selector mask" }
    if op >= sem.OP_SHIFT_VL() { if op <= sem.OP_SHIFT_IR() { return "shift count" } }
    return "index / control mask"
}
def _imm_role(int op, int ew) -> Str {
    if op == sem.OP_BLEND() { return "blend immediate" }
    if op == sem.OP_ALIGN() {
        if ew == 8 { return "byte-shift immediate" }   // vpalignr
        if ew == 64 { return "qword-shift immediate" } // valignq
        return "dword-shift immediate"                 // valignd
    }
    if op == sem.OP_PERMUTE() { return "selector immediate" }
    if op >= sem.OP_SHIFT_VL() { if op <= sem.OP_SHIFT_IR() { return "shift count (immediate)" } }
    return "immediate" }
def _operands(&ir.Inst ins, &sem.SpecRow r) -> Str {
    Str s = ""
    int no = ins.ops.len()
    for k in 0..no {
        &ir.Operand o = ins.ops.get_ref(k)
        Str t = o.text.copy()
        Str role = "source data"
        if k == 0 {
            if o.kind == 1 { role = "destination (mem512, memory)" }
            else { if _is_kreg(&t) { role = "destination mask (k-register)" } else { role = "destination" } }
        } else {
            int si = k - 1
            if o.kind == 2 { role = _imm_role(r.op, r.elem_bits) }
            else {
                if r.ctrl_pos == si { role = _ctrl_role(r.op) }
                else {
                    if o.kind == 1 { role = "memory source (mem512)" }
                    else { if _is_kreg(&t) { role = "source mask (k-register)" } else { role = "source data" } }
                }
            }
        }
        Str piece = f"{t} = {role}"
        if k == 0 { s = piece } else { s = f"{s};  {piece}" }
    }
    return s
}

// ---- step header + renderers ------------------------------------------------
def _hdr(Str asml, Str meta, Str isa, Str summary, Str operands, Str desc) -> Str {
    Str h = f"  <div class='step'><h3>{hv.esc(asml)}</h3>\n"
    Str ew = hv.esc(meta)
    if isa.len() > 0 { ew = ew + "   ISA: <span class='isa'>" + hv.esc(isa) + "</span>" }
    h = h + f"    <div class='ew'>{ew}</div>\n"
    if summary.len() > 0 { h = h + f"    <div class='role'><b>summary:</b> {hv.esc(summary)}</div>\n" }
    if operands.len() > 0 { h = h + f"    <div class='maskline'><b>operands:</b> {hv.esc(operands)}</div>\n" }
    if desc.len() > 0 { h = h + f"    <div class='maskline'>{hv.esc(desc)}</div>\n" }
    return h
}
def _close() -> Str { return "  </div>\n" }
def _src_dst(Str h, &sem.State src, &sem.State dst) -> Str {
    Vec(int) si = _ids(src)
    Vec(int) di = _ids(dst)
    Str o = h + hv.h_lanes("SRC (before)", &si)
    o = o + hv.h_lanes("DST (after)", &di)
    return o + _close()
}
def _src_mask_dst(Str h, &sem.State src, Str maskrow, &sem.State dst) -> Str {
    Vec(int) si = _ids(src)
    Vec(int) di = _ids(dst)
    Str o = h + hv.h_lanes("SRC (before)", &si)
    o = o + maskrow
    o = o + hv.h_lanes("DST (after)", &di)
    return o + _close()
}
def _two_src(Str h, &sem.State a, &sem.State b, Str maskrow, &sem.State dst) -> Str {
    Vec(int) ai = _ids(a)
    Vec(int) bi = _ids(b)
    Vec(int) di = _ids(dst)
    Str o = h + hv.h_lanes("SRC A (blue/green/orange/purple)", &ai)
    o = o + hv.h_lanes("SRC B (red/teal/gold/pink)", &bi)
    if maskrow.len() > 0 { o = o + maskrow }
    o = o + hv.h_lanes("DST (after)", &di)
    return o + _close()
}
def _bitstep(Str h, i64 before, i64 after, int ew) -> Str {
    Str o = h + hv.h_bits(f"one representative {ew}-bit element (every lane identical)", before, after, ew)
    return o + _close()
}
// low ew bits set (ew==64 -> all 64). Used to confine the representative element value
// and the shift/rotate result to the real element width.
def _elem_mask(int ew) -> i64 {
    if ew >= 64 { return 0xFFFFFFFFFFFFFFFF as i64 }
    i64 one = 1
    return (one << (ew as i64)) - 1
}
// rotate vs plain shift: vprol*/vpror* carry "rol"/"ror"; no shift mnemonic does.
def _is_rotate(&Str mn) -> bool {
    Str m = mn.copy()
    Str rol = "rol"
    Str ror = "ror"
    return m.contains?(&rol) || m.contains?(&ror)
}
// arithmetic (sign-extending) right shift: psra*/vpsra* carry "sra"; logical srl/sll do not.
def _is_arith_shift(&Str mn) -> bool {
    Str m = mn.copy()
    Str sra = "sra"
    return m.contains?(&sra)
}
// mask-source broadcast: vpbroadcastmb2q / vpbroadcastmw2d broadcast a k-register's bits
// (NOT a vector element) into each lane. They carry "broadcastm".
def _is_mask_broadcast(&Str mn) -> bool {
    Str m = mn.copy()
    Str bm = "broadcastm"
    return m.contains?(&bm)
}
// vpermi2* (index overwritten) vs vpermt2* (a table overwritten): the operand that holds
// the index differs, so the two-table permute operand roles are labelled accordingly.
def _is_i2(&Str mn) -> bool {
    Str i2 = "rmi2"
    return mn.contains?(&i2)
}
// operand roles for a two-table permute (built from the actual operand texts, not synth).
//   vpermi2X dst, s1, s2:  dst = index + result;        s1 = table 1;  s2 = table 2
//   vpermt2X dst, s1, s2:  dst = table 1 + result;      s1 = index;    s2 = table 2
def _operands_perm2(&ir.Inst ins, bool is_i2) -> Str {
    int no = ins.ops.len()
    if no < 3 { return "" }
    Str o0 = _op_text(ins, 0)
    Str o1 = _op_text(ins, 1)
    Str o2 = _op_text(ins, 2)
    if is_i2 {
        return f"{o0} = index + destination (overwritten);  {o1} = source table 1;  {o2} = source table 2"
    }
    return f"{o0} = source table 1 + destination (overwritten);  {o1} = index / control mask;  {o2} = source table 2"
}
// a mask register operand (k0..k7) — used to label a source operand as a mask, not data.
def _is_kreg(&Str t) -> bool {
    int i = 0
    while i < 8 {
        Str kn = f"k{i}"
        if t.eq?(&kn) { return true }
        i = i + 1
    }
    return false
}

def _imm_of(&ir.Inst ins) -> int {
    int no = ins.ops.len()
    for k in 0..no {
        &ir.Operand o = ins.ops.get_ref(k)
        if o.kind == 2 { Str t = o.text.copy(); return t.to_int().unwrap_or(0 - 1) }
    }
    return 0 - 1
}

// ============================================================================
// render ONE instruction.
// ============================================================================
def render_one(&ir.Inst ins, &sem.SpecRow r, &Vec(specdoc.SpecDoc) sd, &Map(Str, int) maskvals, Str mask_note) -> Str {
    Str mn = ins.mnemonic.copy()
    int op = r.op
    int ew = r.elem_bits
    int imm = _imm_of(ins)

    Str asml = mn.copy()
    int no = ins.ops.len()
    for k in 0..no {
        &ir.Operand o = ins.ops.get_ref(k)
        Str t = o.text.copy()
        if k == 0 { asml = f"{asml} {t}" } else { asml = f"{asml}, {t}" }
    }
    Str semc = r.sem.copy()
    Str etype = _elem_type(ew, &semc)
    Str isa = _isa_of(&semc)
    Str clsname = sem.class_name(op)
    // vprol*/vpror* are tabled in the shift band but are rotates, not shifts: relabel so
    // the class line agrees with the summary ("Bit Rotate Left") and the bit grid.
    if op >= sem.OP_SHIFT_VL() {
        if op <= sem.OP_SHIFT_IR() {
            if _is_rotate(&mn) {
                clsname = "rotate-left"
                if op == sem.OP_SHIFT_VR() { clsname = "rotate-right" }
                if op == sem.OP_SHIFT_IR() { clsname = "rotate-right" }
            }
        }
    }
    Str meta = f"class={clsname}   data type: {etype}"
    Str summ = specdoc.summary_of(sd, &mn)
    Str ops = _operands(ins, r)

    sem.State a = _src_a()

    // ---- PERMUTE ----
    if op == sem.OP_PERMUTE() {
        if imm >= 0 {
            if ew == 32 {
                if !r.cross {
                    Vec(int) fields = _imm_fields(imm, 4)
                    Vec(int) ctrl = _imm_perm_ctrl(imm, ew)
                    sem.State d = sem.exec_permute(&a, &ctrl, ew, false)
                    Str fs = _fields_str(&fields)
                    Str desc = f"immediate decoded to dword selectors {fs} (lane-relative): dst dword e <- src dword sel[e]."
                    // single-source index mask, shown to scale as 32-bit hex nibbles (idx in
                    // the low 2 bits = 4 dwords per lane; sel_bit=-1 -> no table-select bit).
                    Str mr = hv.h_index_mask_sel("MASK (decoded from immediate)", &ctrl, ew, 2, 0 - 1)
                    return _src_mask_dst(_hdr(asml, meta, isa, summ, ops, desc), &a, mr, &d)
                }
            }
            // vpermilpd: 64-bit in-lane, 1 bit per qword (ctrl_pos>=0 distinguishes it from
            // the two-source vshufpd, which is ctrl_pos=-1).
            if ew == 64 {
                if !r.cross {
                    if r.ctrl_pos >= 0 {
                        Vec(int) ctrl = _imm_perm_ctrl_pd(imm)
                        sem.State d = sem.exec_permute(&a, &ctrl, ew, false)
                        Str fs = _fields_str(&ctrl)
                        Str desc = f"immediate decoded to per-qword selectors {fs}: dst qword e <- the qword chosen by bit e (0 = low qword of its 128-bit lane, 1 = high qword)."
                        // single-source, 1-bit lane-relative selector, shown to scale as nibbles.
                        Str mr = hv.h_index_mask_sel("MASK (decoded from immediate)", &ctrl, ew, 1, 0 - 1)
                        return _src_mask_dst(_hdr(asml, meta, isa, summ, ops, desc), &a, mr, &d)
                    }
                }
            }
            return _hdr(asml, meta, isa, summ, ops, "immediate-controlled shuffle (element width / lane layout omitted from grid).") + _close()
        }
        if r.ctrl_pos >= 0 {
            int nelem = VL() / ew
            // two-table permute (vpermi2*/vpermt2*): index reaches across src1++src2; the
            // select bit (a property of the mask) picks the table. Generic renderer.
            if r.nsrc >= 2 {
                int idxb = 0
                int tt0 = nelem
                while tt0 > 1 { idxb = idxb + 1; tt0 = tt0 / 2 }   // within-table index bits
                sem.State b = _src_b()
                Vec(int) ctrl = _idx_interleave2(nelem, idxb)
                sem.State d = sem.exec_permute2(&a, &b, &ctrl, ew, idxb)
                Str lbl = "MASK (index register, runtime)"
                if mask_note.len() > 0 { lbl = f"MASK ({mask_note})" }
                Str mr = hv.h_index_mask_sel(lbl, &ctrl, ew, idxb, idxb)
                Str ops2 = _operands_perm2(ins, _is_i2(&mn))
                Str d2 = f"two-table permute: each index picks 1 of {nelem * 2} elements from src1++src2 (low {idxb} bits = element, bit {idxb} = table select: 0=src1, 1=src2). Example interleaves the two sources."
                return _two_src(_hdr(asml, meta, isa, summ, ops2, d2), &a, &b, mr, &d)
            }
            if r.cross {
                Vec(int) ctrl = _idx_rotate_cross(nelem)
                sem.State d = sem.exec_permute(&a, &ctrl, ew, true)
                Str lbl = "MASK (index register, runtime)"
                if mask_note.len() > 0 { lbl = f"MASK ({mask_note})" }
                int cb = 0
                int tt = nelem
                while tt > 1 { cb = cb + 1; tt = tt / 2 }   // index bits = log2(elements)
                Str mr = hv.h_index_mask_sel(lbl, &ctrl, ew, cb, 0 - 1)   // single-source (no select bit)
                return _src_mask_dst(_hdr(asml, meta, isa, summ, ops, "cross-lane: each element index selects any source element."), &a, mr, &d)
            }
            Vec(int) ctrl = _idx_rotate_lane(ew)
            sem.State d = sem.exec_permute(&a, &ctrl, ew, false)
            Str lbl2 = "MASK (index register, runtime)"
            if mask_note.len() > 0 { lbl2 = f"MASK ({mask_note})" }
            int le = 128 / ew
            int cb2 = 0
            int tt2 = le
            while tt2 > 1 { cb2 = cb2 + 1; tt2 = tt2 / 2 }   // in-lane: index bits = log2(per-lane elements)
            Str mr = hv.h_index_mask_sel(lbl2, &ctrl, ew, cb2, 0 - 1)   // single-source (no select bit)
            return _src_mask_dst(_hdr(asml, meta, isa, summ, ops, "in-lane: index is lane-relative; each 128-bit lane independent."), &a, mr, &d)
        }
        // compress / expand: the control is the write-mask k (defines which elements
        // are active). Bind a concrete k-mask, SHOW its bits, render SRC -> DST.
        int nelem2 = VL() / ew
        Vec(int) keep = _kmask_bits(ins, ew, nelem2, maskvals)
        Str mr = hv.h_kmask(_kmask_text(ins, ew, nelem2, maskvals, mask_note), &keep, ew)
        bool mem = _has_mem(ins)
        Str mbase = _mem_base(ins)
        // form-accurate summary (the SDM title is form-ambiguous — it says "Memory" even
        // for the register-destination form), derived from the actual destination/source.
        if _is_expand(&mn) {
            sem.State d = sem.exec_expand(&a, &keep, ew)
            if mem {
                int cnt = _active_elems(&keep).len()
                Vec(int) dense = _dense_indices(cnt)
                Vec(int) di = _ids(&d)
                Str fs = "Expand-load: read packed elements from memory and place them into the positions the mask selects (k1 bit 1) (inverse of compress-store); unselected positions keep their old value (merge {k1}) or are zeroed (the {z}/maskz form)"
                Str h = _hdr(asml, meta, isa, fs, ops, "memory is the SOURCE (packed, top->down); each mask-selected position (k1 bit 1) takes the next element in order.")
                h = h + hv.h_mem_vertical(f"MEMORY [{mbase}] (mem512 source, dense; one element/row, top=lowest addr)", mbase, &dense, ew)
                h = h + mr
                h = h + hv.h_lanes("DST (register, after scatter)", &di)
                return h + _close()
            }
            Str fs2 = "Expand: read the source's elements in order (element 0, 1, 2, ...) and place each at the next position the mask selects (k1 bit 1) (inverse of compress); unselected positions keep their old value (merge {k1}) or are zeroed (the {z}/maskz form)"
            return _src_mask_dst(_hdr(asml, meta, isa, fs2, ops, "register-to-register: each mask-selected position (k1 bit 1) takes the next packed element in order."), &a, mr, &d)
        }
        sem.State d = sem.exec_compress(&a, &keep, ew)
        if mem {
            Vec(int) active = _active_elems(&keep)
            Vec(int) si = _ids(&a)
            Str fs3 = "Compress-store: take the elements the mask selects (k1 bit is 1) and write them packed together into dense memory"
            Str h = _hdr(asml, meta, isa, fs3, ops, "destination is MEMORY (dense, top->down); each row is the actual bytes moved from the source register.")
            h = h + hv.h_lanes("SRC (register, before)", &si)
            h = h + mr
            h = h + hv.h_mem_vertical(f"MEMORY [{mbase}] (mem512 dest, dense; one element/row, top=lowest addr)", mbase, &active, ew)
            return h + _close()
        }
        Str fs4 = "Compress: take the elements the mask selects (those whose k1 bit is 1) and pack them together at the bottom of the destination (element 0, 1, 2, ...); the unfilled high elements keep dst's old value (merge {k1}) or are zeroed (the {z}/maskz form)"
        return _src_mask_dst(_hdr(asml, meta, isa, fs4, ops, "register-to-register: mask-selected elements (k1 bit 1) are packed into the low end; unselected source elements are dropped."), &a, mr, &d)
    }

    // ---- SHIFT (bit-level) ----
    if op >= sem.OP_SHIFT_VL() {
        if op <= sem.OP_SHIFT_IR() {
            bool left = false
            if op == sem.OP_SHIFT_VL() { left = true }
            if op == sem.OP_SHIFT_IL() { left = true }
            int by = 4
            if imm >= 0 { by = imm }
            bool rot = _is_rotate(&mn)
            bool arith = _is_arith_shift(&mn)
            // representative element value, filled to the real element width
            i64 emask = _elem_mask(ew)
            i64 base = 0x6A3F2B11C75E9D43 as i64
            i64 v = base & emask
            // arithmetic right shift: force the sign bit set so the sign-fill is visible
            // (the base value already has MSB=1 for ew=16/32, but not for ew=64).
            if arith {
                if !left {
                    i64 one = 1
                    v = v | (one << ((ew - 1) as i64))
                }
            }
            int sh = by
            if sh < 0 { sh = 0 }
            i64 after = v
            if rot {
                int rb = sh % ew
                if rb != 0 {
                    if left { after = ((v << (rb as i64)) | (v >> ((ew - rb) as i64))) & emask }
                    else { after = ((v >> (rb as i64)) | (v << ((ew - rb) as i64))) & emask }
                }
            } else {
                if left { after = (v << (sh as i64)) & emask }
                else {
                    if arith {
                        i64 fill = emask - _elem_mask(ew - sh)   // top sh bits set (sign fill)
                        after = ((v >> (sh as i64)) | fill) & emask
                    } else {
                        after = (v >> (sh as i64)) & emask
                    }
                }
            }
            Str dirw = "left"
            if !left { dirw = "right" }
            Str verb = "shift"
            if rot { verb = "rotate" }
            Str signnote = ""
            if arith {
                if !left { signnote = " (arithmetic: sign bit fills the vacated top bits)" }
            }
            Str srcw = "per-lane variable count (from a register)"
            if imm >= 0 { srcw = f"immediate count = {by}" }
            return _bitstep(_hdr(asml, meta, isa, summ, ops, f"{verb} {dirw}, {srcw}; every {ew}-bit lane in parallel.{signnote}"), v, after, ew)
        }
    }

    // ---- BROADCAST ----
    if op == sem.OP_BROADCAST() {
        sem.State d = sem.exec_broadcast(&a, ew)
        Str bdesc = f"replicate source element 0 across all {VL() / ew} lanes."
        if _is_mask_broadcast(&mn) {
            int mbits = ew    // mb2q -> 8 mask bits per qword; mw2d -> 16 per dword
            if ew == 64 { mbits = 8 }
            if ew == 32 { mbits = 16 }
            Str ename = "qword"
            if ew == 32 { ename = "dword" }
            bdesc = f"broadcast the low {mbits} bits of mask register k1 (zero-extended) into every {ew}-bit {ename} lane ({VL() / ew} lanes)."
        }
        return _src_dst(_hdr(asml, meta, isa, summ, ops, bdesc), &a, &d)
    }

    // ---- UNPACK ----
    if op == sem.OP_UNPACK_LO() {
        sem.State b = _src_b()
        sem.State d = sem.exec_unpack(&a, &b, ew, false)
        return _two_src(_hdr(asml, meta, isa, summ, ops, "interleave the LOW half of each 128-bit lane: a0,b0,a1,b1,..."), &a, &b, "", &d)
    }
    if op == sem.OP_UNPACK_HI() {
        sem.State b = _src_b()
        sem.State d = sem.exec_unpack(&a, &b, ew, true)
        return _two_src(_hdr(asml, meta, isa, summ, ops, "interleave the HIGH half of each 128-bit lane."), &a, &b, "", &d)
    }

    // ---- BLEND ----
    if op == sem.OP_BLEND() {
        sem.State b = _src_b()
        int nelem = VL() / ew
        Vec(int) sel = {}
        Str desc = ""
        Str mlab = ""
        if imm >= 0 {
            for e in 0..nelem { sel.push((imm >> (e % 8)) & 1) }
            desc = "immediate selector: bit e -> 1 picks B, 0 picks A."
            mlab = "blend mask from immediate (0=A, 1=B)"
        } else {
            sel = _kmask_bits(ins, ew, nelem, maskvals)
            desc = "mask-controlled (k-register); 0 picks A, 1 picks B."
            mlab = _kmask_text(ins, ew, nelem, maskvals, mask_note)
        }
        sem.State d = sem.exec_blend(&a, &b, &sel, ew)
        Str mr = hv.h_kmask(mlab, &sel, ew)
        return _two_src(_hdr(asml, meta, isa, summ, ops, desc), &a, &b, mr, &d)
    }

    // ---- ALIGN ----
    if op == sem.OP_ALIGN() {
        sem.State b = _src_b()
        int by = 4
        if imm >= 0 { by = imm }
        // vpalignr (ew=8): per-128-bit lane, shift by `by` BYTES, keep low 16.
        // valignd/valignq (ew=32/64): whole register, shift by `by` ELEMENTS.
        Str desc = ""
        if ew == 8 {
            sem.State d = sem.exec_align(&a, &b, by, 16)
            desc = f"per 128-bit lane: a:b (src1 high, src2 low), shift right {by} bytes, keep low 16 (dst byte 0 <- src2)."
            return _two_src(_hdr(asml, meta, isa, summ, ops, desc), &a, &b, "", &d)
        }
        int ebytes = ew / 8
        int byte_shift = by * ebytes
        sem.State d = sem.exec_align(&a, &b, byte_shift, 0)
        Str ename = "dword"
        if ew == 64 { ename = "qword" }
        desc = f"whole register: a:b (src1 high, src2 low), shift right {by} {ename}s ({byte_shift} bytes), keep low {VL() / ew} (dst elem 0 <- src2)."
        return _two_src(_hdr(asml, meta, isa, summ, ops, desc), &a, &b, "", &d)
    }

    // ---- LOGIC-OR ----
    if op == sem.OP_LOGIC_OR() {
        i64 em = _elem_mask(ew)
        i64 oa = (0x00FF00FF00FF00FF as i64) & em
        i64 ob_v = (0xFF00FF00FF00FF00 as i64) & em
        return _bitstep(_hdr(asml, meta, isa, summ, ops, "bitwise OR (disjoint byte lanes shown)."), oa, oa | ob_v, ew)
    }

    // ---- STORE / LOAD ----
    // A plain (unmasked) move copies memory <-> register verbatim, so the byte grid is pure
    // identity (no information). Only a WRITE-MASKED move ({k}) selects elements -> render the
    // grid just for that; plain load/store get the header + a one-line note.
    Str kmask = "{k"
    bool is_masked_move = asml.contains?(&kmask)
    if op == sem.OP_STORE() {
        if is_masked_move {
            Vec(int) sm = _kmask_bits(ins, 8, 64, maskvals)
            sem.State d = sem.exec_store(&a, &sm)
            Str mr = hv.h_kmask(_kmask_text(ins, 8, 64, maskvals, mask_note), &sm, 8)
            return _src_mask_dst(_hdr(asml, meta, isa, summ, ops, "masked store: only the bytes the mask selects (k1 bit is 1) are written; the rest are skipped."), &a, mr, &d)
        }
        return _hdr(asml, meta, isa, summ, ops, "store the register to memory verbatim (no shuffle/mask; every byte written).") + _close()
    }
    if op == sem.OP_LOAD() {
        Str ld = "load: copy memory into the register verbatim (no shuffle; all lanes live)."
        if is_masked_move { ld = "masked load: the mask (k1 bit 1) selects which elements are loaded; the rest keep their old value (or are zeroed in the {z} form)." }
        return _hdr(asml, meta, isa, summ, ops, ld) + _close()
    }

    // ---- vector -> mask (vpmov*2m): sign bit of each element into a k-register ----
    if _is_vec2mask(&mn) {
        Str ename = "byte"
        if ew == 16 { ename = "word" }
        if ew == 32 { ename = "dword" }
        if ew == 64 { ename = "qword" }
        Str vd = f"set mask bit i = the most-significant (sign) bit of source {ename} i; result is a {VL() / ew}-bit k-register."
        return _hdr(asml, meta, isa, summ, ops, vd) + _close()
    }

    // ---- down-convert pack ----
    int psrc = _pack_src_ew(&mn)
    if psrc > 0 {
        int pdst = _pack_dst_ew(&mn)
        sem.State d = _exec_pack(&a, psrc, pdst)
        Str kind = _pack_kind(&mn)
        Str pd = f"{kind} down-convert: each {psrc}-bit element -> {pdst}-bit (keep low {pdst} bits)."
        if kind.eq?("signed-saturating") { pd = f"signed-saturating down-convert: clamp each {psrc}-bit element to the signed {pdst}-bit range (grid shows element layout)." }
        if kind.eq?("unsigned-saturating") { pd = f"unsigned-saturating down-convert: clamp each {psrc}-bit element to the unsigned {pdst}-bit range (grid shows element layout)." }
        return _src_dst(_hdr(asml, meta, isa, summ, ops, pd), &a, &d)
    }

    // ---- everything else: summary + operand meanings only ----
    return _hdr(asml, meta, isa, summ, ops, "") + _close()
}

// ============================================================================
// synthesise a representative asm line for a mnemonic (the program, not a hint).
// ============================================================================
def _is_imm_shuffle(&Str mn) -> bool {
    if mn.eq?("vpshufd") { return true }
    if mn.eq?("vpermilps") { return true }
    if mn.eq?("vshufps") { return true }
    if mn.eq?("vpshufhw") { return true }
    if mn.eq?("vpshuflw") { return true }
    if mn.eq?("vpermq") { return true }
    if mn.eq?("vpermpd") { return true }
    if mn.eq?("vpermilpd") { return true }
    if mn.eq?("vshufpd") { return true }
    return false
}
def _is_imm_blend(&Str mn) -> bool {
    if mn.eq?("vpblendd") { return true }
    if mn.eq?("vpblendw") { return true }
    if mn.eq?("vblendps") { return true }
    if mn.eq?("vblendpd") { return true }
    return false
}
def _synth(&sem.SpecRow r) -> Str {
    Str mn = r.mn.copy()
    int op = r.op
    if op == sem.OP_PERMUTE() {
        if _is_imm_shuffle(&mn) { return f"{mn} zmm0, zmm1, 0x1B" }
        if r.ctrl_pos >= 0 { return f"{mn} zmm0, zmm1, zmm2" }
        return mn + " zmm0 {k1}, zmm1"          // compress/expand: write-mask is the control
    }
    if op == sem.OP_SHIFT_IL() { return f"{mn} zmm0, zmm1, 5" }
    if op == sem.OP_SHIFT_IR() { return f"{mn} zmm0, zmm1, 5" }
    if op == sem.OP_SHIFT_VL() { return f"{mn} zmm0, zmm1, zmm2" }
    if op == sem.OP_SHIFT_VR() { return f"{mn} zmm0, zmm1, zmm2" }
    if op == sem.OP_BROADCAST() {
        if _is_mask_broadcast(&mn) { return f"{mn} zmm0, k1" }   // source is a mask register
        return f"{mn} zmm0, xmm1"
    }
    if op == sem.OP_UNPACK_LO() { return f"{mn} zmm0, zmm1, zmm2" }
    if op == sem.OP_UNPACK_HI() { return f"{mn} zmm0, zmm1, zmm2" }
    if op == sem.OP_BLEND() {
        if _is_imm_blend(&mn) { return f"{mn} zmm0, zmm1, zmm2, 0xAA" }
        return f"{mn} zmm0, zmm1, zmm2"
    }
    if op == sem.OP_ALIGN() { return f"{mn} zmm0, zmm1, zmm2, 4" }
    if op == sem.OP_LOGIC_OR() { return f"{mn} zmm0, zmm1, zmm2" }
    if op == sem.OP_STORE() { return f"{mn} [rdi], zmm0" }
    if op == sem.OP_LOAD() { return f"{mn} zmm0, [rsi]" }
    Str pk = mn.copy()
    if _is_vec2mask(&pk) { return f"{mn} k0, zmm1" }   // vector -> mask: dest is a k-register
    int psw = _pack_src_ew(&pk)
    if psw > 0 {
        // dst register size = (VL/src_ew)*dst_ew bits; pick xmm/ymm accordingly.
        int dbits = (VL() / psw) * _pack_dst_ew(&pk)
        Str dreg = "zmm0"
        if dbits <= 128 { dreg = "xmm0" }
        else { if dbits <= 256 { dreg = "ymm0" } }
        return f"{mn} {dreg}, zmm1"
    }
    return f"{mn} zmm0, zmm1, zmm2"
}
// a SECOND form to also show (the memory variant of compress/expand), or "" if none.
def _synth_mem(&sem.SpecRow r) -> Str {
    Str mn = r.mn.copy()
    if r.op == sem.OP_PERMUTE() {
        if r.ctrl_pos >= 0 { return "" }
        if _is_imm_shuffle(&mn) { return "" }
        if _is_expand(&mn) { return mn + " zmm0 {k1}, [rsi]" }   // expand-load
        return mn + " [rdi] {k1}, zmm1"                          // compress-store
    }
    return ""
}

def _op_order() -> Vec(int) {
    Vec(int) o = {}
    o.push(0); o.push(5); o.push(6); o.push(7); o.push(8); o.push(9)
    o.push(1); o.push(2); o.push(3); o.push(4); o.push(10); o.push(11); o.push(12)
    o.push(26); o.push(20); o.push(21); o.push(22); o.push(23); o.push(24)
    o.push(25); o.push(27); o.push(28); o.push(29); o.push(30); o.push(31); o.push(99)
    return o
}

def all_instructions_asm() -> Str {
    Vec(sem.SpecRow) tbl = isa.build_table()
    Vec(int) order = _op_order()
    Str asm = ""
    int no = order.len()
    int nt = tbl.len()
    for oi in 0..no {
        int op = order.get!(oi)
        for i in 0..nt {
            &sem.SpecRow r = tbl.get_ref(i)
            if r.op == op {
                asm = asm + _synth(r) + "\n"
                Str ex = _synth_mem(r)
                if ex.len() > 0 { asm = asm + ex + "\n" }
            }
        }
    }
    return asm
}

// ============================================================================
// PLAIN-TEXT gallery (monospace, regview) — parallel to render_one's HTML, reusing the
// same shared data (SpecRow / exec_* / immediate decode). Header (meaning) for EVERY
// instruction; movement headline classes (permute / shift) also get a regview data view.
// ============================================================================
def _txt_asml(&ir.Inst ins) -> Str {
    Str a = ins.mnemonic.copy()
    int no = ins.ops.len()
    for k in 0..no {
        Str t = _op_text(ins, k)
        if k == 0 { a = f"{a} {t}" } else { a = f"{a}, {t}" }
    }
    return a
}
def _txt_header(&ir.Inst ins, &sem.SpecRow r, &Vec(specdoc.SpecDoc) sd, Str clsname) -> Str {
    Str mn = ins.mnemonic.copy()
    Str semc = r.sem.copy()
    Str etype = _elem_type(r.elem_bits, &semc)
    Str isa = _isa_of(&semc)
    Str summ = specdoc.summary_of(sd, &mn)
    Str ops = _operands(ins, r)
    Str h = f"{_txt_asml(ins)}\n"
    Str meta = f"    class={clsname}   data type: {etype}"
    if isa.len() > 0 { meta = f"{meta}   ISA: {isa}" }
    h = f"{h}{meta}\n"
    if summ.len() > 0 { h = f"{h}    summary:  {summ}\n" }
    if ops.len() > 0 { h = f"{h}    operands: {ops}\n" }
    return h
}
// element-level provenance derived from the COMPUTED dst state: for each dst element, which
// source element it came from (A = src1, B = src2 for two-source ops; .. = zero/dead). Shows
// what the register actually became — not a raw selector dump.
// one fixed-width provenance cell: "[tag<idx>]" with idx padded so all cells line up.
def _txt_prov_cell(Str tag, int val, int iw) -> Str {
    Str vs = f"{val}"
    return f"[{tag}{vs.pad_left(iw, 32)}]"
}
// SRC (element identity) and DST (<- source) as bracketed cells, ONE lane per line, mirroring
// the HTML SRC/DST lanes. Single-source DST shows the source element index; two-source shows
// A<idx>/B<idx> (A=src1, B=src2). All cells equal width so columns and lanes line up.
def _txt_elem_provenance(&sem.State d, int ew, bool twosrc) -> Str {
    Vec(int) dids = _ids(d)
    int ebytes = ew / 8
    if ebytes < 1 { ebytes = 1 }
    int nelem = dids.len() / ebytes
    int per_lane = 128 / ew
    if per_lane < 1 { per_lane = 1 }
    int lanes = nelem / per_lane
    int iw = 1
    if nelem - 1 >= 10 { iw = 2 }
    Str styp = " "                     // leading space where the A/B tag column sits
    if !twosrc { styp = "" }
    Str out = "    SRC (element identity):\n"
    int l = lanes - 1
    while l >= 0 {
        Str line = f"    L{l}  "
        int e = per_lane - 1
        while e >= 0 {
            int ei = l * per_lane + e
            line = f"{line}{_txt_prov_cell(styp, ei, iw)} "
            e = e - 1
        }
        out = f"{out}{line}\n"
        l = l - 1
    }
    Str dlbl = "    DST (dst <- source element):\n"
    if twosrc { dlbl = "    DST (dst <- source; A=src1, B=src2):\n" }
    out = f"{out}{dlbl}"
    l = lanes - 1
    while l >= 0 {
        Str line = f"    L{l}  "
        int e = per_lane - 1
        while e >= 0 {
            int ei = l * per_lane + e
            int prov = dids.get!(ei * ebytes)
            Str cell = ""
            if prov < 0 {
                Str dots = "".pad_left(iw, 46)
                cell = f"[{styp}{dots}]"
            } else {
                if twosrc {
                    if prov >= 64 { cell = _txt_prov_cell("B", (prov - 64) / ebytes, iw) }
                    else { cell = _txt_prov_cell("A", prov / ebytes, iw) }
                } else {
                    cell = _txt_prov_cell(styp, prov / ebytes, iw)
                }
            }
            line = f"{line}{cell} "
            e = e - 1
        }
        out = f"{out}{line}\n"
        l = l - 1
    }
    return out
}
def _txt_hex1(int v) -> Str {
    int d = v & 15
    if d < 10 { return f"{d}" }
    if d == 10 { return "a" }
    if d == 11 { return "b" }
    if d == 12 { return "c" }
    if d == 13 { return "d" }
    if d == 14 { return "e" }
    return "f"
}
// text mirror of htmlview.h_index_mask_sel: the index MASK as fixed-width nibble cells, one
// lane per line. `[x]` = a nibble (ignored high nibbles show 0); `(x)` = the nibble holding
// the table-select bit (two-source). byte/word stay compact: `[.]`=ignored, `(s)`=select,
// `[idx]`=decimal index (padded so all cells are equal width and the lanes line up).
def _txt_index_mask(Str title, &Vec(int) ctrl, int ew, int idx_bits, int sel_bit) -> Str {
    int n = ctrl.len()
    int per_lane = 128 / ew
    if per_lane < 1 { per_lane = 1 }
    int lanes = n / per_lane
    bool nibform = false
    if ew >= 32 { nibform = true }
    bool twosrc = false
    if sel_bit >= 0 { twosrc = true }
    int nnib = ew / 4
    int telems = 1
    int z = 0
    while z < idx_bits { telems = telems * 2; z = z + 1 }
    int iw = 1
    if telems - 1 >= 10 { iw = 2 }
    Str out = f"    {title}\n"
    int l = lanes - 1
    while l >= 0 {
        Str line = f"    L{l}  "
        int e = per_lane - 1
        while e >= 0 {
            int ei = l * per_lane + e
            int v = 0 - 1
            if ei < n { v = ctrl.get!(ei) }
            Str cell = ""
            if nibform {
                int nb = nnib - 1
                while nb >= 0 {
                    int lo = nb * 4
                    int hi = lo + 3
                    int nibval = 0
                    if v >= 0 { nibval = (v >> lo) & 15 }
                    bool isign = false
                    bool issel = false
                    if twosrc {
                        if sel_bit < lo { isign = true } else { if sel_bit <= hi { issel = true } }
                    } else {
                        if lo >= idx_bits { isign = true }
                    }
                    if v < 0 { cell = f"{cell}[.]" }
                    else {
                        if isign { cell = f"{cell}[0]" }
                        else { if issel { cell = f"{cell}({_txt_hex1(nibval)})" } else { cell = f"{cell}[{_txt_hex1(nibval)}]" } }
                    }
                    nb = nb - 1
                }
            } else {
                int within = 0
                int tbl = 0
                if v >= 0 { within = v & (telems - 1); if twosrc { tbl = (v >> idx_bits) & 1 } }
                int ign_count = ew - idx_bits
                if twosrc { ign_count = ew - 1 - sel_bit }
                if ign_count > 0 { cell = "[.]" }
                if twosrc { cell = f"{cell}({tbl})" }
                if v >= 0 {
                    Str iv = f"{within}"
                    cell = f"{cell}[{iv.pad_left(iw, 32)}]"
                } else {
                    Str dots = "".pad_left(iw, 46)
                    cell = f"{cell}[{dots}]"
                }
            }
            line = f"{line}{cell} "
            e = e - 1
        }
        out = f"{out}{line}\n"
        l = l - 1
    }
    return out
}
def render_one_text(&ir.Inst ins, &sem.SpecRow r, &Vec(specdoc.SpecDoc) sd, &Map(Str, int) maskvals, Str mask_note) -> Str {
    Str mn = ins.mnemonic.copy()
    int op = r.op
    int ew = r.elem_bits
    int imm = _imm_of(ins)
    Str clsname = sem.class_name(op)
    if op >= sem.OP_SHIFT_VL() {
        if op <= sem.OP_SHIFT_IR() {
            if _is_rotate(&mn) {
                clsname = "rotate-left"
                if op == sem.OP_SHIFT_VR() { clsname = "rotate-right" }
                if op == sem.OP_SHIFT_IR() { clsname = "rotate-right" }
            }
        }
    }
    Str h = _txt_header(ins, r, sd, clsname)
    sem.State a = _src_a()

    // ---- PERMUTE ----
    if op == sem.OP_PERMUTE() {
        int nelem = VL() / ew
        if imm >= 0 {
            if ew == 32 {
                if !r.cross {
                    Vec(int) ctrl = _imm_perm_ctrl(imm, ew)
                    sem.State d = sem.exec_permute(&a, &ctrl, ew, false)
                    Str mk = _txt_index_mask("MASK (index nibbles, high=0 ignored):", &ctrl, ew, 2, 0 - 1)
                    return f"{h}    in-lane dword shuffle (imm), lane-relative:\n{mk}{_txt_elem_provenance(&d, ew, false)}"
                }
            }
            if ew == 64 {
                if !r.cross {
                    if r.ctrl_pos >= 0 {
                        Vec(int) ctrl = _imm_perm_ctrl_pd(imm)
                        sem.State d = sem.exec_permute(&a, &ctrl, ew, false)
                        Str mk = _txt_index_mask("MASK (index nibbles, high=0 ignored):", &ctrl, ew, 1, 0 - 1)
                        return f"{h}    in-lane qword select (imm), 0=low/1=high of lane:\n{mk}{_txt_elem_provenance(&d, ew, false)}"
                    }
                }
            }
            return f"{h}    immediate-controlled shuffle (selectors omitted).\n"
        }
        if r.ctrl_pos >= 0 {
            if r.nsrc >= 2 {
                int idxb = 0
                int tt0 = nelem
                while tt0 > 1 { idxb = idxb + 1; tt0 = tt0 / 2 }
                sem.State b = _src_b()
                Vec(int) ctrl = _idx_interleave2(nelem, idxb)
                sem.State d = sem.exec_permute2(&a, &b, &ctrl, ew, idxb)
                Str mk = _txt_index_mask("MASK (index, []=nibble, ()=table-select):", &ctrl, ew, idxb, idxb)
                return f"{h}    two-table permute, interleave src1/src2 example:\n{mk}{_txt_elem_provenance(&d, ew, true)}"
            }
            if r.cross {
                Vec(int) ctrl = _idx_rotate_cross(nelem)
                sem.State d = sem.exec_permute(&a, &ctrl, ew, true)
                int cb = 0
                int tt = nelem
                while tt > 1 { cb = cb + 1; tt = tt / 2 }
                Str mk = _txt_index_mask("MASK (index nibbles, high=0 ignored):", &ctrl, ew, cb, 0 - 1)
                return f"{h}    cross-lane gather (example dst[i]=src[i-1]):\n{mk}{_txt_elem_provenance(&d, ew, false)}"
            }
            Vec(int) ctrl = _idx_rotate_lane(ew)
            sem.State d = sem.exec_permute(&a, &ctrl, ew, false)
            int le = 128 / ew
            int cb2 = 0
            int tt2 = le
            while tt2 > 1 { cb2 = cb2 + 1; tt2 = tt2 / 2 }
            Str mk2 = _txt_index_mask("MASK (index nibbles, high=0 ignored):", &ctrl, ew, cb2, 0 - 1)
            return f"{h}    in-lane shuffle (lane-relative example):\n{mk2}{_txt_elem_provenance(&d, ew, false)}"
        }
        return f"{h}"
    }

    // ---- SHIFT / ROTATE: bit-level before -> after on one representative element ----
    if op >= sem.OP_SHIFT_VL() {
        if op <= sem.OP_SHIFT_IR() {
            bool left = false
            if op == sem.OP_SHIFT_VL() { left = true }
            if op == sem.OP_SHIFT_IL() { left = true }
            int by = 4
            if imm >= 0 { by = imm }
            bool rot = _is_rotate(&mn)
            bool arith = _is_arith_shift(&mn)
            i64 emask = _elem_mask(ew)
            i64 base = 0x6A3F2B11C75E9D43 as i64
            i64 v = base & emask
            if arith { if !left { i64 one = 1; v = v | (one << ((ew - 1) as i64)) } }
            int sh = by
            if sh < 0 { sh = 0 }
            i64 after = v
            if rot {
                int rb = sh % ew
                if rb != 0 {
                    if left { after = ((v << (rb as i64)) | (v >> ((ew - rb) as i64))) & emask }
                    else { after = ((v >> (rb as i64)) | (v << ((ew - rb) as i64))) & emask }
                }
            } else {
                if left { after = (v << (sh as i64)) & emask }
                else {
                    if arith { i64 fill = emask - _elem_mask(ew - sh); after = ((v >> (sh as i64)) | fill) & emask }
                    else { after = (v >> (sh as i64)) & emask }
                }
            }
            Str t = f"{clsname} {ew}-bit element (count {by})"
            return f"{h}{rv.bit_xform(t, v, after, ew, 8)}"
        }
    }

    // ---- BROADCAST ----
    if op == sem.OP_BROADCAST() {
        sem.State d = sem.exec_broadcast(&a, ew)
        return f"{h}    replicate src element 0 across all lanes:\n{_txt_elem_provenance(&d, ew, false)}"
    }

    // ---- UNPACK ----
    if op == sem.OP_UNPACK_LO() {
        sem.State b = _src_b()
        sem.State d = sem.exec_unpack(&a, &b, ew, false)
        return f"{h}    interleave LOW half of each 128-bit lane (a0,b0,a1,b1,...):\n{_txt_elem_provenance(&d, ew, true)}"
    }
    if op == sem.OP_UNPACK_HI() {
        sem.State b = _src_b()
        sem.State d = sem.exec_unpack(&a, &b, ew, true)
        return f"{h}    interleave HIGH half of each 128-bit lane:\n{_txt_elem_provenance(&d, ew, true)}"
    }

    // ---- BLEND (alternating A/B example) ----
    if op == sem.OP_BLEND() {
        sem.State b = _src_b()
        int nelem = VL() / ew
        Vec(int) sel = _sel_alt(nelem)
        sem.State d = sem.exec_blend(&a, &b, &sel, ew)
        return f"{h}    per-element select (example mask 0=A,1=B alternating):\n{_txt_elem_provenance(&d, ew, true)}"
    }

    // ---- ALIGN ----
    if op == sem.OP_ALIGN() {
        sem.State b = _src_b()
        int by = 4
        if imm >= 0 { by = imm }
        Str note = f"    a:b (src1 high, src2 low), shift right (count {by}); dst elem 0 <- src2:\n"
        if ew == 8 {
            sem.State d = sem.exec_align(&a, &b, by, 16)
            return f"{h}{note}{_txt_elem_provenance(&d, ew, true)}"
        }
        int ebytes = ew / 8
        sem.State d2 = sem.exec_align(&a, &b, by * ebytes, 0)
        return f"{h}{note}{_txt_elem_provenance(&d2, ew, true)}"
    }

    // everything else (logic-or / store / load / convert / compute / crypto): the header
    // (summary + operands) carries the meaning, matching the HTML summary-only side.
    return f"{h}"
}

def build_gallery_text() -> Str {
    Str asm = all_instructions_asm()
    Vec(ir.Inst) prog = decode.parse_listing(&asm, 0x400 as i64)
    Vec(sem.SpecRow) tbl = isa.build_table()
    Vec(specdoc.SpecDoc) sd = specdoc.build_specdoc_table()
    Map(Str, int) maskvals = {}
    Str out = "FULL-ISA INSTRUCTION GALLERY (plain text)\n"
    out = f"{out}{rv.legend()}\n\n"
    int n = prog.len()
    int prev_op = 0 - 999
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        Str mn = ins.mnemonic.copy()
        sem.SpecRow r = sem.lookup(&tbl, &mn)
        if r.op != prev_op {
            Str cn = sem.class_name(r.op)
            out = f"{out}========================================================================\n"
            out = f"{out}== {cn}\n"
            out = f"{out}========================================================================\n"
            prev_op = r.op
        }
        Str mnote = _gallery_mask_note(ins, &r)
        out = f"{out}{render_one_text(ins, &r, &sd, &maskvals, mnote)}\n"
    }
    return out
}

// ============================================================================
def build_gallery() -> Str {
    Str asm = all_instructions_asm()
    Vec(ir.Inst) prog = decode.parse_listing(&asm, 0x400 as i64)
    Vec(sem.SpecRow) tbl = isa.build_table()
    Vec(specdoc.SpecDoc) sd = specdoc.build_specdoc_table()

    Map(Str, int) maskvals = {}   // empty: gallery instrs are isolated -> example masks by element width
    Str nav = "  <div class='navbar'><div class='navtitle'>jump to op-class</div>\n"
    Str content = "  <div class='content'>\n  <h2>Full-ISA instruction gallery</h2>\n"
    int n = prog.len()
    int prev_op = 0 - 999
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        Str mn = ins.mnemonic.copy()
        sem.SpecRow r = sem.lookup(&tbl, &mn)
        if r.op != prev_op {
            Str cn = sem.class_name(r.op)
            Str kind = "byte / bit level SRC -> DST"
            if r.op >= 20 { kind = "summary + operands" }
            if r.op == 26 { kind = "pack -> grid; others -> summary" }
            Str id = f"sec{r.op}"
            nav = nav + f"    <a href='#{id}'>{cn}</a>\n"
            content = content + hv.h_banner_id(f"{cn}  ({kind})", id)
            prev_op = r.op
        }
        Str mnote = _gallery_mask_note(ins, &r)
        content = content + render_one(ins, &r, &sd, &maskvals, mnote)
    }
    nav = nav + "  </div>\n"
    content = content + "  </div>\n"
    return hv.page("", nav + content)
}
