// M-3 验证：统一所有权转移 API 在各站点的正确性
// NOTE: Vec(has_drop struct) 的 drop 正确性在 M-4 测试中覆盖

import std.vec

struct Person {
    string name
    int age
}

enum Value {
    Str(string)
    Num(int)
    None
}

fn make_name() -> string {
    return "dynamic".upper()
}

fn main() -> int {
    // 1. Vec.push string IDENT → move
    string s1 = "hello".upper()
    Vec(string) v1 = {}
    v1.push(s1)
    print(v1[0])

    // 2. Vec.push string rvalue → temp transfer
    Vec(string) v2 = {}
    v2.push("world".upper())
    print(v2[0])

    // 3. Vec.push string from fn call → rvalue transfer
    Vec(string) v3 = {}
    v3.push(make_name())
    print(v3[0])

    // 4. Vec[i] = string IDENT → move
    Vec(string) v4 = ["aaa".copy(), "bbb".copy()]
    string x = "ccc".copy()
    v4[0] = x
    print(v4[0])

    // 5. enum ctor with string IDENT copy → 存入 enum
    string name = "Bob".upper()
    Value val = Str(name.copy())
    match val {
        Str(sv) => { print(sv) }
        Num(n)  => { print(n) }
        None    => { print("none") }
    }

    // 6. struct ctor with string rvalue
    Person p = Person{name: "Alice".upper(), age: 30}
    print(p.name)

    // 7. Vec(string) swap via index assign
    Vec(string) v5 = ["AAA".copy(), "BBB".copy()]
    string a = v5[0].copy()
    string b = v5[1].copy()
    v5[0] = b
    v5[1] = a
    print(v5[0])
    print(v5[1])

    print("done")
    return 0
}
