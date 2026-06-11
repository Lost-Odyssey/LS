// struct_field_defaults_v2_test.ls — v2 defaults: empty/literal vec + nested struct.
// Prints "SFD2 PASS" / "SFD2 FAIL: ...".

import std.vec
import std.str

struct Sub {
    int a = 5
    int b = 7
}

struct Cfg {
    int w = 800
    Vec(int) preset = [1, 2, 3]      // literal vec default
    Vec(f64) data = []               // empty vec default
    Vec(Str) names = ["x", "y"]      // Str vec default
    Sub inner = Sub{}                // nested struct default
}

fn check(Str got, Str want, Str name) -> bool {
    if got.eq?(want) { return true }
    print(f"SFD2 FAIL: {name} got=[{got}] want=[{want}]")
    return false
}

fn main() {
    bool ok = true

    Cfg c = Cfg{}

    // literal vec default: length + element values
    int p0 = c.preset[0]
    int p2 = c.preset[2]
    ok = check(f"{c.preset.len()}:{p0}:{p2}", "3:1:3", "vec_literal_default") && ok

    // empty vec default starts empty, supports push
    ok = check(f"{c.data.len()}", "0", "vec_empty_default") && ok
    c.data.push(1.5)
    c.data.push(2.5)
    ok = check(f"{c.data.len()}", "2", "vec_empty_push") && ok

    // Str vec default
    Str n1 = c.names[1]
    ok = check(f"{c.names.len()}:{n1}", "2:y", "vec_string_default") && ok

    // nested struct default
    ok = check(f"{c.inner.a},{c.inner.b}", "5,7", "nested_struct_default") && ok

    // override one field; the rest keep defaults
    Cfg d = Cfg{w: 99}
    int dp0 = d.preset[0]
    ok = check(f"{d.w}:{d.preset.len()}:{dp0}:{d.inner.a}", "99:3:1:5", "partial_override") && ok

    if ok { print("SFD2 PASS") }
}
