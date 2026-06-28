// opt_combinator_test.ls — C1 Option/Result combinators (compiler-lowered):
//   unwrap / expect / unwrap_or / is_some? / is_none? / is_ok? / is_err?
// Mirrors try / force-unwrap `!` (also compiler-lowered, not library methods).
// Self-verifying; the driver also runs AOT + memcheck (owned payloads must not
// leak / double-free). Panic and use-after-move paths live in sibling samples.
import std.core.map
import std.core.str
import std.core.vec

def check(bool c, Str label) {
    if c { @print(f"  ok: {label}") } else { @print(f"FAIL: {label}") }
}

def mk(int n) -> Option(int)         { if n > 0 { return Some(n) } return None }
def mkr(int n) -> Result(int, Str)   { if n > 0 { return Ok(n) }   return Err("neg") }
// C2b helpers: produce owned-Str payloads so the closure-combinator tests
// exercise has_drop move-out / passthrough.
def ok_str(int n) -> Result(Str, Str) { return Ok(f"n{n}") }

def main() -> int {
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

    // ---- C2b: closure combinators (map / and_then / map_err / unwrap_or_else) ----
    // The result type param U is explicit, matching `vec.map(int)(...)`. The
    // closure body is inlined into the lowered match (no closure value / env).

    // map: transform the success payload.
    Option(int) m1 = Some(5)
    check(m1.map(int)(|x| x * 2).unwrap_or(0) == 10, "map: Some -> doubled")
    Option(int) m2 = None
    check(m2.map(int)(|x| x * 2).is_none?(), "map: None passthrough")
    // map to a different type (int -> owned Str); verified via match (consumes result).
    Option(int) m3 = Some(3)
    check(match m3.map(Str)(|x| f"v{x}") { Some(s) => s.eq?("v3")  None => false },
          "map: int -> owned Str")
    // map on Result maps the Ok value; Err passes through.
    Result(int, Str) mr = Ok(4)
    check(mr.map(int)(|x| x + 1).unwrap_or(0) == 5, "map: Result Ok mapped")
    Result(int, Str) mre = Err("bad")
    check(mre.map(int)(|x| x + 1).is_err?(), "map: Result Err passthrough")
    // map moves an owned Str payload out and transforms it.
    Option(Str) mo = Some("hello")
    check(match mo.map(Str)(|x| x.upper()) { Some(s) => s.eq?("HELLO")  None => false },
          "map: owned Str moves & transforms")

    // and_then: chain a fallible step (closure returns the full Option/Result).
    check(mk(6).and_then(int)(|x| mk(x - 6)).is_none?(), "and_then: -> None")
    check(mk(6).and_then(int)(|x| mk(x)).unwrap_or(0) == 6, "and_then: -> Some")
    check(mkr(3).and_then(int)(|x| mkr(x * 2)).unwrap_or(0) == 6, "and_then: Result Ok")
    check(mkr(-1).and_then(int)(|x| mkr(x)).is_err?(), "and_then: Result Err passthrough")
    // and_then producing an owned-Str Result, verified via match.
    Result(int, Str) a1 = Ok(3)
    check(match a1.and_then(Str)(|x| ok_str(x)) { Ok(s) => s.eq?("n3")  Err(_) => false },
          "and_then: owned Str Ok result")

    // map_err: transform the error (Result only).
    Result(int, Str) e1 = Err("oops")
    check(e1.map_err(int)(|e| e.len()).is_err?(), "map_err: Err mapped to int")
    Result(int, Str) e2 = Ok(9)
    check(e2.map_err(int)(|e| 0).unwrap_or(0) == 9, "map_err: Ok passthrough")
    // map_err to an owned Str error, verified via match.
    Result(int, Str) g1 = Err("boom")
    check(match g1.map_err(Str)(|e| f"{e}!") { Ok(_) => false  Err(s) => s.eq?("boom!") },
          "map_err: owned Str error")

    // unwrap_or_else: compute a fallback lazily (no type arg).
    Option(int) u1 = None
    check(u1.unwrap_or_else(|| 40 + 2) == 42, "unwrap_or_else: Option None -> computed")
    check(mk(7).unwrap_or_else(|| 0) == 7, "unwrap_or_else: Option Some")
    Result(int, Str) u2 = Err("oops")
    check(u2.unwrap_or_else(|e| e.len()) == 4, "unwrap_or_else: Result Err uses error")
    check(mkr(8).unwrap_or_else(|e| -1) == 8, "unwrap_or_else: Result Ok")

    // headline chain: v.get(i) -> Option, then map, then unwrap_or.
    Vec(int) vv = [10, 20, 30]
    check(vv.get(1).map(int)(|x| x + 5).unwrap_or(0) == 25, "v.get(i).map(...) chain")
    check(vv.get(9).map(int)(|x| x + 5).unwrap_or(-1) == -1, "v.get(oob).map -> None")

    @print("OPTCOMB PASS")
    return 0
}

// `ok_or` adapts an Option into a Result so `try` can propagate it from a
// Result-returning function — the headline C2a use case.
def read_first(bool present) -> int {
    Result(int, Str) r = read_val(present)
    return match r { Ok(v) => v  Err(e) => -1 }
}
def read_val(bool present) -> Result(int, Str) {
    Option(int) o = None
    if present { o = Some(5) }
    Str absent = "absent"
    int v = try o.ok_or(absent)
    return Ok(v)
}
