// field_borrow_test.ls — read-only `&field` / `&element` borrow (the twin of
// the `&!field` writable field borrow). Passing `&obj.field` / `&arr[i]` to a
// read-only `&T` parameter (a plain def or a Block(&T)) lends a zero-copy
// read-only borrow — NO clone of has_drop fields/elements — and the source
// stays alive. Enables zero-clone container iteration (L-006).
import std.core.vec
import std.core.str

def slen(&Str s) -> int { return s.len() }
def vsum(&Vec(int) v) -> int {
    int a = 0
    for i in 0..v.len() { a = a + v[i] }
    return a
}

struct Pair(A, B) { A first; B second }

methods(A, B) Pair(A, B) {
    // &field (has_drop) via a plain read-only def param
    def first_len(&self) -> int { return slen(&self.first) }
    // &field via a read-only Block(&B) param (zero-copy)
    def with_second(R)(&self, Block(&B)->R f) -> R { return f(&self.second) }
}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"FB FAIL {label}") }
}

def main() -> int {
    Pair(Str, Vec(int)) p = {}
    p.first = "hello world"
    p.second.push(10); p.second.push(20); p.second.push(30)

    check(p.first_len() == 11, "field-str-fn")
    check(p.with_second(int)(|v| { return v.len() }) == 3, "field-vec-block-len")
    check(p.with_second(int)(|v| {
        int a = 0
        for i in 0..v.len() { a = a + v[i] }
        return a
    }) == 60, "field-vec-block-sum")

    // sources stay alive after being borrowed
    check(p.first.len() == 11, "source-str-alive")
    check(p.second.len() == 3, "source-vec-alive")

    // &element (array index) read borrow — has_drop Str elements
    array(Str, 3) names = ["alice", "bob", "cy"]
    check(slen(&names[0]) == 5, "elem-str-0")
    check(slen(&names[1]) == 3, "elem-str-1")
    check(slen(&names[2]) == 2, "elem-str-2")

    // &element with a Vec(int) field passed to a read-only def
    check(vsum(&p.second) == 60, "field-vec-fn-sum")

    @print("FIELDBORROW OK")
    return 0
}
