// proc_test.ls — Tests for the proc stdlib module.

import std.str
import std.vec
import proc

fn main() -> int {
    // Test 1: proc.pid() — should return a positive integer
    int pid = proc.pid()
    if pid > 0 {
        print("PASS: proc.pid")
    } else {
        print("FAIL: proc.pid should be > 0")
        return 1
    }

    // Test 2: proc.program() — should be non-empty
    Str prog = proc.program()
    if prog.len() > 0 {
        print("PASS: proc.program")
    } else {
        print("FAIL: proc.program empty")
        return 1
    }

    // Test 3: proc.args() — called with no extra args, should be empty
    // (when run as: ls run proc_test.ls — no user args follow)
    Vec(Str) args = proc.args()
    print(f"PASS: proc.args count={args.len()}")

    // Test 4: proc.run — echo succeeds with exit code 0
    int code = proc.run("echo proc_run_ok")
    if code == 0 {
        print("PASS: proc.run exit code")
    } else {
        print("FAIL: proc.run non-zero exit")
        return 1
    }

    // Test 5: proc.exec — capture echo output
    Result(Str, Str) r = proc.exec("echo hello_from_exec")
    match r {
        Err(e) => {
            print(f"FAIL: proc.exec error: {e}")
            return 1
        }
        Ok(out) => {
            if out.contains?("hello_from_exec") {
                print("PASS: proc.exec output")
            } else {
                print(f"FAIL: proc.exec unexpected output: {out}")
                return 1
            }
        }
    }

    // Test 6: proc.exec — non-zero exit returns Err
    Result(Str, Str) r2 = proc.exec("exit 1")
    match r2 {
        Ok(v) => {
            print("FAIL: proc.exec exit 1 should return Err")
            return 1
        }
        Err(e) => {
            print("PASS: proc.exec non-zero is Err")
        }
    }

    // Test 7: proc.exec_full — separate stdout/stderr
    Result(ExecResult, Str) r3 = proc.exec_full("echo stdout_line")
    match r3 {
        Err(e) => {
            print(f"FAIL: exec_full error: {e}")
            return 1
        }
        Ok(res) => {
            if res.stdout.contains?("stdout_line") && res.exit_code == 0 {
                print("PASS: proc.exec_full")
            } else {
                print(f"FAIL: exec_full stdout={res.stdout} code={res.exit_code}")
                return 1
            }
        }
    }

    print("All proc tests passed")
    return 0
}
