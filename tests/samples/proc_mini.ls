import std.str
import proc

fn main() -> int {
    print("before exec_full")
    Result(ExecResult, Str) r3 = proc.exec_full("echo stdout_line")
    print("after exec_full")
    match r3 {
        Err(e) => {
            print(f"FAIL: {e}")
            return 1
        }
        Ok(res) => {
            print(f"code={res.exit_code}")
            print(f"stdout={res.stdout}")
            if res.stdout.contains?("stdout_line") && res.exit_code == 0 {
                print("PASS: exec_full")
            } else {
                print("FAIL: exec_full")
                return 1
            }
        }
    }
    return 0
}
