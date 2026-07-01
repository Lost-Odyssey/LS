// sink_print_show_test.ls — Stage C-2: @print(x) honors Show. For a struct/enum
// that impls Show, print renders via Show (checker rewrites the arg to to_str(x));
// a plain struct keeps the structural `Name{f=v}` form; POD/Str keep the fast
// path. (Enums render the same either way, so the visible change is on structs.)
// docs/plan_print_sink.md Stage C.

@derive(Show)
struct P { int x; int y }

struct Q { int a; Str tag }     // no Show -> structural

@derive(Show)
enum E { A; B(int) }

def main() {
    P p = P { x: 3, y: 4 }
    @print(p)                     // P { x: 3, y: 4 }   (Show: colon+spaces)
    Q q = Q { a: 7, tag: "hi" }
    @print(q)                     // Q{a=7, tag="hi"}   (structural: =, quoted Str)
    E e = B(9)
    @print(e)                     // B(9)               (Show == structural for enums)
    @print(42, "hi", true)        // 42 hi true         (POD/Str fast path)
    @print("SHOW PRINT DONE")
}
