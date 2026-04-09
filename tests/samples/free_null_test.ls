struct Point { f64 x; f64 y; }

impl Point {
    fn __drop() {
        print("__drop called")
    }
}

fn main() {
    *Point p = nil
    free(p)
    print("free(nil) should not crash")
}
