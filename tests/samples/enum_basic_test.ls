// Phase 8: enum basics — C-style no-payload variants

enum Color {
    Red
    Green
    Blue
}

fn name(Color c) -> int {
    match c {
        Red => 1
        Green => 2
        Blue => 3
    }
}

fn main() -> int {
    Color a = Red
    Color b = Green
    Color c = Blue
    print(name(a))     // 1
    print(name(b))     // 2
    print(name(c))     // 3
    return 0
}
