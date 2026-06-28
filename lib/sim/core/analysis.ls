// sim.core.analysis — shared dependency-graph + data-flow analysis (plan §4.3).
//
// Computed once, consumed by BOTH the advisor (provenance lets it tell a constant
// twiddle operand from a dynamic one — §6.6d) and the visualizers (dep graph,
// value-source colouring). This is the single source of truth that lets the advisor
// give advice an opcode-level linter cannot.
//
// MVP scope: formalize uop deps into a DepGraph (with edge latency), recompute the
// critical path on it (cross-checks engine-1), and classify operand provenance
// (Const / LoopInvariant / LoopVariant / Unknown). Loop-carried distance δ and
// liveness intervals are stubbed for the engine-2 / loop work.
//
// Pure LS, zero compiler changes (same discipline as lib/oran).

import std.core.vec
import std.core.str
import sim.core.ir as ir

// ---- dependency edge / graph -------------------------------------------------
// kind: 0 RAW data, 1 memory, 2 structural (port/buffer)
struct DepEdge {
    int from_uop       // producer
    int to_uop         // consumer
    int latency        // edge weight = producer result latency
    int distance       // δ: 0 = same iteration; >=1 = loop-carried (iter N -> N+δ)
    int kind
}

def dep_raw(int f, int t, int lat) -> DepEdge {
    return DepEdge { from_uop: f, to_uop: t, latency: lat, distance: 0, kind: 0 }
}

struct DepGraph {
    Vec(DepEdge) edges
    Vec(int) node_lat  // per-uop result latency (for critical-path DP)
    int nuops
}

// formalize ir.Uop.src_uops into explicit RAW edges (δ=0 for a straight-line body)
def build_depgraph(&Vec(ir.Uop) uops) -> DepGraph {
    Vec(DepEdge) edges = {}
    Vec(int) lat = {}
    int n = uops.len()
    for i in 0..n {
        &ir.Uop u = uops.get_ref(i)
        lat.push(u.latency)
        int m = u.src_uops.len()
        for j in 0..m {
            int s = u.src_uops.get!(j)
            int plat = uops.get_ref(s).latency   // POD field on get_ref -> value copy
            edges.push(dep_raw(s, i, plat))
        }
    }
    return DepGraph { edges: edges, node_lat: lat, nuops: n }
}

// longest-latency path on the graph (uops in program/topological order).
// Should equal engine-1's critical_path — a cross-check of the two derivations.
def crit_path(&DepGraph g) -> int {
    int n = g.nuops
    Vec(int) cp = {}
    for i in 0..n { cp.push(0) }
    int best = 0
    int e = g.edges.len()
    for i in 0..n {
        int base = 0
        for k in 0..e {
            &DepEdge ed = g.edges.get_ref(k)
            if ed.to_uop == i {
                int cf = cp.get!(ed.from_uop)
                if cf > base { base = cf }
            }
        }
        int val = base + g.node_lat.get!(i)
        cp.set(i, val)
        if val > best { best = val }
    }
    return best
}

def in_degree(&DepGraph g, int uop) -> int {
    int c = 0
    int e = g.edges.len()
    for k in 0..e {
        if g.edges.get_ref(k).to_uop == uop { c = c + 1 }
    }
    return c
}

// ---- provenance --------------------------------------------------------------
// Const / LoopInvariant / LoopVariant / LoopCarried / Unknown (ints, like the
// bottleneck/gate tags elsewhere in sim).
def prov_const()           -> int { return 0 }
def prov_loop_invariant()  -> int { return 1 }
def prov_loop_variant()    -> int { return 2 }
def prov_loop_carried()    -> int { return 3 }
def prov_unknown()         -> int { return 4 }

def prov_name(int p) -> Str {
    if p == 0 { return "Const" }
    if p == 1 { return "LoopInvariant" }
    if p == 2 { return "LoopVariant" }
    if p == 3 { return "LoopCarried" }
    return "Unknown"
}

// every register written somewhere in the kernel (= produced this iteration)
def written_regs(&Vec(ir.Inst) prog) -> Vec(Str) {
    Vec(Str) w = {}
    for ins in &prog {
        for op in &ins.ops {
            if op.kind == 0 {
                if op.is_write { w.push(op.text.copy()) }
            }
        }
    }
    return w
}

def _in_set(&Vec(Str) set, &Str x) -> bool {
    for s in &set {
        if s.eq?(x) { return true }
    }
    return false
}

// classify an operand's provenance.
//   immediate                                  -> Const
//   register listed in `const_regs` (annotated .rodata table / known constant)
//                                              -> LoopInvariant
//   register written somewhere in the kernel   -> LoopVariant (produced this iter)
//   register never written, not annotated      -> Unknown (a live-in; could be
//                                                 invariant data or per-iter input)
def classify(&ir.Operand op, &Vec(Str) written, &Vec(Str) const_regs) -> int {
    if op.kind == 2 { return prov_const() }
    if op.kind == 0 {
        if _in_set(const_regs, &op.text) { return prov_loop_invariant() }
        if _in_set(written, &op.text) { return prov_loop_variant() }
        return prov_unknown()
    }
    return prov_unknown()    // memory: needs alias analysis (future)
}

// is this operand a compile-time-known / loop-invariant value?
// (the §6.6d gate: a constant twiddle/coefficient enables the INT16 vpmaddwd path)
def is_invariant(int prov) -> bool {
    return prov == prov_const() || prov == prov_loop_invariant()
}

// ---- loop-invariance fixpoint (the §4.3 def-use dataflow) ---------------------
// A register is loop-invariant if it is seeded constant OR every register/memory
// input of the instruction that defines it is itself invariant (transitively).
// This is the closure const_regs can't express on its own (a value COMPUTED from
// constants is invariant too) — what powers LICM hoisting advice.

// the register written by `ins` (first register write), or "" for none / a store
def _written_reg(&ir.Inst ins) -> Str {
    int m = ins.ops.len()
    for j in 0..m {
        &ir.Operand op = ins.ops.get_ref(j)
        if op.kind == 0 {
            if op.is_write { return op.text.copy() }
        }
    }
    return ""
}

// are all of ins's reads invariant? (>=1 register read; a memory read disqualifies)
def _all_reads_invariant(&ir.Inst ins, &Vec(Str) inv) -> bool {
    int m = ins.ops.len()
    int reads = 0
    for j in 0..m {
        &ir.Operand op = ins.ops.get_ref(j)
        if op.is_read {
            if op.kind == 0 {
                reads = reads + 1
                if !_in_set(inv, &op.text) { return false }
            } else {
                if op.kind == 1 { return false }   // memory read: alias-unsafe, bail
            }
            // immediate reads are constant -> don't disqualify
        }
    }
    return reads > 0
}

def invariant_regs(&Vec(ir.Inst) prog, &Vec(Str) const_regs) -> Vec(Str) {
    Vec(Str) inv = {}
    for c in &const_regs { inv.push(c.copy()) }
    bool changed = true
    int guard = 0
    while changed {
        changed = false
        int n = prog.len()
        for i in 0..n {
            &ir.Inst ins = prog.get_ref(i)
            if _all_reads_invariant(ins, &inv) {
                Str wr = _written_reg(ins)
                if wr.len() > 0 {
                    if !_in_set(&inv, &wr) {
                        inv.push(wr)
                        changed = true
                    }
                }
            }
        }
        guard = guard + 1
        if guard > 1000 { changed = false }
    }
    return inv
}

// is this instruction loop-invariant (hoistable out of the loop)?
//   all reads invariant, writes a register (not a store), has at least one read.
def inst_is_hoistable(&ir.Inst ins, &Vec(Str) inv) -> bool {
    if !_all_reads_invariant(ins, inv) { return false }
    Str wr = _written_reg(ins)
    return wr.len() > 0
}

// ---- text report -------------------------------------------------------------
def report_provenance(&Vec(ir.Inst) prog, &Vec(Str) const_regs) -> Str {
    Vec(Str) written = written_regs(prog)
    Str out = "=== operand provenance ===\n"
    int n = prog.len()
    for i in 0..n {
        &ir.Inst ins = prog.get_ref(i)
        Str line = ins.mnemonic.copy()
        int m = ins.ops.len()
        for j in 0..m {
            &ir.Operand op = ins.ops.get_ref(j)
            if op.is_read {
                int p = classify(op, &written, const_regs)
                Str pn = prov_name(p)
                line = f"{line}  {op.text}:{pn}"
            }
        }
        out = f"{out}  {line}\n"
    }
    return out
}
