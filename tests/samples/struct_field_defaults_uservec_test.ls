// struct_field_defaults_uservec_test.ls — VR-LIM-012:
// user Vec(T) field defaults use the __from_list protocol.

import std.vec

struct Kid {
    string name
}

impl Kid {
    static fn make(string s) -> Kid { return Kid { name: s } }
}

struct Bag {
    Vec(int) nums = [1, 2, 3]
    Vec(string) names = [f"ada", f"bea"]
    Vec(f64) empty = []
    Vec(Kid) kids = [Kid.make("ivy".upper()), Kid.make("max".upper())]
    Vec(Vec(int)) rows = [[1, 2], [3, 4, 5]]
}

fn check(bool c, string label) -> bool {
    if c { return true }
    print("FAIL " + label)
    return false
}

fn main() {
    Bag b = Bag{}
    bool ok = true
    ok = check(b.nums.len() == 3 && b.nums.get(0) == 1 && b.nums.get(2) == 3, "Vec(int) default") && ok
    ok = check(b.names.len() == 2 && b.names.get(1) == "bea", "Vec(string) default") && ok
    ok = check(b.empty.len() == 0, "Vec(f64) empty default") && ok
    Kid k = b.kids.get(0)
    ok = check(b.kids.len() == 2 && k.name == "IVY", "Vec(Kid) default") && ok
    Vec(int) row1 = b.rows.get(1)
    ok = check(b.rows.len() == 2 && row1.len() == 3 && row1.get(2) == 5, "nested Vec default") && ok

    Bag c = Bag{ names: [f"zed"] }
    ok = check(c.nums.len() == 3 && c.names.len() == 1 && c.names.get(0) == "zed", "override one field") && ok

    if ok { print("USERVEC_FIELD_DEFAULTS PASS") }
}
