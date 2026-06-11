// M-3 验证：统一所有权转移 API 在各站点的正确性
// NOTE: Vec(has_drop struct) 的 drop 正确性在 M-4 测试中覆盖

import std.vec
import std.str

struct Person {
    Str name
    int age
}

enum Value {
    Text(Str)
    Num(int)
    None
}

fn make_name() -> Str {
    return "dynamic".upper()
}

fn main() -> int {
    // 1. Vec.push Str IDENT → move
    Str s1 = "hello".upper()
    Vec(Str) v1 = {}
    v1.push(s1)
    print(v1[0])

    // 2. Vec.push Str rvalue → temp transfer
    Vec(Str) v2 = {}
    v2.push("world".upper())
    print(v2[0])

    // 3. Vec.push Str from fn call → rvalue transfer
    Vec(Str) v3 = {}
    v3.push(make_name())
    print(v3[0])

    // 4. Vec[i] = Str IDENT → move
    Vec(Str) v4 = ["aaa".copy(), "bbb".copy()]
    Str x = "ccc".copy()
    v4[0] = x
    print(v4[0])

    // 5. enum ctor with Str IDENT copy → 存入 enum
    Str name = "Bob".upper()
    Value val = Text(name.copy())
    match val {
        Text(sv) => { print(sv) }
        Num(n)  => { print(n) }
        None    => { print("none") }
    }

    // 6. struct ctor with Str rvalue
    Person p = Person{name: "Alice".upper(), age: 30}
    print(p.name)

    // 7. Vec(Str) swap via index assign
    Vec(Str) v5 = ["AAA".copy(), "BBB".copy()]
    Str a = v5[0].copy()
    Str b = v5[1].copy()
    v5[0] = b
    v5[1] = a
    print(v5[0])
    print(v5[1])

    print("done")
    return 0
}
