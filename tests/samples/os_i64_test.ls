extern def ls_os_fseek64(object fp, i64 off, int origin) -> int
extern def ls_os_ftell64(object fp) -> i64

def main() -> int {
    @print("A")
    i64 off = 0
    int r = ls_os_fseek64(nil, off, 0)
    @print(r)
    @print("B")
    i64 t = ls_os_ftell64(nil)
    @print(t)
    @print("C")
    return 0
}
