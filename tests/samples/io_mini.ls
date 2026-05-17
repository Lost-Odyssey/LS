extern fn ls_os_fseek64(object fp, i64 off, int origin) -> int
extern fn ls_os_pid() -> int

fn main() {
    print("A")
    int pid = ls_os_pid()
    print("B")
    int r = ls_os_fseek64(nil, 0, 0)
    print("C")
}
