// std.sim.core.ir — architecture-neutral IR for the instruction-level simulator.
//
// The decode/model backends (std.sim.intel) fill these; the engine, advisor and
// renderer only ever see this neutral shell (so the ARM backend is cheap later).
// See docs/plan_std_sim.md §4.1.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import std.text.strconv as strconv

// ---- ISA class tags (advisor isa-gating + reporting) -------------------------
// Kept as small ints so Inst stays POD-ish in spirit; names via isa_name().
//  0 BASE   1 SSE   2 AVX   3 AVX2   4 AVX512   5 BMI2
//  6 GFNI   7 VBMI  8 VBMI2 9 VNNI  10 BF16    11 FP16
def isa_name(int c) -> Str {
    if c == 1 { return "SSE" }
    if c == 2 { return "AVX" }
    if c == 3 { return "AVX2" }
    if c == 4 { return "AVX512" }
    if c == 5 { return "BMI2" }
    if c == 6 { return "GFNI" }
    if c == 7 { return "VBMI" }
    if c == 8 { return "VBMI2" }
    if c == 9 { return "VNNI" }
    if c == 10 { return "BF16" }
    if c == 11 { return "FP16" }
    return "BASE"
}

// ---- Operand -----------------------------------------------------------------
// kind: 0 reg, 1 mem, 2 imm
struct Operand {
    int  kind
    Str  text          // "zmm0" / "[rax+8]" / "0x1ff"
    bool is_read
    bool is_write
}

def reg_r(Str name)  -> Operand { return Operand { kind: 0, text: name, is_read: true,  is_write: false } }
def reg_w(Str name)  -> Operand { return Operand { kind: 0, text: name, is_read: false, is_write: true  } }
def reg_rw(Str name) -> Operand { return Operand { kind: 0, text: name, is_read: true,  is_write: true  } }
def mem_r(Str t)     -> Operand { return Operand { kind: 1, text: t,    is_read: true,  is_write: false } }
def mem_w(Str t)     -> Operand { return Operand { kind: 1, text: t,    is_read: false, is_write: true  } }
def imm(Str t)       -> Operand { return Operand { kind: 2, text: t,    is_read: true,  is_write: false } }

def operand_kind_name(int k) -> Str {
    if k == 1 { return "mem" }
    if k == 2 { return "imm" }
    return "reg"
}

// ---- Inst (one decoded instruction) -----------------------------------------
struct Inst {
    i64 addr
    int length         // bytes (x86 variable length; frontend 16B-window model needs it)
    Str mnemonic
    Vec(Operand) ops
    int isa_class
}

def inst(i64 addr, int length, Str mnemonic, Vec(Operand) ops, int isa) -> Inst {
    return Inst { addr: addr, length: length, mnemonic: mnemonic, ops: ops, isa_class: isa }
}

// ---- Uop (the unit the engine actually schedules) ---------------------------
struct Uop {
    int      inst_id      // which Inst it belongs to
    Vec(int) port_mask    // ports it may issue to, e.g. [0,1,5]
    int      latency      // result latency in cycles
    Vec(int) src_uops     // data deps: producer uop ids
    bool     is_load
    bool     is_store
    bool     is_fused     // micro-fusion domain marker
}

def uop(int inst_id, Vec(int) ports, int latency, Vec(int) srcs, bool ld, bool st) -> Uop {
    return Uop { inst_id: inst_id, port_mask: ports, latency: latency,
                 src_uops: srcs, is_load: ld, is_store: st, is_fused: false }
}

// ---- Carried (one loop-carried recurrence edge) -----------------------------
// In the replicated steady-state stream, iteration t's `reader` uop consumes
// iteration (t-1)'s `writer` uop (a register read whose value comes from the
// previous iteration). Both ids are uop POSITIONS within ONE body. distance is
// always 1 (consumed exactly one iteration later) — the common reduction/scan
// shape. This is what lets the engine compute RecMII (recurrence-bound II) and
// engine-2 reproduce the actual steady-state cost, not just the port bound.
struct Carried {
    int reader      // uop that reads the carried register (this iteration)
    int writer      // uop that produced it (previous iteration's final writer)
}
def carried(int reader, int writer) -> Carried { return Carried { reader: reader, writer: writer } }

// ---- Port (execution resource) ----------------------------------------------
struct Port {
    int id
    Str label          // "p0 ALU/FMA", "p23 Load", ...
}
def port(int id, Str label) -> Port { return Port { id: id, label: label } }

// ---- UopTrace (engine-2 product: per-stage cycle stamps -> Gantt) -----------
// Cycle counts are plain int (a kernel never runs billions of cycles); addr stays
// i64 in Inst. This keeps engine-2 arithmetic free of i64-literal friction.
struct UopTrace {
    int uop_id
    int cycle_ready      // all source results available
    int cycle_issued     // dispatched to a port
    int cycle_done       // result written back (issued + latency)
    int port_used
}

def uoptrace(int uop_id, int ready, int issued, int done, int port) -> UopTrace {
    return UopTrace { uop_id: uop_id, cycle_ready: ready, cycle_issued: issued,
                      cycle_done: done, port_used: port }
}

// =============================================================================
// Instruction text view (the "instruction" half of step-1 visualization).
// One line per inst:  addr: mnemonic  op1, op2, ...   [len=N isa=AVX512]
// =============================================================================

def _operand_str(&Operand o) -> Str {
    // read/write annotation is small but useful for the dep view later
    if o.is_write {
        if o.is_read { return f"{o.text}(rw)" }
        return f"{o.text}(w)"
    }
    return o.text
}

def _ops_str(&Vec(Operand) ops) -> Str {
    Str s = ""
    int n = ops.len()
    for i in 0..n {
        Str piece = _operand_str(ops.get_ref(i))
        if i == 0 { s = piece }
        else { s = f"{s}, {piece}" }
    }
    return s
}

def inst_line(&Inst ins) -> Str {
    Str ah = strconv.int_to_hex(ins.addr as int)
    Str mn = ins.mnemonic.pad_right(12, 32)         // ' '
    Str ops = _ops_str(&ins.ops)
    Str body = f"{mn}{ops}".pad_right(40, 32)
    Str isa = isa_name(ins.isa_class)
    return f"  {ah.pad_left(6, 32)}: {body}  [len={ins.length} isa={isa}]"
}

def dump_insts(&Vec(Inst) prog) -> Str {
    Str out = "instruction listing:\n"
    int n = prog.len()
    for i in 0..n {
        Str line = inst_line(prog.get_ref(i))
        out = f"{out}{line}\n"
    }
    return out
}
