enum JsonValue { Null, Bool(bool), Int(int), Float(f64), String(string) }

impl JsonValue {
    fn is_null(&self) -> bool {
        match self { Null => true, _ => false }
    }
    fn type_name(&self) -> string {
        match self {
            Null => "null",
            Bool(_) => "bool",
            Int(_) => "int",
            Float(_) => "float",
            String(_) => "string",
        }
    }
}

fn main() {
    JsonValue a = Null;
    JsonValue b = Int(42);
    JsonValue c = Bool(true);
    JsonValue d = String("hello");
    if (a.is_null()) { print("PASS 1a") }
    if (b.type_name() == "int") { print("PASS 1b") }
    if (c.type_name() == "bool") { print("PASS 1c") }
    if (d.type_name() == "string") { print("PASS 1d") }
}
