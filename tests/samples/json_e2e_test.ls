// json_e2e_test.ls — end-to-end JSON file read + iterate keys/values
import std.sys.io as io
import std.text.json as json
import std.core.str

def print_value(Str key, JsonValue val) {
    match val {
        Null => {
            @print(key, "= null")
        }
        Bool(b) => {
            @print(key, "=", b)
        }
        Number(n) => {
            @print(key, "=", n)
        }
        Text(s) => {
            @print(key, "=", s)
        }
        Array(items) => {
            @print(key, "= [", items.len(), " items]")
        }
        Object(ks, entries) => {
            @print(key, "= {", ks.len(), " keys}")
        }
    }
}

def main() {
    // Read JSON file (io and json are both Str-based now; the old variant-name
    // collision is gone — JsonValue's string variant was renamed to `Text`).
    Str raw = ""
    match io.read_file("tests/samples/json_e2e_data.json") {
        Ok(s) => { raw = s }
        Err(e) => {
            @print("read_file error:", e)
            return
        }
    }

    // Parse
    JsonValue root
    Result(JsonValue, Str) rp = json.parse(raw)
    match rp {
        Ok(v) => { root = v }
        Err(e) => {
            @print("parse error:", e)
            return
        }
    }

    // Walk the top-level object
    match root {
        Object(keys, entries) => {
            @print("Top-level keys:", keys.len())
            for k in entries.keys() {
                match entries.get(k) {
                    Some(v) => { print_value(k, v) }
                    None => {}
                }
            }
        }
        _ => {
            @print("expected object, got something else")
        }
    }

    // Round-trip via stringify
    Str rt = json.stringify(root)
    @print("round-trip:", rt)
}
