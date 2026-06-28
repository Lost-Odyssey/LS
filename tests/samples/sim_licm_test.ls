// sim_licm_test.ls — loop-invariant code motion advice (plan §4.3 fixpoint).
//
// An instruction whose inputs are ALL loop-invariant computes the same value every
// iteration and should be hoisted. The key is transitivity: a value computed from
// constants is itself invariant, so the invariance propagates (the def-use fixpoint).
// An opcode linter can't give this — it depends on which operands are constant.

import sim.intel.decode as decode
import sim.intel.patterns as adv
import sim.core.analysis as an
import sim.core.ir as ir
import std.core.vec
import std.core.str

def check(bool cond, Str name) {
    if cond { @print(f"  ok: {name}") }
    else { @print(f"  FAIL: {name}") }
}

def in_set(&Vec(Str) set, Str x) -> bool {
    for s in &set {
        if s.eq?(&x) { return true }
    }
    return false
}

def any_title_has(&Vec(adv.Suggestion) sg, Str needle) -> bool {
    for s in &sg {
        if s.title.contains?(&needle) { return true }
    }
    return false
}

def main() {
    // zmm16 is a constant; zmm0 is per-iteration live-in data.
    //   vpaddd zmm20, zmm16, zmm16  -> all-const  -> invariant, hoistable
    //   vpslld zmm21, zmm20, 4      -> zmm20 invariant + imm -> invariant (TRANSITIVE)
    //   vpaddd zmm5,  zmm0,  zmm21  -> reads live-in zmm0 -> NOT invariant, stays in loop
    Str src = ""
    src = f"{src}vpaddd zmm20, zmm16, zmm16\n"
    src = f"{src}vpslld zmm21, zmm20, 4\n"
    src = f"{src}vpaddd zmm5, zmm0, zmm21\n"
    Vec(ir.Inst) prog = decode.parse_listing(&src, 0x400 as i64)

    Vec(Str) cregs = {}
    cregs.push("zmm16")

    // ---- invariance fixpoint ----
    Vec(Str) inv = an.invariant_regs(&prog, &cregs)
    check(in_set(&inv, "zmm16"), "seed constant zmm16 is invariant")
    check(in_set(&inv, "zmm20"), "zmm20 = f(const,const) is invariant")
    check(in_set(&inv, "zmm21"), "zmm21 = f(zmm20,imm) is invariant (TRANSITIVE)")
    check(!in_set(&inv, "zmm5"), "zmm5 = f(live-in, ...) is NOT invariant")
    check(!in_set(&inv, "zmm0"), "live-in data zmm0 is NOT invariant")

    // ---- LICM advice ----
    Vec(adv.Suggestion) sg = adv.advise_licm(&prog, &cregs)
    @print(adv.render(&sg))
    check(sg.len() == 2, "two hoistable instructions flagged (the third reads data)")
    check(any_title_has(&sg, "vpaddd"), "vpaddd zmm20 flagged hoistable")
    check(any_title_has(&sg, "vpslld"), "vpslld zmm21 flagged hoistable (transitive)")

    // ---- a kernel with no invariants has nothing to hoist ----
    Str bsrc = ""
    bsrc = f"{bsrc}vpermw zmm1, zmm0, zmm16\n"
    bsrc = f"{bsrc}vpor   zmm5, zmm1, zmm2\n"
    Vec(ir.Inst) bprog = decode.parse_listing(&bsrc, 0x500 as i64)
    Vec(Str) nocreg = {}
    Vec(adv.Suggestion) sg2 = adv.advise_licm(&bprog, &nocreg)
    check(sg2.len() == 0, "no constants annotated -> nothing hoistable")

    @print("SIM LICM PASS")
}
