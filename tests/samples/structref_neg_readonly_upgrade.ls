/* Neg: &struct cannot be upgraded to &!struct. */
struct Point { f64 x; f64 y; }

def sink(&!Point p) { p.x = 0.0 }

def forward(&Point p) {
    sink(&!p)
}

def main() -> int {
    Point p = Point { x: 1.0, y: 2.0 }
    forward(p)
    return 0
}
