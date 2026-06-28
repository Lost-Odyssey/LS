extern def ls_os_pid() -> int

def main() -> int {
    int p = ls_os_pid()
    @print(p)
    return 0
}
