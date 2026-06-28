// test_proc_args.ls
// Tests: proc.args() returns all user-supplied arguments (BF-036).
// Run as: ls run test_proc_args.ls hello world 123
// Expected output (3 lines):
//   hello
//   world
//   123

import std.core.str
import std.core.vec
import std.sys.proc as proc

def main() -> int {
    Vec(Str) a = proc.args()
    a.each(|s| { @print(s) })
    return 0
}
