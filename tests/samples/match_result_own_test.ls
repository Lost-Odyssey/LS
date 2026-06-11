// match_result_own_test.ls — L-013 match-result-ownership matrix.
// Verifies that a match yielding an owned heap payload (Str / Vec / has_drop
// struct·enum) has exactly one owner across every consumer (var-decl / call-arg /
// return / nested), with 0 leak / 0 double-free under --memcheck.
//   docs/plan_match_result_ownership.md §8. Marker: "MATCHOWN PASS".

import std.vec
import std.str

struct Box { Str name }
enum E { A(Str s)  B }

fn mk() -> Result(Str, Str) { return Ok("h1") }

fn ret_match(Option(Str) o) -> Str {
    // result of a match used as a return value (return consumer)
    return match o { Some(v) => v  None => "none".copy() }
}

fn take(Str s) -> int { return s.len() }

fn check(bool cond, Str label) {
    if (!cond) { print("FAIL:", label) }
}

fn main() {
    // ---- Str `=> binder` : var-decl consumer (was leaking 16B) ----
    Str s = match mk() { Ok(v) => v  Err(e) => e }
    check(s.eq?("h1"), "string binder var-decl")

    // ---- Str `=> binder` : call-arg consumer ----
    int n = take(match mk() { Ok(v) => v  Err(e) => e })
    check(n == 2, "string binder call-arg")

    // ---- binder + concat mixed arms ----
    Str t = match mk() { Ok(v) => v + "!"  Err(e) => e }
    check(t.eq?("h1!"), "binder+concat")

    // ---- binder + static fallback ----
    Option(Str) o1 = Some("x".copy())
    Str u = match o1 { Some(v) => v  None => "d" }
    check(u.eq?("x"), "binder+static")

    // ---- result of match as a return value ----
    Option(Str) o2 = Some("ret".copy())
    Str r = ret_match(o2)
    check(r.eq?("ret"), "match result as return")

    // ---- has_drop STRUCT: yield OUTER local (was double-free) ----
    Box outer = Box{ name: "outer".copy() }
    bool flag = true
    Box bx = match flag { true => outer  false => Box{ name: "z".copy() } }
    check(bx.name.eq?("outer"), "struct outer yield result")
    check(outer.name.eq?("outer"), "struct outer still alive")

    // ---- has_drop ENUM: yield OUTER local ----
    E e_outer = A("payload".copy())
    E er = match flag { true => e_outer  false => B }
    Str er_s = match er { A(s) => s  B => "b".copy() }
    check(er_s.eq?("payload"), "enum outer yield result")
    Str eo_s = match e_outer { A(s) => s  B => "b".copy() }
    check(eo_s.eq?("payload"), "enum outer still alive")

    // ---- Vec(int) payload moved out via match ----
    Vec(int) vsrc = [10, 20, 30]
    Vec(int) vfb = [99]
    Option(Vec(int)) ov = Some(vsrc)
    Vec(int) w = match ov { Some(v) => v  None => vfb }
    check(w.len() == 3, "vec payload move-out")
    check(w.get(1) == 20, "vec payload contents")

    // ---- nested match yielding inner owned result ----
    Option(Str) a = Some("A".copy())
    Option(Str) b = Some("B".copy())
    bool pick_a = false
    Str nest = match pick_a {
        true  => match a { Some(x) => x  None => "na".copy() }
        false => match b { Some(y) => y  None => "nb".copy() }
    }
    check(nest.eq?("B"), "nested match owned result")

    // ---- match result discarded (value produced, unused) ----
    match mk() { Ok(v) => v  Err(e) => e }

    print("MATCHOWN PASS")
}
