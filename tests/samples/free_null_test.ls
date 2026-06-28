struct Point { f64 x; f64 y; }

methods Point {
}

methods Point: Destroy {
    def ~(&!self) {
        @print("__drop called")
    }
}

def main() {
    *Point p = nil
    std.sys.c.free(p)
    @print("free(nil) should not crash")
}
