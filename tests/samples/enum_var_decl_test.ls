// enum_var_decl_test.ls
// Phase E-3 TDD: has-drop enum variable declaration ownership semantics
// Verifies: 0 leaks, 0 double-frees with --memcheck

enum Value {
    None
    Num(f64 n)
    Txt(string s)
    Pair(string a, string b)
}

fn make_txt() -> Value {
    return Txt("hello".copy())
}

fn identity(Value v) -> Value {
    return v
}

fn main() {
    // A: var_decl from rvalue (call) — no clone needed, just transfer
    Value a = make_txt()
    print("PASS 1")

    // B: var_decl from IDENT — must deep-clone (Bug #13 fix)
    Value b = a
    print("PASS 2")
    // a and b are fully independent → both drop cleanly

    // C: reassignment (drop old + store new)
    Value c = Txt("old".copy())
    c = Txt("new".copy())
    print("PASS 3")
    // "old" string was dropped before reassign; c holds "new"

    // D: uninitialized var + later assign (zero-init fix, Bug #14)
    Value d
    d = Pair("x".copy(), "y".copy())
    print("PASS 4")
    // zero-init ensures clean state before first assignment

    // E: match + enum stays alive after
    Value e = Txt("match_me".copy())
    match e {
        Txt(s) => { print("PASS 5") }
        _ => {}
    }

    // F: chain of copies — all independent
    Value f1 = Pair("p".copy(), "q".copy())
    Value f2 = f1
    Value f3 = f2
    print("PASS 6")
    // f1, f2, f3 each own their own strings

    // G: identity pass-through (fn receives clone, returns owned)
    Value g_in = Txt("passthru".copy())
    Value g_out = identity(g_in)
    print("PASS 7")

    print("all done")
}
