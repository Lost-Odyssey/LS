// negative: the destructor can never be called manually (would double-free).
struct F { Str t }
methods F: Destroy {
    def ~(&!self) { }
}
def main() -> int {
    F f = F { t: "x" }
    f.__drop()
    return 0
}
