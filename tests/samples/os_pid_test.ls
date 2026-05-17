extern fn ls_os_pid() -> int
extern fn ls_os_fseek64(object fp, i64 off, int origin) -> int

fn main() -> int {
    int p = ls_os_pid()
    print(p)
    print("pid ok")
    int r = ls_os_fseek64(nil, 0, 0)
    print(r)
    print("fseek ok")
    return 0
}
