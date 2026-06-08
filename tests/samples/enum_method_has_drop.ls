import std.vec

enum JsonValue { Null, Bool(bool), Int(int), String(string), Array(Vec(JsonValue)) }

impl JsonValue {
    fn is_null(&self) -> bool {
        match self { Null => true, _ => false }
    }
    fn type_name(&self) -> string {
        match self {
            Null => "null",
            Bool(_) => "bool",
            Int(_) => "int",
            String(_) => "string",
            Array(_) => "array",
        }
    }
}

fn main() {
    JsonValue a = Null;
    JsonValue b = Int(42);
    JsonValue c = String("hello");
    Vec(JsonValue) items = {}
    items.push(Int(1));
    items.push(String("world"));
    JsonValue d = Array(items);
    if (a.is_null()) { print("PASS 4a") }
    if (b.type_name() == "int") { print("PASS 4b") }
    if (c.type_name() == "string") { print("PASS 4c") }
    if (d.type_name() == "array") { print("PASS 4d") }
}
