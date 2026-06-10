// match_result_own_test.ls — L-013 match-result-ownership matrix.
// Verifies that a match yielding an owned heap payload (string / Vec / has_drop
// struct·enum) has exactly one owner across every consumer (var-decl / call-arg /
// return / nested), with 0 leak / 0 double-free under --memcheck.
//   docs/plan_match_result_ownership.md §8. Marker: "MATCHOWN PASS".

import std.vec

struct Box { string name }
enum E { A(string s)  B }

fn mk() -> Result(string, string) { return Ok("h" + "1") }

fn ret_match(Option(string) o) -> string {
    // result of a match used as a return value (return consumer)
    return match o { Some(v) => v  None => "none".copy() }
}

fn take(string s) -> int { return s.length }

fn check(bool cond, string label) {
    if (!cond) { print("FAIL:", label) }
}

fn main() {
    // ---- string `=> binder` : var-decl consumer (was leaking 16B) ----
    string s = match mk() { Ok(v) => v  Err(e) => e }
    check(s == "h1", "string binder var-decl")

    // ---- string `=> binder` : call-arg consumer ----
    int n = take(match mk() { Ok(v) => v  Err(e) => e })
    check(n == 2, "string binder call-arg")

    // ---- binder + concat mixed arms ----
    string t = match mk() { Ok(v) => v + "!"  Err(e) => e }
    check(t == "h1!", "binder+concat")

    // ---- binder + static fallback ----
    Option(string) o1 = Some("x".copy())
    string u = match o1 { Some(v) => v  None => "d" }
    check(u == "x", "binder+static")

    // ---- result of match as a return value ----
    Option(string) o2 = Some("ret".copy())
    string r = ret_match(o2)
    check(r == "ret", "match result as return")

    // ---- has_drop STRUCT: yield OUTER local (was double-free) ----
    Box outer = Box{ name: "outer".copy() }
    bool flag = true
    Box bx = match flag { true => outer  false => Box{ name: "z".copy() } }
    check(bx.name == "outer", "struct outer yield result")
    check(outer.name == "outer", "struct outer still alive")

    // ---- has_drop ENUM: yield OUTER local ----
    E e_outer = A("payload".copy())
    E er = match flag { true => e_outer  false => B }
    string er_s = match er { A(s) => s  B => "b".copy() }
    check(er_s == "payload", "enum outer yield result")
    string eo_s = match e_outer { A(s) => s  B => "b".copy() }
    check(eo_s == "payload", "enum outer still alive")

    // ---- Vec(int) payload moved out via match ----
    Vec(int) vsrc = [10, 20, 30]
    Vec(int) vfb = [99]
    Option(Vec(int)) ov = Some(vsrc)
    Vec(int) w = match ov { Some(v) => v  None => vfb }
    check(w.len() == 3, "vec payload move-out")
    check(w.get(1) == 20, "vec payload contents")

    // ---- nested match yielding inner owned result ----
    Option(string) a = Some("A".copy())
    Option(string) b = Some("B".copy())
    bool pick_a = false
    string nest = match pick_a {
        true  => match a { Some(x) => x  None => "na".copy() }
        false => match b { Some(y) => y  None => "nb".copy() }
    }
    check(nest == "B", "nested match owned result")

    // ---- match result discarded (value produced, unused) ----
    match mk() { Ok(v) => v  Err(e) => e }

    print("MATCHOWN PASS")
}
