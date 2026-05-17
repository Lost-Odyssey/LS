extern fn ls_os_pid() -> int

fn main() -> int {
    int p = ls_os_pid()
    print(p)
    return 0
}
