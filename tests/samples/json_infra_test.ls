// Test: enum with Vec(Self) and Map(string, Self) indirect self-reference
// This is the prerequisite for JsonValue

import std.vec
import std.map

enum JV {
    Null
    Num(f64 val)
    Str(string val)
    Arr(Vec(JV) items)
    Obj(Map(string, JV) entries)
}

fn main() {
    // Test 1: basic Null variant
    JV a = Null
    match a {
        Null => print("PASS 1: Null")
        _ => print("FAIL 1")
    }

    // Test 2: Num variant
    JV b = Num(3.14)
    match b {
        Num(v) => print(f"PASS 2: Num={v}")
        _ => print("FAIL 2")
    }

    // Test 3: Str variant
    JV c = Str("hello")
    match c {
        Str(s) => print(f"PASS 3: Str={s}")
        _ => print("FAIL 3")
    }

    // Test 4: Arr variant with Vec(JV)
    Vec(JV) items = {}
    items.push(Num(1.0))
    items.push(Num(2.0))
    items.push(Str("three"))
    JV d = Arr(items)
    match d {
        Arr(arr) => print(f"PASS 4: Arr len={arr.len()}")
        _ => print("FAIL 4")
    }

    // Test 5: Obj variant with Map(string, JV)
    Map(string, JV) entries = {}
    entries.set("name", Str("Alice"))
    entries.set("age", Num(30.0))
    JV e = Obj(entries)
    match e {
        Obj(m) => print(f"PASS 5: Obj has name={m.has?("name")}")
        _ => print("FAIL 5")
    }

    print("done")
}
