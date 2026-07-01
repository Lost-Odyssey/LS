// Phase B 极端场景压测：Vec / map / struct / enum / 嵌套 try / 循环 + owned 借用。
// 期望：跑 `ls run --memcheck` 输出 OK clean。任何 LEAK / DOUBLE FREE 都是真实 bug。

import std.core.vec
import std.core.str
import std.core.map

struct Person {
    Str name
    int    age
}

enum Tree {
    Leaf
    Node(int, Tree, Tree)
}

def build_tree(int depth) -> Tree {
    if depth == 0 { return Leaf }
    return Node(depth, build_tree(depth - 1), build_tree(depth - 1))
}

def sum_tree(Tree t) -> int {
    match t {
        Leaf => 0
        Node(v, l, r) => v + sum_tree(l) + sum_tree(r)
    }
}

def try_inner(int x) -> Result(Str, Str) {
    if x < 0 { return Err("negative".upper()) }
    return Ok(f"got {x}")
}

def try_chain(int x) -> Result(Str, Str) {
    Str s = try try_inner(x)
    return Ok(s + "!")
}

def break_with_owned(int n) -> int {
    int total = 0
    for i in 0..n {
        Str s = f"i={i}"
        if i == 5 { break }
        total = total + s.len()
    }
    return total
}

def main() -> int {
    // ---- Vec(Str) ----
    Vec(Str) names = ["alice".upper(), "bob".upper(), "charlie".upper()]
    for n in names { @print(n) }

    Vec(int) nums = {}
    for i in 0..20 { nums.push(i * i) }   // triggers Vec.grow (realloc)
    @print(nums.len())

    // ---- Map(Str, int) ----
    Map(Str, int) ages = {}
    ages.set("alice", 30)
    ages.set("bob",   25)
    ages.set("charlie", 40)
    @print(ages.len())
    match ages.get("alice") {
        Some(age) => { @print(age) }
        None => { @print(-1) }
    }

    // ---- struct with Str field (has_drop) ----
    Person p = Person { name: "diana".upper(), age: 28 }
    @print(p.name)
    @print(p.age)

    // ---- recursive enum + drop chain ----
    Tree t = build_tree(3)
    @print(sum_tree(t))                 // 36

    // ---- nested try chain (Err early-return drops Ok payload alloc) ----
    match try_chain(7) {
        Ok(s)  => @print(s)             // got 7!
        Err(e) => @print(e)
    }
    match try_chain(-1) {
        Ok(s)  => @print(s)
        Err(e) => @print(e)             // NEGATIVE
    }

    // ---- break inside loop with owned scope vars ----
    @print(break_with_owned(10))        // 0+1+2+3+4 totals length sum

    // ---- Str concat in loop ----
    Str accum = ""
    for i in 0..5 {
        accum = accum + f"[{i}]"
    }
    @print(accum)

    return 0
}
