// opt_owned_rvalue_test.ls — owned (has_drop Str) Option/Result combinator
// results consumed as BARE rvalues, and the identity-closure combinator.
//
// Two L-013-family fixes guarded here (both previously failed under memcheck):
//
//  1. An owned combinator result (unwrap_or / ok_or / map / `!` / expect, all
//     compiler-lowered to AST_MATCH / AST_FORCE_UNWRAP) consumed as a bare
//     rvalue — print arg / discarded statement / chained receiver — must be
//     dropped at the consuming site. The owned-rvalue consumer whitelists
//     listed AST_CALL but missed the lowered nodes → the payload LEAKED.
//
//  2. `map(|x| x)` (identity closure) lowers to `Some({ x })`: the binder is
//     wrapped in a block ctor. cg_store_owned did not peel the block to find
//     the moved binder, so the payload was BOTH moved into the result AND
//     dropped on arm exit → DOUBLE-FREE. Non-identity bodies (`|s| s.upper()`)
//     yield a fresh rvalue and were never affected.
//
// Self-verifying; the driver runs JIT + AOT + memcheck (0 leak / 0 double-free).
import std.core.str

def ok_str(int n) -> Result(Str, Str) { return Ok(f"n{n}") }
def some_str(int n) -> Option(Str) { return Some(f"s{n}") }

def check(bool c, Str label) {
    if c { @print(f"  ok: {label}") } else { @print(f"FAIL: {label}") }
}

def main() -> int {
    // ---- (1) owned combinator result as a bare rvalue ----
    @print(ok_str(1).unwrap_or("fb"))        // print arg (was leak)
    ok_str(2).unwrap_or("fbx")              // discarded statement (was leak)
    check(ok_str(3).unwrap_or("fb").eq?("n3"), "unwrap_or chained .eq?")
    @print(ok_str(4)!)                       // force-unwrap owned, printed (was leak)
    some_str(8).expect("x")                 // expect owned, discarded (was leak)
    check(some_str(9).map(Str)(|x| x.upper()).unwrap_or("z").eq?("S9"),
          "non-identity map chained (control)")
    Str fs = f"[{ok_str(5).unwrap_or("fb")}]"   // owned combinator in f-string (was leak)
    check(fs.eq?("[n5]"), "owned combinator interpolated in f-string")

    // ---- (2) identity-closure map: block-wrapped binder, no double-free ----
    Option(Str) ms = some_str(5).map(Str)(|x| x)         // bind (non-rvalue path)
    check(match ms { Some(s) => s.eq?("s5")  None => false }, "identity map bound")
    check(some_str(6).map(Str)(|x| x).unwrap_or("z").eq?("s6"),
          "identity map chained .unwrap_or")

    // ---- moved-binder safety: the bound var stays usable / not double-freed ----
    Option(Str) keep = some_str(10).map(Str)(|x| x)
    check(keep.is_some?(), "identity-map result usable after bind")

    @print("OPTOWN PASS")
    return 0
}
