import std.core.vec
import std.core.str

enum JsonValue { Null, Bool(bool), Int(int), String(Str), Array(Vec(JsonValue)) }

methods JsonValue {
    def is_null(&self) -> bool {
        match self { Null => true, _ => false }
    }
    def type_name(&self) -> Str {
        match self {
            Null => { return "null" }
            Bool(_) => { return "bool" }
            Int(_) => { return "int" }
            String(_) => { return "string" }
            Array(_) => { return "array" }
        }
    }
}

def main() {
    JsonValue a = Null;
    JsonValue b = Int(42);
    JsonValue c = String("hello");
    Vec(JsonValue) items = {}
    items.push(Int(1));
    items.push(String("world"));
    JsonValue d = Array(items);
    if (a.is_null()) { @print("PASS 4a") }
    if (b.type_name().eq?("int")) { @print("PASS 4b") }
    if (c.type_name().eq?("string")) { @print("PASS 4c") }
    if (d.type_name().eq?("array")) { @print("PASS 4d") }
}
