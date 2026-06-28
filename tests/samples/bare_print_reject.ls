// bare_print_reject.ls — Stage F: bare `print` is retired. The print intrinsic is
// ONLY spelled `@print` (a dedicated @-token, like @time/@bench). A bare `print(...)`
// resolves to nothing -> a clear "undefined variable 'print'" compile error.
// docs/plan_print_sink.md Stage F.
def main() {
    print("bare print is retired — use @print")
}
