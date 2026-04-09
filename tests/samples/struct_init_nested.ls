module main

struct S1 {
    i64 k;
}

struct Point {
    int x;
    int y;
    S1 s;
}

fn main() -> int {
    *Point p1 = new Point {x:100, y:200, s: S1{k: 3 as i64}}
    print(p1.x)
    print(p1.y)
    print(p1.s.k)
    print(*p1)
    return 1
}
