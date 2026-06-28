extern def ls_os_fseek64(object fp, i64 off, int origin) -> int

def main() -> int {
    int r = ls_os_fseek64(nil, 0, 0)
    @print(r)
    return 0
}
