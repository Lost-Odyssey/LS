import std.sys.os as os

def main() {
    int n = os.raw_env_count()
    @print(n)
}
