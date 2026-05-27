// M-3 验证：统一所有权转移 API 在各站点的正确性
// NOTE: vec(has_drop struct) 的 drop 正确性在 M-4 测试中覆盖

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
    // 1. vec.push string IDENT → move
    string s1 = "hello".upper()
    vec(string) v1 = []
    v1.push(s1)
    print(v1[0])

    // 2. vec.push string rvalue → temp transfer
    vec(string) v2 = []
    v2.push("world".upper())
    print(v2[0])

    // 3. vec.push string from fn call → rvalue transfer
    vec(string) v3 = []
    v3.push(make_name())
    print(v3[0])

    // 4. vec[i] = string IDENT → move
    vec(string) v4 = ["aaa".copy(), "bbb".copy()]
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

    // 7. vec(string) swap via index assign
    vec(string) v5 = ["AAA".copy(), "BBB".copy()]
    string a = v5[0].copy()
    string b = v5[1].copy()
    v5[0] = b
    v5[1] = a
    print(v5[0])
    print(v5[1])

    print("done")
    return 0
}
