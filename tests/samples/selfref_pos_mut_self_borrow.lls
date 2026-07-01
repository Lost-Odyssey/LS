/* Pos: &!self method called through &!Struct param. */
struct Point { f64 x; f64 y; }

methods Point {
    def shift(&!self, f64 dx) {
        self.x = self.x + dx
    }
}

def bump(&!Point p) {
    p.shift(5.0)
}

def main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    bump(&!p)
    @print(p.x)  /* expect 6 */
    return 0
}
