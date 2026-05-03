// Phase B 极端场景压测：vec / map / struct / enum / 嵌套 try / 循环 + owned 借用。
// 期望：跑 `ls run --memcheck` 输出 OK clean。任何 LEAK / DOUBLE FREE 都是真实 bug。

struct Person {
    string name
    int    age
}

enum Tree {
    Leaf
    Node(int, Tree, Tree)
}

fn build_tree(int depth) -> Tree {
    if depth == 0 { return Leaf }
    return Node(depth, build_tree(depth - 1), build_tree(depth - 1))
}

fn sum_tree(Tree t) -> int {
    match t {
        Leaf => 0
        Node(v, l, r) => v + sum_tree(l) + sum_tree(r)
    }
}

fn try_inner(int x) -> Result(string, string) {
    if x < 0 { return Err("negative".upper()) }
    return Ok(f"got {x}")
}

fn try_chain(int x) -> Result(string, string) {
    string s = try try_inner(x)
    return Ok(s + "!")
}

fn break_with_owned(int n) -> int {
    int total = 0
    for i in 0..n {
        string s = f"i={i}"
        if i == 5 { break }
        total = total + s.length
    }
    return total
}

fn main() -> int {
    // ---- vec(string) ----
    vec(string) names = ["alice".upper(), "bob".upper(), "charlie".upper()]
    for n in names { print(n) }

    vec(int) nums = []
    for i in 0..20 { nums.push(i * i) }   // triggers vec.grow (realloc)
    print(nums.length)

    // ---- map(string, int) ----
    map(string, int) ages = {}
    ages.set("alice", 30)
    ages.set("bob",   25)
    ages.set("charlie", 40)
    print(ages.length)
    print(ages.get("alice"))

    // ---- struct with string field (has_drop) ----
    Person p = Person { name: "diana".upper(), age: 28 }
    print(p.name)
    print(p.age)

    // ---- recursive enum + drop chain ----
    Tree t = build_tree(3)
    print(sum_tree(t))                 // 36

    // ---- nested try chain (Err early-return drops Ok payload alloc) ----
    match try_chain(7) {
        Ok(s)  => print(s)             // got 7!
        Err(e) => print(e)
    }
    match try_chain(-1) {
        Ok(s)  => print(s)
        Err(e) => print(e)             // NEGATIVE
    }

    // ---- break inside loop with owned scope vars ----
    print(break_with_owned(10))        // 0+1+2+3+4 totals length sum

    // ---- string concat in loop ----
    string accum = ""
    for i in 0..5 {
        accum = accum + f"[{i}]"
    }
    print(accum)

    return 0
}
