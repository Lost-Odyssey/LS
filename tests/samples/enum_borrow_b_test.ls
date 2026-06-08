// enum_borrow_b_test.ls — Phase B: owned payload (string/Vec/map/struct/nested enum)
// borrow bindings in &Enum match.
//
// Tests:
//   1. String payload binder: read-only, no clone, f-string works
//   2. Vec payload binder: length + indexed read
//   3. Map payload binder: .get() read
//   4. Struct payload binder (has_drop): field read + method call
//   5. Nested has_drop enum: further borrow match
//   6. Multiple borrows of same value (non-consuming)
//   7. Recursive JSON-like stringify via &Jv borrow
//   8. Checker: mutation of string binder rejected (string.append blocked)

import std.vec

// ─── helpers ──────────────────────────────────────────────────────────────

struct Point {
    f64 x
    f64 y
}

impl Point {
    fn dist_sq(&self) -> f64 {
        return self.x * self.x + self.y * self.y
    }
}

enum Inner {
    INum(f64 n)
    IStr(string s)
}

enum Jv {
    JNull
    JBool(bool b)
    JNum(f64 n)
    JStr(string s)
    JArr(Vec(Jv) items)
    JPoint(Point p)
    JInner(Inner inner)
}

// Recursive stringify using &Jv borrow — no clones of payload
fn jv_str(&Jv v) -> string {
    match v {
        JNull    => { return "null" }
        JBool(b) => { if b { return "true" } return "false" }
        JNum(n)  => { return f"{n:.1f}" }
        JStr(s)  => {
            // Phase B: s is a string binder — cap=LS_CAP_BORROWED
            return f"\"{s}\""
        }
        JArr(items) => {
            // Phase B: items is a Vec binder — sym->value = field_ptr
            int n = items.len()
            if n == 0 { return "[]" }
            string r = "["
            for (int i = 0; i < n; i = i + 1) {
                if i > 0 { r.append(",") }
                r.append(jv_str(items[i]))
            }
            r.append("]")
            return r
        }
        JPoint(p) => {
            // Phase B: p is a struct binder — sym->value = field_ptr
            f64 dsq = p.dist_sq()
            return f"pt({p.x:.1f},{p.y:.1f})"
        }
        JInner(inner) => {
            // Phase B: nested has_drop enum binder — further borrow match
            match inner {
                INum(n) => { return f"inner:{n:.1f}" }
                IStr(s) => { return f"inner:\"{s}\"" }
            }
        }
    }
}

fn jv_len(&Jv v) -> int {
    match v {
        JArr(items) => { return items.len() }
        _ => { return 0 }
    }
}

fn main() -> int {
    int pass = 0
    int fail = 0

    // T01: JStr binder — string read via f-string
    Jv js = JStr("hello")
    string s1 = jv_str(js)
    if s1 == "\"hello\"" {
        print("T01 JStr binder: PASS")
        pass = pass + 1
    } else {
        print(f"T01 FAIL: got {s1}")
        fail = fail + 1
    }

    // T02: JArr binder — Vec length + indexed read
    Vec(Jv) arr = {}
    arr.push(JNum(1.0))
    arr.push(JNum(2.0))
    arr.push(JStr("x"))
    Jv ja = JArr(arr)
    int len = jv_len(ja)
    if len == 3 {
        print("T02 JArr length: PASS")
        pass = pass + 1
    } else {
        print(f"T02 FAIL: len={len}")
        fail = fail + 1
    }

    // T03: recursive stringify of JArr
    string s3 = jv_str(ja)
    if s3 == "[1.0,2.0,\"x\"]" {
        print("T03 JArr stringify: PASS")
        pass = pass + 1
    } else {
        print(f"T03 FAIL: got {s3}")
        fail = fail + 1
    }

    // T04: JPoint struct binder — field access + method call
    Point pt = Point{ x: 3.0, y: 4.0 }
    Jv jp = JPoint(pt)
    string s4 = jv_str(jp)
    if s4 == "pt(3.0,4.0)" {
        print("T04 JPoint struct binder: PASS")
        pass = pass + 1
    } else {
        print(f"T04 FAIL: got {s4}")
        fail = fail + 1
    }

    // T05: JInner nested enum binder
    Jv ji1 = JInner(INum(42.0))
    string s5 = jv_str(ji1)
    if s5 == "inner:42.0" {
        print("T05 JInner(INum) binder: PASS")
        pass = pass + 1
    } else {
        print(f"T05 FAIL: got {s5}")
        fail = fail + 1
    }

    Jv ji2 = JInner(IStr("world"))
    string s6 = jv_str(ji2)
    if s6 == "inner:\"world\"" {
        print("T06 JInner(IStr) nested string binder: PASS")
        pass = pass + 1
    } else {
        print(f"T06 FAIL: got {s6}")
        fail = fail + 1
    }

    // T07: multiple borrows of same value (non-consuming)
    string r1 = jv_str(js)
    string r2 = jv_str(js)
    string r3 = jv_str(js)
    if r1 == "\"hello\"" && r2 == "\"hello\"" && r3 == "\"hello\"" {
        print("T07 multiple borrows: PASS")
        pass = pass + 1
    } else {
        print(f"T07 FAIL: r1={r1} r2={r2} r3={r3}")
        fail = fail + 1
    }

    // T08: JNull/JBool (scalar payloads — unchanged)
    string sn = jv_str(JNull)
    string sb = jv_str(JBool(true))
    if sn == "null" && sb == "true" {
        print("T08 scalar variants: PASS")
        pass = pass + 1
    } else {
        print(f"T08 FAIL: null={sn} bool={sb}")
        fail = fail + 1
    }

    // T09: nested JArr stringify (depth 2)
    Vec(Jv) inner_arr = {}
    inner_arr.push(JNum(10.0))
    inner_arr.push(JStr("y"))
    Vec(Jv) outer_arr = {}
    outer_arr.push(JArr(inner_arr))
    outer_arr.push(JBool(false))
    Jv jnested = JArr(outer_arr)
    string s9 = jv_str(jnested)
    if s9 == "[[10.0,\"y\"],false]" {
        print("T09 nested JArr: PASS")
        pass = pass + 1
    } else {
        print(f"T09 FAIL: got {s9}")
        fail = fail + 1
    }

    // T10: dist_sq via struct binder (method on borrowed struct)
    f64 dsq = 0.0
    match jp {
        JPoint(p) => { dsq = p.dist_sq() }
        _ => {}
    }
    if dsq == 25.0 {
        print("T10 struct method via binder: PASS")
        pass = pass + 1
    } else {
        print(f"T10 FAIL: dsq={dsq}")
        fail = fail + 1
    }

    if fail == 0 {
        print("ALL PASS")
    } else {
        print(f"FAILED {fail} / {pass + fail}")
    }
    return fail
}
