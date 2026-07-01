/* Pos: &!self method called on owned struct. */
struct Point { f64 x; f64 y; }

methods Point {
    def shift(&!self, f64 dx) {
        self.x = self.x + dx
    }
}

def main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    p.shift(10.0)
    @print(p.x)  /* expect 11 */
    return 0
}
