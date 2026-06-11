// Str method-port batch 3: parsing — to_int/to_i64/to_float/to_bool, each
// returning Result(T, Str) (Err payload is a diagnostic Str; the literal Err
// messages exercise string-literal -> Str coercion in enum-payload position).
// JIT+AOT+memcheck 0/0/0.
import std.str

fn check(bool ok, Str what) {
    if !ok { print(f"STRM3 FAIL: {what}") }
}

fn iok(Result(int, Str) r, int want, Str what) {
    match r {
        Ok(v) => check(v == want, what)
        Err(e) => check(false, what)
    }
}
fn ierr(Result(int, Str) r, Str what) {
    match r {
        Ok(v) => check(false, what)
        Err(e) => check(!e.empty?(), what)
    }
}

fn main() {
    // to_int
    Str a = "123"
    Str b = "-7"
    Str c = "+42"
    iok(a.to_int(), 123, "int pos")
    iok(b.to_int(), -7, "int neg")
    iok(c.to_int(), 42, "int plus")
    Str bad = "12z"
    Str emp = ""
    Str sgn = "-"
    ierr(bad.to_int(), "int bad")
    ierr(emp.to_int(), "int empty")
    ierr(sgn.to_int(), "int signonly")

    // to_i64
    Str big = "1000000"
    match big.to_i64() {
        Ok(v) => check(v == 1000000, "i64")
        Err(e) => check(false, "i64")
    }

    // to_float
    Str f1 = "2.5"
    Str f2 = "-0.25"
    Str f3 = "10"
    match f1.to_float() { Ok(v) => check(v == 2.5, "float") Err(e) => check(false, "float") }
    match f2.to_float() { Ok(v) => check(v == -0.25, "float neg") Err(e) => check(false, "float neg") }
    match f3.to_float() { Ok(v) => check(v == 10.0, "float int") Err(e) => check(false, "float int") }
    Str fx = "x"
    match fx.to_float() { Ok(v) => check(false, "float bad") Err(e) => check(true, "float bad") }

    // to_bool
    Str t = "true"
    Str fa = "false"
    Str no = "yes"
    match t.to_bool() { Ok(v) => check(v, "bool true") Err(e) => check(false, "bool true") }
    match fa.to_bool() { Ok(v) => check(!v, "bool false") Err(e) => check(false, "bool false") }
    match no.to_bool() { Ok(v) => check(false, "bool bad") Err(e) => check(true, "bool bad") }

    print("STRM3 PASS")
}
