// sink_fstring_show_test.ls — Stage D: f-string interpolation honors Show. A Show
// struct/enum interpolated in f"...{x}..." renders via Show (checker rewrites the
// interp expr to to_str(x)); previously it was a compile error. Mirrors @print()'s
// C-2 rewrite. docs/plan_print_sink.md Stage D.

@derive(Show)
struct P { int x; int y }

@derive(Show)
enum E { A; B(int) }

def main() {
    P p = P { x: 3, y: 4 }
    @print(f"point is {p} done")          // point is P { x: 3, y: 4 } done
    E e = B(9)
    Str s = f"[{e}]"                      // [B(9)]
    @print(s)
    @print(f"{p} and {e} and {42}")       // P { x: 3, y: 4 } and B(9) and 42
    @print("FSTRING SHOW DONE")
}
