/* Phase F.5 tests: enum capture in closures
   Expected output (one per line):
     None
     42
     error: not found
     hello
     200
     Red
     1
     10
*/

import std.core.vec
import std.core.str

enum Direction { North South East West }
enum Color     { Red Green Blue RGB(int r, int g, int b) }

type IntGetter    = Block() -> int
type StringGetter = Block() -> Str
type OptGetter    = Block() -> int

/* F.5.1: capture non-has_drop enum (disc-only) — by-copy */
def test_pod_enum_capture() {
    Direction d = North
    IntGetter f = || {
        match d {
            North => { return 1 }
            South => { return 2 }
            East  => { return 3 }
            West  => { return 4 }
        }
    }
    int v = f()
    if v == 1 {
        @print("None")
    }
}

/* F.5.2: capture Option(int) (has_drop enum, by-move) */
def test_option_capture() {
    Option(int) o = Some(42)
    IntGetter f = || {
        match o {
            Some(v) => { return v }
            None    => { return 0 }
        }
    }
    @print(f())   /* 42 */
}

/* F.5.3: capture Result(int, Str) (has_drop enum, by-move) */
def test_result_capture() {
    Result(int, Str) r = Err("not found")
    StringGetter f = || {
        match r {
            Ok(v)  => { return "ok" }
            Err(e) => { return f"error: {e}" }
        }
    }
    @print(f())   /* error: not found */
}

/* F.5.4: factory — return Block capturing has_drop enum */
def make_ok_getter(Result(Str, Str) res) -> StringGetter {
    return || {
        match res {
            Ok(s)  => { return s }
            Err(e) => { return e }
        }
    }
}

def test_factory() {
    StringGetter f = make_ok_getter(Ok("hello"))
    @print(f())   /* hello */
}

/* F.5.5: capture non-has_drop enum + POD capture mixed */
def test_color_mul_capture() {
    Direction d = East
    int factor = 100
    IntGetter f = || {
        int base = 0
        match d {
            North => { base = 1 }
            South => { base = 2 }
            East  => { base = 2 }
            West  => { base = 4 }
        }
        return base * factor
    }
    @print(f())   /* East→2, 2*100=200 */
}

/* F.5.6: capture non-has_drop enum (Color with POD payload) */
def test_color_capture() {
    Color c = Red
    StringGetter f = || {
        match c {
            Red          => { return "Red" }
            Green        => { return "Green" }
            Blue         => { return "Blue" }
            RGB(r, g, b) => { return f"RGB({r},{g},{b})" }
        }
    }
    @print(f())   /* Red */
}

/* F.5.7: Vec of Block capturing has_drop enum */
def test_vec_block_option() {
    Vec(OptGetter) funcs = {}

    Option(int) a = Some(1)
    funcs.push(|| {
        match a {
            Some(v) => { return v }
            None    => { return -1 }
        }
    })

    @print(funcs[0]())   /* 1 */
}

/* F.5.8: repeated call of closure capturing enum */
def test_repeated_call() {
    Option(int) seed = Some(5)
    IntGetter f = || {
        match seed {
            Some(v) => { return v * 2 }
            None    => { return 0 }
        }
    }
    @print(f())   /* 10 */
}

def main() {
    test_pod_enum_capture()
    test_option_capture()
    test_result_capture()
    test_factory()
    test_color_mul_capture()
    test_color_capture()
    test_vec_block_option()
    test_repeated_call()
}
