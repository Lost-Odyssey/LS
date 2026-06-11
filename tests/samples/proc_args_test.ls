// proc_args_test.ls — bug #22: proc.args() must work in AOT too.
import std.str
import std.vec
import proc

fn main() -> int {
    Vec(Str) a = proc.args()
    print(f"argc_extra={a.len()}")
    int i = 0
    while i < a.len() {
        print(f"arg[{i}]={a[i]}")
        i = i + 1
    }
    return 0
}
