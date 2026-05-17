// Test: extern fn returning i64
extern fn ls_os_exec_stdout_len() -> i64

fn main() -> int {
    print("A")
    i64 n = ls_os_exec_stdout_len()
    print(n)
    print("B")
    return 0
}
