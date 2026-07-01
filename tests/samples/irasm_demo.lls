// Sample for `ls ir` / `ls asm` (per-function codegen inspection).
def square(int x) -> int { return x * x }

struct Point { int x; int y }
methods Point { def area(&self) -> int { return self.x * self.y } }

def main() {
    Point p = Point { x: 3, y: 4 }
    @print(square(p.area()))
}
