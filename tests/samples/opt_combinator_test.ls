// opt_combinator_test.ls — C1 Option/Result combinators (compiler-lowered):
//   unwrap / expect / unwrap_or / is_some? / is_none? / is_ok? / is_err?
// Mirrors try / force-unwrap `!` (also compiler-lowered, not library methods).
// Self-verifying; the driver also runs AOT + memcheck (owned payloads must not
// leak / double-free). Panic and use-after-move paths live in sibling samples.
import std.map

fn check(bool c, string label) {
    if c { print(f"  ok: {label}") } else { print(f"FAIL: {label}") }
}

fn mk(int n) -> Option(int)            { if n > 0 { return Some(n) } return None }
fn mkr(int n) -> Result(int, string)   { if n > 0 { return Ok(n) }   return Err("neg") }

fn main() -> int {
    // ---- unwrap (Option / Result success) ----
    check(Some(5).unwrap() == 5, "Option unwrap")
    check(mkr(7).unwrap() == 7, "Result unwrap (Ok)")

    // ---- unwrap_or: Some/Ok -> payload, None/Err -> fallback ----
    Option(int) none = None
    check(none.unwrap_or(99) == 99, "Option unwrap_or (None -> fallback)")
    check(Some(7).unwrap_or(0) == 7, "Option unwrap_or (Some -> payload)")
    Result(int, string) er = Err("bad")
    check(er.unwrap_or(-1) == -1, "Result unwrap_or (Err -> fallback, drops payload)")
    Result(int, string) ok = Ok(8)
    check(ok.unwrap_or(0) == 8, "Result unwrap_or (Ok -> payload)")

    // ---- predicates borrow (do NOT consume the receiver) ----
    Option(int) d = Some(1)
    check(d.is_some?(), "is_some? true")
    check(!d.is_none?(), "is_none? false")
    check(d.unwrap() == 1, "receiver still usable after predicates")
    Option(int) e = None
    check(e.is_none?() && !e.is_some?(), "None predicates")
    Result(int, string) r1 = Ok(3)
    check(r1.is_ok?() && !r1.is_err?(), "Result Ok predicates")
    Result(int, string) r2 = Err("x")
    check(r2.is_err?() && !r2.is_ok?(), "Result Err predicates")

    // ---- owned success payload: unwrap moves it out (no leak) ----
    Option(string) s = Some("hello")
    string got = s.unwrap()
    check(got == "hello", "owned string unwrap moves out")
    Option(string) t = None
    string fb = t.unwrap_or("fallback")
    check(fb == "fallback", "owned string unwrap_or fallback")

    // ---- expect success path returns the payload ----
    Option(int) some42 = Some(42)
    check(some42.expect("must exist") == 42, "expect success")

    // ---- headline use case: m.get(k).unwrap_or(default) ----
    Map(string, int) m = {}
    m["a"] = 1
    m["b"] = 2
    check(m.get("a").unwrap_or(0) == 1, "map.get(k).unwrap_or hit")
    check(m.get("z").unwrap_or(-1) == -1, "map.get(k).unwrap_or miss")
    check(m.get("b").is_some?(), "map.get(k).is_some?")
    check(m.get("z").is_none?(), "map.get(k).is_none?")

    // ---- chaining on an rvalue receiver ----
    check(mk(3).unwrap_or(0) + 1 == 4, "rvalue chain unwrap_or + arithmetic")

    print("OPTCOMB PASS")
    return 0
}
