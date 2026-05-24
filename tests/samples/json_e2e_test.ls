// json_e2e_test.ls — end-to-end JSON file read + iterate keys/values
import std.io as io
import std.json as json

fn print_value(string key, JsonValue val) {
    match val {
        Null => {
            print(key, "= null")
        }
        Bool(b) => {
            print(key, "=", b)
        }
        Number(n) => {
            print(key, "=", n)
        }
        Str(s) => {
            print(key, "=", s)
        }
        Array(items) => {
            print(key, "= [", items.length, " items]")
        }
        Object(ks, entries) => {
            print(key, "= {", ks.length, " keys}")
        }
    }
}

fn main() {
    // Read JSON file
    string raw
    Result(string, string) rraw = io.read_file("tests/samples/json_e2e_data.json")
    match rraw {
        Ok(s) => { raw = s }
        Err(e) => {
            print("read_file error:", e)
            return
        }
    }

    // Parse
    JsonValue root
    Result(JsonValue, string) rp = json.parse(raw)
    match rp {
        Ok(v) => { root = v }
        Err(e) => {
            print("parse error:", e)
            return
        }
    }

    // Walk the top-level object
    match root {
        Object(keys, entries) => {
            print("Top-level keys:", keys.length)
            int i = 0
            while i < keys.length {
                string k = keys[i]
                JsonValue v = entries[k]
                print_value(k, v)
                i = i + 1
            }
        }
        _ => {
            print("expected object, got something else")
        }
    }

    // Round-trip via stringify
    string rt = json.stringify(root)
    print("round-trip:", rt)
}