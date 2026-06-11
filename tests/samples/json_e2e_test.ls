// json_e2e_test.ls — end-to-end JSON file read + iterate keys/values
import io
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
            print(key, "= [", items.len(), " items]")
        }
        Object(ks, entries) => {
            print(key, "= {", ks.len(), " keys}")
        }
    }
}

fn main() {
    // Read JSON file
    // NOTE: no `import std.str` / no explicit Result(Str, Str) annotation here —
    // JsonValue has a variant literally named `Str`, and importing the struct
    // Str alongside std.json breaks top-level signature resolution (known
    // collision, resolves itself when json.ls migrates in P7 #17). Matching the
    // call directly avoids naming the type.
    string raw = ""
    match io.read_file("tests/samples/json_e2e_data.json") {
        Ok(s) => { string ss = s
                   raw = ss }
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
            print("Top-level keys:", keys.len())
            for k in entries.keys() {
                match entries.get(k) {
                    Some(v) => { print_value(k, v) }
                    None => {}
                }
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