// Test extern def taking i64 param
extern def ls_os_exec_stderr_len() -> i64  // works (no params, returns i64)
extern def ls_os_ftell64(object fp) -> i64  // crashes (obj param, returns i64)

def main() -> int {
    @print("step1")
    i64 a = ls_os_exec_stderr_len()
    @print(a)
    @print("step2")
    i64 b = ls_os_ftell64(nil)
    @print(b)
    @print("step3")
    return 0
}
