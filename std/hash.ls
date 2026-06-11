// std/hash.ls — the `Hash` trait + a default FxHash hasher.
//
// std.map prerequisite (mini-phase M-H, docs/plan_std_map.md §3). The builtin
// `map` hard-codes its hash in C; a pure-LS `Map(K,V)` needs a user-reachable
// hash. This file provides:
//
//   * trait Hash { fn hash(&self) -> u64 }
//   * impl Hash for the builtin hashable types (int / i64 / bool / char / string)
//   * FxHash mixing primitives (fx_mix / fxhash_str) for impls to reuse.
//
// Usage: `import std.hash`, then call `k.hash()` on any impl'd type, or bound a
// generic on `where K: Hash`. FxHash is the v1 hasher (rustc's hasher): cheap,
// good avalanche on small keys. wyhash is a possible v2 upgrade.

// FxHash seed (rustc FxHasher's multiplier): 0x517cc1b727220a95.
fn fx_seed() -> u64 { return 0x517cc1b727220a95 as u64 }

// Mix one 64-bit word into the running hash:
//   h = rotate_left(h ^ word, 5) * SEED
// Shift amounts are cast to u64 so both shl/lshr operands share the i64 width.
fn fx_mix(u64 h, u64 word) -> u64 {
    u64 x = h ^ word
    u64 r = (x << (5 as u64)) | (x >> (59 as u64))
    return r * fx_seed()
}

// Byte-wise FxHash over a string's bytes (v1: one byte per step; an 8-byte-word
// variant is a possible v2 speedup). Plain by-value string param = runtime
// cap=-2 borrow, no copy (P4 removed the `&string` parameter type).
fn fxhash_str(string s) -> u64 {
    u64 h = 0 as u64
    int n = s.length
    int i = 0
    while i < n {
        u64 b = s.at_unsafe(i) as u64
        h = fx_mix(h, b)
        i = i + 1
    }
    return h
}

trait Hash {
    fn hash(&self) -> u64
}

impl Hash for int {
    fn hash(&self) -> u64 { return fx_mix(0 as u64, self as u64) }
}

impl Hash for i64 {
    fn hash(&self) -> u64 { return fx_mix(0 as u64, self as u64) }
}

impl Hash for char {
    fn hash(&self) -> u64 { return fx_mix(0 as u64, self as u64) }
}

impl Hash for bool {
    fn hash(&self) -> u64 {
        u64 w = 0 as u64
        if self { w = 1 as u64 }
        return fx_mix(0 as u64, w)
    }
}

impl Hash for string {
    fn hash(&self) -> u64 { return fxhash_str(self) }
}

// NOTE: `impl Hash for Str` lives in std/str.ls, NOT here: a trait impl for a
// user struct must be emitted in the type's own module so the method symbol
// gets the type's llvm_name prefix (std_str__Str.hash); emitting it here would
// produce std_hash__Str.hash and JIT "Symbols not found" at call sites.
