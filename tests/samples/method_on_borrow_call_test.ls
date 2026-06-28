// Regression: a method call whose RECEIVER is a borrow-returning call result —
// `v.get_ref(i).method(args)` where get_ref returns &T. The receiver being an
// AST_CALL of reference type used to fall through the instance-method dispatch
// (the deref logic only unwrapped *T pointers, not &T references), so `self`
// was counted as a missing explicit argument →
//   "wrong number of arguments: expected N, got N-1".
// Covers struct + enum pointees, with-arg + no-arg + chained-owned methods, and
// the named-temp control path (which always worked). JIT + AOT + memcheck 0/0/0.
import std.core.str
import std.core.vec

enum Shape {
    Circle(int)
    Square(int)
}

methods Shape {
    def area(&self) -> int {
        match self {
            Circle(r) => { return r * r * 3 }
            Square(s) => { return s * s }
        }
    }
}

def expect_true(bool cond, &Str label) {
    if cond { @print(f"ok {label}") } else { @print(f"FAIL {label}") }
}

def main() {
    Vec(Str) words = []
    words.push("hello")
    words.push("world")
    Str key = "hello"

    // (1) struct pointee, method WITH an argument — the exact bug shape.
    expect_true(words.get_ref(0).eq?(key), "struct method-with-arg on call result")

    // (2) struct pointee, no-arg method on borrow-return receiver.
    expect_true(words.get_ref(1).len() == 5, "struct no-arg method on call result")

    // (3) chained — borrow-return receiver, method returns an owned Str.
    Str up = words.get_ref(0).upper()
    expect_true(up.eq?("HELLO"), "chained owned-returning method on call result")

    // (4) enum pointee, method on borrow-return receiver.
    Vec(Shape) shapes = []
    shapes.push(Circle(2))
    shapes.push(Square(3))
    expect_true(shapes.get_ref(0).area() == 12, "enum method on call result")
    expect_true(shapes.get_ref(1).area() == 9,  "enum method on call result 2")

    // (5) control: named-temp form (the documented workaround) still works.
    &Str k = words.get_ref(1)
    expect_true(k.eq?("world"), "named-temp borrow receiver")

    @print("METHOD ON BORROW CALL PASS")
}
