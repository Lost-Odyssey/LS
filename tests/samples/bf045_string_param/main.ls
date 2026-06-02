/* BF-045 regression: an owned string passed as a `string` parameter, then stored
   into a returned struct's field (or returned directly), aliased the caller's
   buffer (param cap=-2 borrow). After the call the caller freed its temp, so the
   escaped field/return dangled — AOT printed garbage, JIT got a lucky use-after-free.
   Fix: string params are marked borrowed → cg_store_owned + AST_RETURN clone them.
   Must print the real strings under AOT and stay memcheck-clean. */

struct S { string a; int n }

fn of(string s) -> S { return S { a: s, n: 1 } }     // param → struct field → return
fn id(string s) -> string { return s }                // param → return directly

struct W { string label }
impl W { static fn make(string s) -> W { return W { label: s } } }  // static method

fn main() -> int {
    S x = of("hello".upper())
    string y = id("world".upper())
    W w = W.make("widget".upper())

    print(f"x={x.a} y={y} w={w.label}")
    if x.a == "HELLO" && y == "WORLD" && w.label == "WIDGET" {
        print("BF045 PASS")
    }
    return 0
}
