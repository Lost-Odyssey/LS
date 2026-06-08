// strbench.ls — built-in string method throughput benchmark.
// Measures the five most-used string methods that allocate new strings
// in tight loops: upper, contains, split, replace, substr. Compares LS
// against C++ std::string / Rust String / Python.
//
//   ls run strbench.ls [n]   /   ls run -O strbench.ls [n]   /   compile

import std.vec
import std.string
import perf
import proc

fn parse_n(int dflt) -> int {
    Vec(string) a = proc.args()
    if a.len() >= 1 {
        Result(int, string) r = a[0].to_int()
        match r { Ok(v) => { return v } Err(e) => { return dflt } }
    }
    return dflt
}

fn main() -> int {
    int n = parse_n(1000000)
    // needles indexed at runtime so O2 can't constant-fold contains/substr
    // (both args are runtime values). Use 5 needles, all present in base.
    Vec(string) needles = {}
    needles.push("Fox")
    needles.push("Dog")
    needles.push("The")
    needles.push("Lazy")
    needles.push("Quick")
    string base = "The Quick Brown Fox Jumps Over The Lazy Dog"

    // ① upper
    i64 t0 = perf.now()
    i64 a1 = 0
    for i in 0..n { string u = base.upper(); a1 = a1 + u.length as i64 }
    i64 t1 = perf.now()

    // ② contains — needle from Vec (runtime, not foldable)
    i64 a2 = 0
    for i in 0..n {
        string nd = needles[i % 5]
        if base.contains(nd) { a2 = a2 + 1 }
    }
    i64 t2 = perf.now()

    // ③ split
    i64 a3 = 0
    for i in 0..n { Vec(string) parts = base.split(" "); a3 = a3 + parts.len() as i64 }
    i64 t3 = perf.now()

    // ④ replace
    i64 a4 = 0
    for i in 0..n { string r = base.replace("o", "0"); a4 = a4 + r.length as i64 }
    i64 t4 = perf.now()

    // ⑤ substr — read a byte from result so O2 can't const-fold the length.
    i64 a5 = 0
    for i in 0..n {
        int p = i % 10
        string s = base.substr(p, 5)
        a5 = a5 + s.at_unsafe(0) as i64
    }
    i64 t5 = perf.now()

    i64 chk = a1 + a2 + a3 + a4 + a5
    print(f"result: {chk}")
    print(f"[@bench] upper     {(t1-t0)/1000} us")
    print(f"[@bench] contains  {(t2-t1)/1000} us")
    print(f"[@bench] split     {(t3-t2)/1000} us")
    print(f"[@bench] replace   {(t4-t3)/1000} us")
    print(f"[@bench] substr    {(t5-t4)/1000} us")
    print(f"[@bench] mean {(t5-t0)/1000} us ({n} iterations)")
    return 0
}
