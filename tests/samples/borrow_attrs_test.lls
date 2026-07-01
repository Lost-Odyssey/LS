// #1 borrow → LLVM param attributes (docs/plan_borrow_noalias.md).
// Exercises every attributed borrow form so the attrs (nonnull/dereferenceable/
// align/readonly/nocapture) can't silently miscompile. Correctness is the gate;
// the IR attributes are verified manually in the design doc.
import std.core.vec

struct Acc { int sum; int n }

methods Acc {
    def add(&!self, int x) {        // &!self exclusive borrow (writes self)
        self.sum = self.sum + x
        self.n = self.n + 1
    }
    def mean(&self) -> int {        // &self read-only borrow (readonly attr)
        if self.n == 0 { return 0 }
        return self.sum / self.n
    }
}

def sum_vec(&Vec(int) v) -> int {   // &T read-only aggregate borrow
    int s = 0
    for i in 0..v.len() { s = s + v.get!(i) }
    return s
}

def scale_into(&!Vec(int) dst, &Vec(int) src, int k) {  // &!dst + &src (the noalias case)
    for i in 0..src.len() { dst.push(src.get!(i) * k) }
}

def main() {
    Acc a = Acc{sum: 0, n: 0}
    for i in 1..6 { a.add(i) }     // 1+2+3+4+5 = 15, n=5
    int m = a.mean()               // 15/5 = 3

    Vec(int) src = {}
    for i in 1..6 { src.push(i) }  // [1,2,3,4,5]
    int s = sum_vec(&src)          // 15

    Vec(int) dst = {}
    scale_into(&!dst, &src, 10)    // [10,20,30,40,50]
    int s2 = sum_vec(&dst)         // 150

    bool ok = true
    if m != 3 { ok = false }
    if s != 15 { ok = false }
    if s2 != 150 { ok = false }
    if ok {
        @print("BORROWATTR PASS")
    } else {
        @print(f"BORROWATTR FAIL m={m} s={s} s2={s2}")
    }
}
