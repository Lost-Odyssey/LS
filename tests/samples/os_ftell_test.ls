extern fn ls_os_ftell64(object fp) -> i64

fn main() -> int {
    print("A")
    i64 t = ls_os_ftell64(nil)
    print(t)
    print("B")
    return 0
}
