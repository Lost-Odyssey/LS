// sim.intel.decode — instruction-listing front-end (plan §5.3, zero-FFI fallback).
//
// The real decoder (V1+) wraps Zydis/XED over LS's C FFI to turn machine-code
// bytes into ir.Inst. Until then this accepts a hand-written **annotated listing**
// (one instruction per line, Intel syntax) so the whole pipeline is usable from
// text:  decode -> ports.build_uops -> engine.analyze -> patterns.advise -> render.
//
// Line format:   mnemonic [op1[, op2, ...]]      (Intel: destination first)
//   * operand starting with '['        -> memory  (first operand = store/write)
//   * operand starting with a digit/'-' -> immediate
//   * otherwise                         -> register
//   * the FIRST operand is the written destination; the rest are read sources
//     (true for VEX/EVEX 3-operand forms — the common AVX-512 kernel shape).
//   * blank lines and ';' '#' '//' comments are skipped.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import std.sys.io as io
import sim.core.ir as ir
import sim.intel.intrinsics as intr

// classify one operand token; is_first => it is the destination (written).
// A memory operand is any token containing '[' — both the bare `[rax+8]` form and
// the size-prefixed `zmmword ptr [r8 + rcx]` form clang emits (the '[' is no longer
// at position 0, so we scan for it). Memory operands drive the load/store-fusion uop
// synthesis in ports.build_uops.
def _classify(&Str tok, bool is_first) -> ir.Operand {
    int c0 = 0
    if tok.len() > 0 { c0 = tok.byte_at(0) }
    Str lbrack = "["
    if tok.find(&lbrack) >= 0 {          // '[' anywhere -> memory operand
        if is_first { return ir.mem_w(tok.copy()) }
        return ir.mem_r(tok.copy())
    }
    if c0 == 45 { return ir.imm(tok.copy()) }              // '-' immediate
    if c0 >= 48 { if c0 <= 57 { return ir.imm(tok.copy()) } }  // '0'..'9' immediate
    if is_first { return ir.reg_w(tok.copy()) }
    return ir.reg_r(tok.copy())
}

// control-transfer mnemonics (jmp/jcc/call/ret/loop): their operand is a branch TARGET
// (a label or indirect register), never a written data destination. The steady-state
// engine models the branch as a port-6 uop and does NOT follow the target — so the
// target is classified as an immediate-like token, kept out of the register dependency
// graph (otherwise `jne .LBB0_1` would mark `.LBB0_1` as a phantom register write).
def _is_control_flow(&Str mn) -> bool {
    if mn.len() == 0 { return false }
    if mn.byte_at(0) == 106 { return true }   // 'j' — every x86 jump
    if mn.eq?("call") { return true }
    if mn.eq?("ret") { return true }
    if mn.eq?("loop") { return true }
    return false
}

// accumulating ops read AND write their destination (3-operand FMA / VNNI dot accumulate):
// `vfmadd231ps zmm0, zmm1, zmm2` is zmm0 += zmm1*zmm2, so zmm0 is the loop-carried
// accumulator. The bare 3-operand AVX form marks dest write-only; these mnemonics
// override that to read-write so the recurrence (RecMII) layer sees the carried read.
def _is_accumulating(&Str mn) -> bool {
    if mn.starts_with?("vfmadd") { return true }
    if mn.starts_with?("vfmsub") { return true }
    if mn.starts_with?("vfnmadd") { return true }
    if mn.starts_with?("vfnmsub") { return true }
    if mn.starts_with?("vpdpbusd") { return true }   // VNNI int8 dot-accumulate
    if mn.starts_with?("vpdpwssd") { return true }   // VNNI int16 dot-accumulate
    return false
}

// coarse ISA-class inference from the mnemonic (advisor uses it for isa-gating).
// Fine-grained feature flags are what the catalog gates on; this is a default.
def infer_isa(&Str mn) -> int {
    if mn.eq?("vpermb") { return 7 }            // VBMI
    if mn.eq?("vpermi2b") { return 7 }
    if mn.eq?("vpermt2b") { return 7 }
    if mn.eq?("vpmultishiftqb") { return 7 }
    if mn.eq?("vpcompressb") { return 8 }       // VBMI2
    if mn.eq?("vpcompressd") { return 4 }
    if mn.eq?("vpshldvd") { return 8 }
    if mn.eq?("vpshrdvd") { return 8 }
    if mn.eq?("vpdpbusd") { return 9 }          // VNNI
    if mn.eq?("vpdpwssd") { return 9 }
    if mn.eq?("vgf2p8mulb") { return 6 }        // GFNI
    if mn.eq?("vgf2p8affineqb") { return 6 }
    if mn.eq?("pext") { return 5 }              // BMI2
    if mn.eq?("pdep") { return 5 }
    // generic vector op (mnemonic starts with 'v') -> AVX512 default
    int c0 = 0
    if mn.len() > 0 { c0 = mn.byte_at(0) }
    if c0 == 118 { return 4 }                   // 'v'
    return 0                                     // BASE / scalar
}

def _is_comment_or_blank(&Str line) -> bool {
    if line.len() == 0 { return true }
    int c0 = line.byte_at(0)
    if c0 == 59 { return true }      // ';'
    if c0 == 35 { return true }      // '#'
    if c0 == 47 { return true }      // '/'  (// comment)
    if c0 == 46 { return true }      // '.'  assembler directive (.text/.globl/.intel_syntax/.LBB…)
    return false
}

// index of the first whitespace byte (space or tab), or -1 if none.
def _first_ws(&Str line) -> int {
    int n = line.len()
    int i = 0
    while i < n {
        int c = line.byte_at(i)
        if c == 32 { return i }      // space
        if c == 9 { return i }       // tab
        i = i + 1
    }
    return 0 - 1
}

// a label line: the first whitespace-delimited token ends with ':' (e.g. `saxpy:`,
// `main:`, `.LBB0_8:`). Real .s files interleave labels with instructions; we drop
// them (control-flow targets aren't modeled by the steady-state engine).
def _is_label(&Str line) -> bool {
    int n = line.len()
    int i = 0
    while i < n {
        int c = line.byte_at(i)
        if c == 32 { return false }      // space before any ':' -> not a bare label
        if c == 9 { return false }       // tab
        if c == 58 { return true }       // ':' ends the first token -> label
        i = i + 1
    }
    return false
}

// strip a trailing assembler comment (`# …` GAS-style, or `// …`) from one line; the
// instruction text precedes it. Intel-syntax operands never contain '#', so cutting at
// the first '#' is safe; '//' is checked as a two-byte sequence.
def _strip_comment(&Str line) -> Str {
    int n = line.len()
    int i = 0
    while i < n {
        int c = line.byte_at(i)
        if c == 35 { return line.substr(0, i) }                       // '#'
        if c == 47 { if i + 1 < n { if line.byte_at(i + 1) == 47 { return line.substr(0, i) } } }  // '//'
        i = i + 1
    }
    return line.copy()
}

// parse a multi-line listing into a Vec(ir.Inst). addresses are synthesized
// from base_addr with a uniform 6-byte stride (length is informational for the
// frontend model; precise lengths require the real decoder).
def parse_listing(&Str text, i64 base_addr) -> Vec(ir.Inst) {
    Vec(ir.Inst) prog = {}
    Vec(Str) lns = text.lines()
    Str comma = ","
    // llvm-mca-style region markers: when the listing contains `# LLVM-MCA-BEGIN`, only
    // the instructions between BEGIN and END are analyzed (the loop body), so a whole
    // function file (setup + loop + epilogue) can be fed and scoped to the steady-state
    // region — without markers, the entire file is one block (current behavior).
    Str begin_tag = "LLVM-MCA-BEGIN"
    Str end_tag = "LLVM-MCA-END"
    bool region_mode = text.contains?(&begin_tag)
    bool in_region = true
    if region_mode { in_region = false }
    i64 addr = base_addr
    int n = lns.len()
    for li in 0..n {
        &Str raw = lns.get_ref(li)
        Str trimmed = raw.trim()
        // region markers are comments — check before stripping comments would erase them.
        if region_mode {
            if trimmed.contains?(&begin_tag) { in_region = true; continue }
            if trimmed.contains?(&end_tag) { in_region = false; continue }
            if !in_region { continue }
        }
        Str decommented = _strip_comment(&trimmed)   // drop trailing # / // comment
        Str line = decommented.trim()
        if _is_comment_or_blank(&line) { continue }
        if _is_label(&line) { continue }             // skip `foo:` label lines

        // split mnemonic from operand list at the first WHITESPACE (space OR tab —
        // clang separates the mnemonic from operands with a tab, hand-written listings
        // with a space; both must work).
        int sp = _first_ws(&line)
        Str mn = ""
        Vec(ir.Operand) ops = {}
        if sp < 0 {
            mn = line.copy()
        } else {
            mn = line.substr(0, sp)
            Str rawrest = line.substr(sp + 1, line.len() - sp - 1)
            Str rest = rawrest.trim()
            Vec(Str) toks = rest.split(&comma)
            int m = toks.len()
            bool accum = _is_accumulating(&mn)
            bool cf = _is_control_flow(&mn)
            for j in 0..m {
                &Str rawtok = toks.get_ref(j)
                Str tok = rawtok.trim()
                if tok.len() > 0 {
                    if cf {
                        // branch/call target: keep out of the register dependency graph.
                        ops.push(ir.imm(tok.copy()))
                    } else {
                        ir.Operand o = _classify(&tok, j == 0)
                        // accumulating op: promote register destination to read-write so
                        // the loop-carried accumulator read is visible to the engine.
                        if accum { if j == 0 { if o.kind == 0 { o.is_read = true } } }
                        ops.push(o)
                    }
                }
            }
        }
        int isa = infer_isa(&mn)
        prog.push(ir.inst(addr, 6, mn, ops, isa))
        addr = addr + (6 as i64)
    }
    return prog
}

// =============================================================================
// INTRINSIC FRONT-END — accept a C-intrinsic kernel listing and lower it to the
// Intel-syntax form parse_listing already understands, so render/full_report can be
// fed intrinsics instead of assembly. The intrinsic -> mnemonic map comes from the
// SDM extraction (sim.intel.intrinsics, generated). One intrinsic per line; the call
// may be bare (`_mm512_storeu_si512(p, v)`) or assigned (`r = _mm512_add_epi32(a,b)`
// / `__m512i r = ...`). Lines without `_mm` (asm, labels, comments) pass through
// unchanged, so mixed listings work.
//
// v1 fidelity: unmasked forms lower exactly (args become source operands in order);
// `store`/`load` intrinsics get the memory operand right; masked/maskz forms resolve
// the mnemonic + ports + timing correctly but their extra mask/merge args are kept as
// plain reads (the control-operand position is the Intel asm order, so a masked
// shuffle's movement grid is approximate). Nested intrinsic calls are not supported.
// =============================================================================

// end (exclusive) of the identifier [A-Za-z0-9_] run starting at `start`.
def _ident_end(&Str s, int start) -> int {
    int n = s.len()
    int i = start
    while i < n {
        int c = s.byte_at(i)
        bool ok = false
        if c >= 48 { if c <= 57 { ok = true } }    // 0-9
        if c >= 65 { if c <= 90 { ok = true } }    // A-Z
        if c >= 97 { if c <= 122 { ok = true } }   // a-z
        if c == 95 { ok = true }                   // _
        if !ok { return i }
        i = i + 1
    }
    return n
}

// last whitespace-delimited token of a trimmed string (the variable name in an
// assignment LHS like `__m512i r` -> `r`).
def _last_token(&Str s) -> Str {
    int n = s.len()
    int last = 0
    int i = 0
    while i < n {
        int c = s.byte_at(i)
        if c == 32 { last = i + 1 }   // space
        if c == 9 { last = i + 1 }    // tab
        i = i + 1
    }
    return s.substr(last, n - last)
}

// lower an intrinsic listing to an Intel-syntax assembly listing (string).
def lower_intrinsics(&Str src) -> Str {
    Vec(IntrinRow) tbl = intr.build_intrinsic_table()
    Vec(Str) lns = src.lines()
    Str out = ""
    Str mm = "_mm"
    Str lp = "("
    Str rp = ")"
    Str eq = "="
    Str comma = ","
    Str store_tag = "store"
    Str load_tag = "load"
    int vctr = 0
    int n = lns.len()
    for li in 0..n {
        &Str raw = lns.get_ref(li)
        Str line = raw.copy()
        Str t = line.trim()
        int mmpos = t.find(&mm)
        if mmpos < 0 { out = out + line + "\n"; continue }      // pass-through

        int nend = _ident_end(&t, mmpos)
        Str name = t.substr(mmpos, nend - mmpos)
        Str mnem = intr.mnemonic_of(&tbl, &name)
        if mnem.len() == 0 { out = out + line + "\n"; continue } // unknown -> as-is

        // locate the argument list: first '(' at/after nend, first ')' after it.
        Str tail = t.substr(nend, t.len() - nend)
        int lrel = tail.find(&lp)
        if lrel < 0 { out = out + line + "\n"; continue }
        int lpar = nend + lrel
        Str inside = t.substr(lpar + 1, t.len() - lpar - 1)
        int rrel = inside.find(&rp)
        if rrel < 0 { out = out + line + "\n"; continue }
        Str argstr = inside.substr(0, rrel)
        Vec(Str) args = argstr.split(&comma)
        int m = args.len()

        // destination: LHS variable before '=' (if it precedes the call), else synth.
        Str dst = ""
        int eqpos = t.find(&eq)
        if eqpos >= 0 { if eqpos < mmpos {
            Str lhs = t.substr(0, eqpos)
            Str lt = lhs.trim()
            dst = _last_token(&lt)
        } }

        bool is_store = name.contains?(&store_tag)
        bool is_load = name.contains?(&load_tag)
        Str asml = ""
        if is_store {
            // store(mem, val[, ...]) -> `mnem [mem], val, ...`
            Str mem = ""
            if m > 0 { &Str a0 = args.get_ref(0); mem = a0.trim() }
            asml = f"{mnem} [{mem}]"
            for j in 1..m { &Str a = args.get_ref(j); Str at = a.trim(); if at.len() > 0 { asml = asml + ", " + at } }
        } else {
            if dst.len() == 0 { dst = f"v{vctr}"; vctr = vctr + 1 }
            asml = f"{mnem} {dst}"
            for j in 0..m {
                &Str a = args.get_ref(j)
                Str at = a.trim()
                if at.len() > 0 {
                    if is_load { if j == 0 { at = f"[{at}]" } }   // load(mem) source is memory
                    asml = asml + ", " + at
                }
            }
        }
        out = out + asml + "\n"
    }
    return out
}

// parse an intrinsic listing straight into ir.Inst (lower + reuse parse_listing).
def parse_intrinsic_listing(&Str src, i64 base_addr) -> Vec(ir.Inst) {
    Str asm = lower_intrinsics(src)
    return parse_listing(&asm, base_addr)
}

// read an Intel-syntax assembly file (`xx.s`) and parse it line-by-line into a
// Vec(ir.Inst). Assembler directives (`.text`/`.globl`/`.intel_syntax`/`.LBB…`),
// labels (`foo:`), and trailing `#`/`//` comments are skipped by parse_listing, so a
// whole clang `-S -masm=intel` listing can be fed directly. AT&T syntax (`%`/`$`,
// reversed operand order) is NOT supported — emit the file with `-masm=intel`.
// Returns Err with a message if the file cannot be read.
def parse_file(Str path, i64 base_addr) -> Result(Vec(ir.Inst), Str) {
    Result(Str, Str) r = io.read_file(path)
    match r {
        Ok(text) => {
            Vec(ir.Inst) prog = parse_listing(&text, base_addr)
            return Ok(prog)
        }
        Err(e) => { return Err(e) }
    }
}

// ---- convenience wrappers: default the placement address (0x400) ------------
// `base_addr` only seeds the synthetic instruction layout for the front-end fetch-window
// model; the value almost never changes the analysis. These let callers skip it (and skip
// the `0x400 as i64` literal). Use the base-taking forms above only to model a specific
// code address. (LS default-param values must be literals, and there is no i64 literal —
// hence a wrapper rather than `base_addr = 0x400`.)
def DEFAULT_BASE() -> i64 { return 0x400 as i64 }
def parse(Str path) -> Result(Vec(ir.Inst), Str) { return parse_file(path, DEFAULT_BASE()) }
def parse_text(&Str asm) -> Vec(ir.Inst) { return parse_listing(asm, DEFAULT_BASE()) }
