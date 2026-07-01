// hash_neg_test.ls — M-H negative: a `where T: Hash` bound must be rejected at
// compile time when T has no `impl Hash`. The checker must emit a clear error
// ("does not implement Hash") and exit non-zero.

import std.core.hash

struct NoHash { int x }

struct Box(T) { T val }
methods Box(T) {
    def h(&self) -> u64 where T: Hash {
        return self.val.hash()
    }
}

def main() {
    Box(NoHash) b = Box(NoHash){ val: NoHash{ x: 1 } }
    u64 z = b.h()
    @print(z as int)
}
