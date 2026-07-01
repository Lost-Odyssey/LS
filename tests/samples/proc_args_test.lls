// proc_args_test.ls — bug #22: proc.args() must work in AOT too.
import std.core.str
import std.core.vec
import std.sys.proc as proc

def main() -> int {
    Vec(Str) a = proc.args()
    @print(f"argc_extra={a.len()}")
    int i = 0
    while i < a.len() {
        @print(f"arg[{i}]={a[i]}")
        i = i + 1
    }
    return 0
}
