struct Point { f64 x; f64 y; }

impl Point {
    fn __drop() {
        print("__drop called")
    }
}

fn main() {
    *Point p = new Point { x: 1.0, y: 2.0 }
    std.c.free(p)
}
