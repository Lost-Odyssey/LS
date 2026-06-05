// proc_args_test.ls — bug #22: proc.args() must work in AOT too.
import proc

fn main() -> int {
    vec(string) a = proc.args()
    print(f"argc_extra={a.length}")
    int i = 0
    while i < a.length {
        print(f"arg[{i}]={a[i]}")
        i = i + 1
    }
    return 0
}
