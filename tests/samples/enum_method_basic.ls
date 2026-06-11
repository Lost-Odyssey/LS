import std.str

enum JsonValue { Null, Bool(bool), Int(int), Float(f64), String(Str) }

impl JsonValue {
    fn is_null(&self) -> bool {
        match self { Null => true, _ => false }
    }
    fn type_name(&self) -> Str {
        match self {
            Null => { return "null" }
            Bool(_) => { return "bool" }
            Int(_) => { return "int" }
            Float(_) => { return "float" }
            String(_) => { return "string" }
        }
    }
}

fn main() {
    JsonValue a = Null;
    JsonValue b = Int(42);
    JsonValue c = Bool(true);
    JsonValue d = String("hello");
    if (a.is_null()) { print("PASS 1a") }
    if (b.type_name().eq?("int")) { print("PASS 1b") }
    if (c.type_name().eq?("bool")) { print("PASS 1c") }
    if (d.type_name().eq?("string")) { print("PASS 1d") }
}
