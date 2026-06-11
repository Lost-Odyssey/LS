import std.str

enum Color { Red, Green, Blue }

impl Color {
    static fn from_name(Str s) -> Color {
        if s.eq?("red") { return Red }
        if s.eq?("green") { return Green }
        if s.eq?("blue") { return Blue }
        return Red
    }
    fn name(&self) -> Str {
        match self {
            Red => { return "red" }
            Green => { return "green" }
            Blue => { return "blue" }
        }
    }
}

fn main() {
    Color c = Color.from_name("blue");
    if (c.name().eq?("blue")) { print("PASS 3a") }
    Color d = Color.from_name("red");
    if (d.name().eq?("red")) { print("PASS 3b") }
}
