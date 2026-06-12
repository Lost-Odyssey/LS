// phase_h_repro.ls — minimal double-free reproduction: map with has_drop value
import std.json as json
import std.str

fn main() {
    // Parse a small JSON object
    Result(JsonValue, Str) r = json.parse("{\"x\":\"hello\"}")
    JsonValue root
    match r {
        Ok(v) => { root = v }
        Err(e) => { return }
    }

    // Extract the map and access a value — this triggers a deep-clone via map subscript
    match root {
        Object(keys, entries) => {
            Str k = keys.get!(0)     // "x" (borrow-match binder Vec: `[]` unsupported, see plan_std_map §13)
            match entries.get(k) {      // Map.get returns Option(V) (clone of "hello" Str)
                Some(v) => { print("v is a string") }
                None => {}
            }
        }
        _ => { print("not an object") }
    }
    // root goes out of scope here — Object's map entries are dropped
    // but entries[k] already cloned "hello" → double-free
}