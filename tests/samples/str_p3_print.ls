// P3 self-verifying (docs/plan_string_to_stdlib.md §5.3): `print` accepts a `Str`
// and writes its raw text (printf "%.*s", len-bounded — a general Str buffer is
// not NUL-terminated). v1 keeps compiler per-type formatting; the @print sigil
// and Printable trait (v2) are deferred. Owned Str rvalues passed to print are
// dropped (no leak). The PASS/FAIL asserts cover the ownership/round-trip; the
// printed lines below also let the eye confirm text output. JIT+AOT+memcheck.
import std.str
import std.vec

fn check(bool ok, string what) {
    if !ok { print(f"STRP3 FAIL: {what}") }
}

// builtin-string equality helper: routes a (possibly Str-flipped) literal back
// through a `string` param so the `to_string()` round-trips stay covered.
fn seq(string a, string b) -> bool { return a == b }

fn make() -> Str { return f"made-{7}" }

fn main() {
    // print a static-literal Str
    Str a = "hello"
    print(a)                       // hello

    // print an owned (bridge-built) Str — buffer NOT NUL-terminated, %.*s bounds it
    Str b = Str.from_string("dyn world")
    print(b)                       // dyn world
    check(seq(b.to_string(), "dyn world"), "owned roundtrip")

    // print an owned f-string Str
    Str f = f"x={42}"
    print(f)                       // x=42
    check(seq(f.to_string(), "x=42"), "fstr roundtrip")

    // print an owned Str RVALUE (clone) — must print text AND drop the clone clean
    print(b.__clone())             // dyn world

    // print an owned Str returned from a call (rvalue, dropped after print)
    print(make())                  // made-7

    // print Str element read from a Vec (owned clone rvalue via get)
    Vec(Str) v = {}
    v.push("alpha")
    v.push(f"beta-{1}")
    print(v.get(0))                // alpha
    print(v.get(1))                // beta-1
    check(v.len() == 2, "vec len")

    // bare-binding print does NOT consume (b still usable afterward)
    print(b)                       // dyn world
    check(seq(b.to_string(), "dyn world"), "no-consume after print")

    print("STRP3 PASS")
}
