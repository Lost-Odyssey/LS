import std.json as json

fn main() {
    // Test: parse a simple number
    Result(JsonValue, string) r1 = json.parse("42")
    match r1 {
        Ok(v) => { print(f"num: {json.stringify(v)}") }
        Err(e) => { print(f"ERR: {e}") }
    }

    // Test: parse a simple string
    Result(JsonValue, string) r2 = json.parse("\"hello\"")
    match r2 {
        Ok(v) => { print(f"str: {json.stringify(v)}") }
        Err(e) => { print(f"ERR: {e}") }
    }

    // Test: parse a simple array
    Result(JsonValue, string) r3 = json.parse("[1, 2]")
    match r3 {
        Ok(v) => { print(f"arr: {json.stringify(v)}") }
        Err(e) => { print(f"ERR: {e}") }
    }

    // Test: parse object with single key
    Result(JsonValue, string) r4 = json.parse("{\"x\": 1}")
    match r4 {
        Ok(v) => { print(f"obj1: {json.stringify(v)}") }
        Err(e) => { print(f"ERR: {e}") }
    }

    // Test: parse object with two keys
    Result(JsonValue, string) r5 = json.parse("{\"x\": 1, \"y\": 2}")
    match r5 {
        Ok(v) => { print(f"obj2: {json.stringify(v)}") }
        Err(e) => { print(f"ERR: {e}") }
    }

    print("done")
}
