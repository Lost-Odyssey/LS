struct Person {
    int age;
    string name;
}

fn main() -> int {
    // ── Case 1: string s = t (var_decl from string IDENT) ──────────────
    string t = "hello".upper()
    string s = t
    s = "world"
    print(t)        // expect: HELLO  (t is independent, unchanged)
    print(s)        // expect: world

    // ── Case 2: Person p2 = p1 (var_decl from struct IDENT) ─────────────
    Person p1 = Person{age:10, name:"Alice".upper()}
    Person p2 = p1
    p2.name = "Bob"
    print(p1)       // expect: Person{age=10, name=ALICE}  (p1 independent)
    print(p2)       // expect: Person{age=10, name=Bob}

    // ── Case 3: array(Person,2)[i] read ─────────────────────────────────
    array(Person, 2) arr = [Person{age:1, name:"Carol".upper()}, Person{age:2, name:"Dave"}]
    Person p3 = arr[0]
    p3.name = "Eve"
    print(arr[0])   // expect: Person{age=1, name=CAROL}  (arr[0] independent)
    print(p3)       // expect: Person{age=1, name=Eve}

    // ── Case 4: string s2 = p.name (struct field read) ───────────────────
    Person base = Person{age:30, name:"Frank".upper()}
    string fname = base.name
    fname = "Grace"
    print(base)     // expect: Person{age=30, name=FRANK}  (base.name independent)
    print(fname)    // expect: Grace

    return 0
}
