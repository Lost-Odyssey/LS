// json_file_test.ls
// Integration test: parse a medium-sized JSON string, stringify it,
// write to a temp file via io, read back, parse again, verify round-trip.
// Also exercises the new navigation API (array_len / object_len / object_has / object_keys).

import std.vec
import std.json as json
import std.str
import io

fn main() {
    // ---- Medium-sized JSON (embedded as string literal) ----
    // ~20 values: nested objects, arrays, strings, numbers, booleans, null
    Str src = "{\"name\":\"test_dataset\",\"version\":2,\"active\":true,\"score\":98.6,\"tags\":[\"alpha\",\"beta\",\"gamma\"],\"config\":{\"debug\":false,\"timeout\":30,\"retries\":3},\"users\":[{\"id\":1,\"name\":\"Alice\",\"role\":\"admin\"},{\"id\":2,\"name\":\"Bob\",\"role\":\"user\"},{\"id\":3,\"name\":\"Carol\",\"role\":\"user\"}],\"metadata\":null}"

    // ---- Test 1: parse medium JSON ----
    Result(JsonValue, Str) r1 = json.parse(src)
    match r1 {
        Err(e) => { print(f"FAIL 1: parse error={e}"); return }
        Ok(v) => {
            if json.is_object(v) { print("PASS 1: parse object") }
            else { print("FAIL 1: not object") }
        }
    }

    // ---- Test 2: navigation API ----
    Result(JsonValue, Str) r2 = json.parse(src)
    match r2 {
        Err(e) => { print(f"FAIL 2: {e}"); return }
        Ok(root) => {
            // object_len: 8 top-level keys
            int olen = json.object_len(root)
            if olen == 8 { print(f"PASS 2a: object_len={olen}") }
            else { print(f"FAIL 2a: object_len={olen} expected 8") }

            // object_has
            if json.object_has(root, "name") { print("PASS 2b: object_has name") }
            else { print("FAIL 2b: missing key 'name'") }

            if !json.object_has(root, "notexist") { print("PASS 2c: object_has negative") }
            else { print("FAIL 2c: should not have 'notexist'") }

            // object_keys — should list 8 keys in insertion order
            Vec(Str) ks = json.object_keys(root)
            if ks.len() == 8 { print(f"PASS 2d: object_keys len={ks.len()}") }
            else { print(f"FAIL 2d: keys len={ks.len()} expected 8") }

            if ks[0].compare("name") == 0 { print("PASS 2e: first key=name") }
            else { print(f"FAIL 2e: first key={ks[0]}") }
        }
    }

    // ---- Test 3: parse sub-structures (array_len / object_len on tags/config) ----
    // We re-parse since we can't safely extract sub-values (Phase H not yet done).
    // Instead verify stringify → re-parse consistency via round-trip below.
    Result(JsonValue, Str) r3 = json.parse("[1,2,3,4,5]")
    match r3 {
        Err(e) => { print(f"FAIL 3: {e}"); return }
        Ok(arr) => {
            int alen = json.array_len(arr)
            if alen == 5 { print(f"PASS 3a: array_len={alen}") }
            else { print(f"FAIL 3a: array_len={alen} expected 5") }
        }
    }

    // array_len on non-array returns -1
    Result(JsonValue, Str) r3b = json.parse("42")
    match r3b {
        Err(e) => { print(f"FAIL 3b: {e}") }
        Ok(num) => {
            int bad = json.array_len(num)
            if bad == 0 - 1 { print("PASS 3b: array_len non-array=-1") }
            else { print(f"FAIL 3b: expected -1 got {bad}") }
        }
    }

    // ---- Test 4: stringify + file round-trip ----
    Result(JsonValue, Str) r4 = json.parse(src)
    match r4 {
        Err(e) => { print(f"FAIL 4: {e}"); return }
        Ok(root) => {
            Str compact = json.stringify(root)

            // Write compact JSON to a temp file
            Str tmpfile = "json_rt_tmp.json"
            Result(int, Str) wr = io.write_file(tmpfile, compact)
            match wr {
                Err(e) => { print(f"FAIL 4a: write_file error={e}"); return }
                Ok(n) => { print(f"PASS 4a: wrote {n} bytes") }
            }

            // Read back
            Result(Str, Str) rd = io.read_file(tmpfile)
            match rd {
                Err(e) => { print(f"FAIL 4b: read_file error={e}"); return }
                Ok(content) => {
                    Str cs = compact.copy()
                    if content.compare(cs) == 0 {
                        print("PASS 4b: file content matches stringify")
                    } else {
                        print("FAIL 4b: file content mismatch")
                    }

                    // Parse the file content again (second parse)
                    Result(JsonValue, Str) r4c = json.parse(content)
                    match r4c {
                        Err(e) => { print(f"FAIL 4c: re-parse error={e}") }
                        Ok(root2) => {
                            Str compact2 = json.stringify(root2)
                            if compact2.compare(compact) == 0 {
                                print("PASS 4c: round-trip stringify matches")
                            } else {
                                print("FAIL 4c: round-trip mismatch")
                                print(f"  original: {compact}")
                                print(f"  round-trip: {compact2}")
                            }
                        }
                    }
                }
            }

            // Clean up temp file (ignore result)
            Result(int, Str) rm = io.remove(tmpfile)
        }
    }

    // ---- Test 5: pretty-print round-trip ----
    Result(JsonValue, Str) r5 = json.parse("{\"a\":1,\"b\":[true,false,null],\"c\":{\"x\":\"hello\",\"y\":-0.5}}")
    match r5 {
        Err(e) => { print(f"FAIL 5: {e}"); return }
        Ok(v5) => {
            Str pretty = json.stringify_pretty(v5, 2)
            // Re-parse the pretty version
            Result(JsonValue, Str) r5b = json.parse(pretty)
            match r5b {
                Err(e) => { print(f"FAIL 5: pretty re-parse error={e}") }
                Ok(v5b) => {
                    Str compact5 = json.stringify(v5b)
                    Str expected5 = json.stringify(v5)
                    if compact5.compare(expected5) == 0 {
                        print("PASS 5: pretty round-trip OK")
                    } else {
                        print("FAIL 5: pretty round-trip mismatch")
                    }
                }
            }
        }
    }

    // ---- Test 6: large array (100 elements) ----
    // Build by concatenating a JSON string, then parse + verify array_len
    Str big = "["
    int idx = 0
    while idx < 100 {
        if idx > 0 { big = big + "," }
        big = big + f"{idx}"
        idx = idx + 1
    }
    big = big + "]"
    Result(JsonValue, Str) r6 = json.parse(big)
    match r6 {
        Err(e) => { print(f"FAIL 6: {e}") }
        Ok(arr6) => {
            int alen6 = json.array_len(arr6)
            if alen6 == 100 { print(f"PASS 6: large array len={alen6}") }
            else { print(f"FAIL 6: expected 100 got {alen6}") }
        }
    }

    print("done")
}
