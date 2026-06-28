// sink_atprint_test.ls — Stage F: `@print` is the print intrinsic — a dedicated
// `@`-token like @time/@bench (the ONLY spelling; bare `print` is retired and is
// an "undefined" error). It composes with the C-2 Show dispatch + D f-string Show
// + C-1 redirect. docs/plan_print_sink.md Stage F.

@derive(Show)
struct P { int x; int y }

def main() {
    @print("at-print works")
    @print(42, true, "mixed")
    P p = P { x: 3, y: 4 }
    @print(p)                       // P { x: 3, y: 4 }  (C-2 via @print)
    @print(f"interp {p}")           // interp P { x: 3, y: 4 }  (D)
    @print("ATPRINT DONE")
}
