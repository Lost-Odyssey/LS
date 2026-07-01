/* Pos: &self readonly method on owned, &Struct, and &!Struct (downgrade). */
struct Point { f64 x; f64 y; }

methods Point {
    def show(&self) {
        @print(self.x)
    }
}

def show_ro(&Point p) {
    p.show()
}

def show_mut(&!Point p) {
    p.show()  /* downgrade &!Struct -> &self */
}

def main() -> int {
    Point p = Point { x: 7.0, y: 2.0 }
    p.show()       /* owned -> &self */
    show_ro(p)     /* auto-borrow */
    show_mut(&!p)
    return 0
}
