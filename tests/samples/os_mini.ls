import std.sys.os as os
def main() {
    object r = os.raw_getenv("PATH")
    if r != nil {
        @print("found PATH")
    }
}
