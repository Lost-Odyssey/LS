// match_result_own_test.ls — L-013 match-result-ownership matrix.
// Verifies that a match yielding an owned heap payload (Str / Vec / has_drop
// struct·enum) has exactly one owner across every consumer (var-decl / call-arg /
// return / nested), with 0 leak / 0 double-free under --memcheck.
//   docs/plan_match_result_ownership.md §8. Marker: "MATCHOWN PASS".

import std.core.vec
import std.core.str

struct Box { Str name }
enum E { A(Str s)  B }

def mk() -> Result(Str, Str) { return Ok("h1") }

def ret_match(Option(Str) o) -> Str {
    // result of a match used as a return value (return consumer)
    return match o { Some(v) => v  None => "none".copy() }
}

def take(Str s) -> int { return s.len() }

def check(bool cond, Str label) {
    if (!cond) { @print("FAIL:", label) }
}

// ---- match arm RETURNS its owned payload binder re-wrapped in a constructor ----
// `match inner() { Ok(v) => { return Ok(v) } }` — v (has_drop payload) is consumed
// by the Ok(...) ctor INSIDE a `return`, not yielded as a bare `=> v` tail. The
// move-out optimization only detected the bare-tail form, so the binder was dropped
// at arm exit AND owned by the returned value → double-free (0xC0000374), masked for
// cap-0/POD/empty payloads. Owned binders now carry a moved_flag (codegen_match.c).
def rewrap_enum_inner() -> Result(E, Str) { return Ok(A("rw".copy())) }
def rewrap_enum() -> Result(E, Str) {
    match rewrap_enum_inner() { Ok(v) => { return Ok(v) }  Err(e) => { return Err(e) } }
}
def rewrap_vec_inner() -> Result(Vec(int), Str) {
    Vec(int) xs = []
    xs.push(7)
    return Ok(xs)
}
def rewrap_vec() -> Result(Vec(int), Str) {
    match rewrap_vec_inner() { Ok(v) => { return Ok(v) }  Err(e) => { return Err(e) } }
}

def main() {
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
    check(w.get!(1) == 20, "vec payload contents")

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

    // ---- match arm returns owned binder re-wrapped in a ctor (`return Ok(v)`) ----
    match rewrap_enum() {
        Ok(v) => { match v { A(s) => { check(s.eq?("rw"), "rewrap enum binder") }
                             B => { check(false, "rewrap enum B") } } }
        Err(e) => { check(false, "rewrap enum err") }
    }
    match rewrap_vec() {
        Ok(v) => { check(v.len() == 1, "rewrap vec binder") }
        Err(e) => { check(false, "rewrap vec err") }
    }

    @print("MATCHOWN PASS")
}
