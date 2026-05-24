enum Color { Red, Green, Blue }

impl Color {
    static fn from_name(string s) -> Color {
        match s {
            "red" => Red,
            "green" => Green,
            "blue" => Blue,
            _ => Red,
        }
    }
    fn name(&self) -> string {
        match self {
            Red => "red",
            Green => "green",
            Blue => "blue",
        }
    }
}

fn main() {
    Color c = Color.from_name("blue");
    if (c.name() == "blue") { print("PASS 3a") }
    Color d = Color.from_name("red");
    if (d.name() == "red") { print("PASS 3b") }
}
