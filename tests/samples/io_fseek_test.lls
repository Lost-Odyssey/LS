import std.sys.io as io
import std.sys.os as os

def main() -> int {
    object fp = nil
    @print("before fseek")
    int r = os.raw_fseek64(fp, 0, 0)
    @print(r)
    @print("after fseek")
    return 0
}
