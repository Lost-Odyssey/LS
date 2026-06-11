// opt_combinator_test.ls — C1 Option/Result combinators (compiler-lowered):
//   unwrap / expect / unwrap_or / is_some? / is_none? / is_ok? / is_err?
// Mirrors try / force-unwrap `!` (also compiler-lowered, not library methods).
// Self-verifying; the driver also runs AOT + memcheck (owned payloads must not
// leak / double-free). Panic and use-after-move paths live in sibling samples.
import std.map
import std.str

fn check(bool c, Str label) {
    if c { print(f"  ok: {label}") } else { print(f"FAIL: {label}") }
}

fn mk(int n) -> Option(int)         { if n > 0 { return Some(n) } return None }
fn mkr(int n) -> Result(int, Str)   { if n > 0 { return Ok(n) }   return Err("neg") }

fn main() -> int {
    // ---- unwrap (Option / Result success) ----
    check(Some(5).unwrap() == 5, "Option unwrap")
    check(mkr(7).unwrap() == 7, "Result unwrap (Ok)")

    // ---- unwrap_or: Some/Ok -> payload, None/Err -> fallback ----
    Option(int) none = None
    check(none.unwrap_or(99) == 99, "Option unwrap_or (None -> fallback)")
    check(Some(7).unwrap_or(0) == 7, "Option unwrap_or (Some -> payload)")
    Result(int, Str) er = Err("bad")
    check(er.unwrap_or(-1) == -1, "Result unwrap_or (Err -> fallback, drops payload)")
    Result(int, Str) ok = Ok(8)
    check(ok.unwrap_or(0) == 8, "Result unwrap_or (Ok -> payload)")

    // ---- predicates borrow (do NOT consume the receiver) ----
    Option(int) d = Some(1)
    check(d.is_some?(), "is_some? true")
    check(!d.is_none?(), "is_none? false")
    check(d.unwrap() == 1, "receiver still usable after predicates")
    Option(int) e = None
    check(e.is_none?() && !e.is_some?(), "None predicates")
    Result(int, Str) r1 = Ok(3)
    check(r1.is_ok?() && !r1.is_err?(), "Result Ok predicates")
    Result(int, Str) r2 = Err("x")
    check(r2.is_err?() && !r2.is_ok?(), "Result Err predicates")

    // ---- owned success payload: unwrap moves it out (no leak) ----
    Option(Str) s = Some("hello")
    Str got = s.unwrap()
    check(got.eq?("hello"), "owned Str unwrap moves out")
    Option(Str) t = None
    Str fb = t.unwrap_or("fallback")
    check(fb.eq?("fallback"), "owned Str unwrap_or fallback")

    // ---- expect success path returns the payload ----
    Option(int) some42 = Some(42)
    check(some42.expect("must exist") == 42, "expect success")

    // ---- headline use case: m.get(k).unwrap_or(default) ----
    Map(Str, int) m = {}
    m["a"] = 1
    m["b"] = 2
    check(m.get("a").unwrap_or(0) == 1, "map.get(k).unwrap_or hit")
    check(m.get("z").unwrap_or(-1) == -1, "map.get(k).unwrap_or miss")
    check(m.get("b").is_some?(), "map.get(k).is_some?")
    check(m.get("z").is_none?(), "map.get(k).is_none?")

    // ---- chaining on an rvalue receiver ----
    check(mk(3).unwrap_or(0) + 1 == 4, "rvalue chain unwrap_or + arithmetic")

    // ---- C2a: Option<->Result conversions (ok / err / ok_or) ----
    // ok_or: Option(T) -> Result(T, E), attaching an error value.
    Option(int) sv = Some(7)
    Str missing = "missing"
    Result(int, Str) ro = sv.ok_or(missing)
    check(ro.is_ok?() && ro.unwrap_or(-1) == 7, "ok_or: Some -> Ok")
    Option(int) nv = None
    Str missing2 = "missing"
    Result(int, Str) re = nv.ok_or(missing2)
    check(re.is_err?() && re.unwrap_or(-1) == -1, "ok_or: None -> Err")

    // ok(): Result(T,E) -> Option(T), dropping the error. Chained with no expected
    // type (a second Option instantiation exists above), exercising the
    // hint-directed bare-ctor disambiguation.
    Result(int, Str) cok = Ok(42)
    Result(int, Str) cer = Err("boom")
    check(cok.ok().unwrap_or(0) == 42, "ok(): Ok -> Some, chained unwrap_or")
    check(cer.ok().unwrap_or(-9) == -9, "ok(): Err -> None (payload dropped)")

    // err(): Result(T,E) -> Option(E), keeping the error.
    Result(int, Str) cer2 = Err("bang")
    Str clean = "clean"
    check(cer2.err().unwrap_or(clean).eq?("bang"), "err(): Err -> Some(error)")
    Result(int, Str) cok2 = Ok(1)
    Str nonefb = "none"
    check(cok2.err().unwrap_or(nonefb).eq?("none"), "err(): Ok -> None")

    // ok_or + try interop: an Option flows into Result-propagation in one line.
    check(read_first(true) == 5, "try ok_or interop (present)")
    check(read_first(false) == -1, "try ok_or interop (absent -> Err propagated)")

    print("OPTCOMB PASS")
    return 0
}

// `ok_or` adapts an Option into a Result so `try` can propagate it from a
// Result-returning function — the headline C2a use case.
fn read_first(bool present) -> int {
    Result(int, Str) r = read_val(present)
    return match r { Ok(v) => v  Err(e) => -1 }
}
fn read_val(bool present) -> Result(int, Str) {
    Option(int) o = None
    if present { o = Some(5) }
    Str absent = "absent"
    int v = try o.ok_or(absent)
    return Ok(v)
}
