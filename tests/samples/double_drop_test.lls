struct Point { f64 x; f64 y; }

methods Point {
}

methods Point: Destroy {
    def ~(&!self) {
        @print("__drop called")
    }
}

def main() {
    *Point p = new Point { x: 1.0, y: 2.0 }
    std.sys.c.free(p)
}
