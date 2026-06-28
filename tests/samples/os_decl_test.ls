// Test: just declaring ls_os_ftell64 without calling it
extern def ls_os_exec_stderr_len() -> i64
extern def ls_os_ftell64(object fp) -> i64

def main() -> int {
    @print("step1")
    i64 a = ls_os_exec_stderr_len()
    @print(a)
    @print("done")
    return 0
}
