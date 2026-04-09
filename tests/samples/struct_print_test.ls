module main

struct Point {
    int x;
    int y;
}

fn main() -> int {
    *Point p1 = malloc(8 as i64) as *Point
    p1.x = 1
    p1.y = 2
    print(p1.x)
    print(p1.y)
    print(p1)
    free(p1 as *u8)
    
    Point p2
    p2.x = 3
    p2.y = 4
    print(p2)
    return 1
}
