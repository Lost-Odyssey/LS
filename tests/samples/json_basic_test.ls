import std.json as json
import std.str

fn main() {
    // ---- Test 1: Parse null ----
    Result(JsonValue, Str) r1 = json.parse("null")
    match r1 {
        Ok(v) => {
            if json.is_null(v) { print("PASS 1: null") }
            else { print("FAIL 1: not null") }
        }
        Err(e) => { print(f"FAIL 1: {e}") }
    }

    // ---- Test 2: Parse true/false ----
    Result(JsonValue, Str) r2 = json.parse("true")
    match r2 {
        Ok(v) => {
            Result(bool, Str) bv = json.as_bool(v)
            match bv {
                Ok(b) => {
                    if b { print("PASS 2: true") }
                    else { print("FAIL 2: expected true") }
                }
                Err(e) => { print(f"FAIL 2: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 2: {e}") }
    }

    Result(JsonValue, Str) r2b = json.parse("false")
    match r2b {
        Ok(v) => {
            Result(bool, Str) bv = json.as_bool(v)
            match bv {
                Ok(b) => {
                    if !b { print("PASS 2b: false") }
                    else { print("FAIL 2b: expected false") }
                }
                Err(e) => { print(f"FAIL 2b: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 2b: {e}") }
    }

    // ---- Test 3: Parse number ----
    Result(JsonValue, Str) r3 = json.parse("42")
    match r3 {
        Ok(v) => {
            Result(int, Str) iv = json.as_int(v)
            match iv {
                Ok(n) => {
                    if n == 42 { print("PASS 3: int 42") }
                    else { print(f"FAIL 3: got {n}") }
                }
                Err(e) => { print(f"FAIL 3: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 3: {e}") }
    }

    // ---- Test 4: Parse float ----
    Result(JsonValue, Str) r4 = json.parse("3.14")
    match r4 {
        Ok(v) => {
            Result(f64, Str) fv = json.as_number(v)
            match fv {
                Ok(n) => { print(f"PASS 4: float {n}") }
                Err(e) => { print(f"FAIL 4: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 4: {e}") }
    }

    // ---- Test 5: Parse string ----
    Result(JsonValue, Str) r5 = json.parse("\"hello world\"")
    match r5 {
        Ok(v) => {
            Result(Str, Str) sv = json.as_string(v)
            match sv {
                Ok(s) => { print(f"PASS 5: string={s}") }
                Err(e) => { print(f"FAIL 5: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 5: {e}") }
    }

    // ---- Test 6: Parse array ----
    Result(JsonValue, Str) r6 = json.parse("[1, 2, 3]")
    match r6 {
        Ok(v) => {
            if json.is_array(v) { print("PASS 6: array") }
            else { print("FAIL 6: not array") }
        }
        Err(e) => { print(f"FAIL 6: {e}") }
    }

    // ---- Test 7: Parse object ----
    Result(JsonValue, Str) r7 = json.parse("{\"name\": \"Alice\", \"age\": 30}")
    match r7 {
        Ok(v) => {
            if json.is_object(v) { print("PASS 7: object") }
            else { print("FAIL 7: not object") }
        }
        Err(e) => { print(f"FAIL 7: {e}") }
    }

    // ---- Test 8: Stringify basic types ----
    JsonValue n = json.null_val()
    print(f"PASS 8: stringify null={json.stringify(n)}")

    JsonValue b = json.bool_val(true)
    print(f"PASS 8b: stringify true={json.stringify(b)}")

    JsonValue num = json.number_int(42)
    print(f"PASS 8c: stringify 42={json.stringify(num)}")

    JsonValue s = json.str_val("hello")
    print(f"PASS 8d: stringify str={json.stringify(s)}")

    // ---- Test 9: Parse error ----
    Result(JsonValue, Str) r9 = json.parse("{invalid}")
    match r9 {
        Ok(v) => { print("FAIL 9: should have failed") }
        Err(e) => { print(f"PASS 9: error={e}") }
    }

    // ---- Test 10: Empty array/object ----
    Result(JsonValue, Str) r10a = json.parse("[]")
    match r10a {
        Ok(v) => {
            if json.is_array(v) { print("PASS 10a: empty array") }
            else { print("FAIL 10a") }
        }
        Err(e) => { print(f"FAIL 10a: {e}") }
    }

    Result(JsonValue, Str) r10b = json.parse("{}")
    match r10b {
        Ok(v) => {
            if json.is_object(v) { print("PASS 10b: empty object") }
            else { print("FAIL 10b") }
        }
        Err(e) => { print(f"FAIL 10b: {e}") }
    }

    // ---- Test 11: Nested structure ----
    Str nested = "{\"users\": [{\"name\": \"Bob\", \"active\": true}], \"count\": 1}"
    Result(JsonValue, Str) r11 = json.parse(nested)
    match r11 {
        Ok(v) => {
            print(f"PASS 11: nested parse ok, stringify={json.stringify(v)}")
        }
        Err(e) => { print(f"FAIL 11: {e}") }
    }

    // ---- Test 12: Stringify pretty ----
    Result(JsonValue, Str) r12 = json.parse("{\"a\": 1, \"b\": [2, 3]}")
    match r12 {
        Ok(v) => {
            Str pretty = json.stringify_pretty(v, 2)
            print("PASS 12: pretty=")
            print(pretty)
        }
        Err(e) => { print(f"FAIL 12: {e}") }
    }

    // ---- Test 13: Negative number ----
    Result(JsonValue, Str) r13 = json.parse("-42.5")
    match r13 {
        Ok(v) => {
            Result(f64, Str) fv = json.as_number(v)
            match fv {
                Ok(n) => { print(f"PASS 13: negative={n}") }
                Err(e) => { print(f"FAIL 13: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 13: {e}") }
    }

    // ---- Test 14: String with escapes ----
    Result(JsonValue, Str) r14 = json.parse("\"hello\\nworld\"")
    match r14 {
        Ok(v) => {
            Result(Str, Str) sv = json.as_string(v)
            match sv {
                Ok(s) => { print(f"PASS 14: escaped len={s.len()}") }
                Err(e) => { print(f"FAIL 14: {e}") }
            }
        }
        Err(e) => { print(f"FAIL 14: {e}") }
    }

    print("done")
}
