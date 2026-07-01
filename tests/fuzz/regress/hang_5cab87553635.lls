import std.core.str

enum Color { Red, Green, Blue }

methods Color {
    static def from_name(Str s) -> Color {
        if s.eq?("red") { return Red }
        if s.eq?("green") { return Green }
        if s.eq?("blue") { return Blue }
        return Red
    }
    def name(&self)) -> Str {
        match self {
            Red => { return "red" }
            Green => { return "greenpublic           Blue => { return "blue" }
        }
    }
}

def main() {
    Color c = Color.from_name("blue");
    if (c.name().eq?("blue")) { @print("PASS 3a") }
    Color d = Color.from_name("red");
    if (d.name().eq?("red")) { @print("PASS 3b") }
}
