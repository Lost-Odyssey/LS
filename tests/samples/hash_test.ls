// hash_test.ls — std.map prereq M-H: the `Hash` trait + FxHash hasher.
// Verifies: stable hashes (int/string), distinct keys differ, good high-bit
// distribution under the Fibonacci scatter the Map will use, and trait-bound
// dispatch `x.hash()` through `where T: Hash` in a generic struct method.
// See docs/plan_std_map.md §3. JIT + AOT + memcheck 0/0/0.

import std.hash

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// Fibonacci scatter onto [0, 2^bits): the index mapping the Map uses (§5.1).
// Uses the HIGH bits of (h * FIB), which is where FxHash entropy lives.
fn scatter(u64 h, u64 shift) -> int {
    u64 FIB = 11400714819323198485 as u64   // 0x9E3779B97F4A7C15
    return ((h * FIB) >> shift) as int
}

struct Box(T) { T val }
impl(T) Box(T) {
    // Realistic Map usage: hash an owned element through the trait bound.
    fn h(&self) -> u64 where T: Hash {
        return self.val.hash()
    }
}

fn main() {
    // ---- stability: same key → same hash ----
    check(42.hash() == 42.hash(), "int hash stable")
    string s = "hello"
    check(s.hash() == "hello".hash(), "string hash stable (var vs literal)")

    // ---- distinctness: different keys → different hashes ----
    check(1.hash() != 2.hash(), "int 1 != 2")
    check("foo".hash() != "bar".hash(), "string foo != bar")
    check("".hash() != "a".hash(), "empty vs non-empty string")

    // ---- bool / char impls exist and are stable ----
    check(true.hash() != false.hash(), "bool true != false")
    check('A'.hash() == 'A'.hash(), "char hash stable")

    // ---- high-bit distribution: 64 distinct ints into 16 buckets ----
    array(int, 16) cnt
    int z = 0
    while z < 16 { cnt[z] = 0; z = z + 1 }
    int i = 0
    while i < 64 {
        int b = scatter(i.hash(), 60 as u64)
        cnt[b] = cnt[b] + 1
        i = i + 1
    }
    int nonempty = 0
    int j = 0
    while j < 16 { if cnt[j] > 0 { nonempty = nonempty + 1 } j = j + 1 }
    check(nonempty >= 14, "int hash spreads across buckets")

    // ---- trait-bound dispatch through a generic method ----
    Box(int) bi = Box(int){ val: 7 }
    check(bi.h() == 7.hash(), "where T: Hash dispatch (int)")
    Box(string) bs = Box(string){ val: "k" }
    check(bs.h() == "k".hash(), "where T: Hash dispatch (string)")

    print("HASH PASS")
}
