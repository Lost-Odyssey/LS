// phase_h_repro.ls — minimal double-free reproduction: map with has_drop value
import std.json as json

fn main() {
    // Parse a small JSON object
    Result(JsonValue, string) r = json.parse("{\"x\":\"hello\"}")
    JsonValue root
    match r {
        Ok(v) => { root = v }
        Err(e) => { return }
    }

    // Extract the map and access a value — this triggers a deep-clone via map subscript
    match root {
        Object(keys, entries) => {
            string k = keys[0]          // "x"
            JsonValue v = entries[k]    // map subscript returns clone of "hello" Str
            print("v is a string")
        }
        _ => { print("not an object") }
    }
    // root goes out of scope here — Object's map entries are dropped
    // but entries[k] already cloned "hello" → double-free
}