// json_file_io_test.ls
// End-to-end JSON file I/O test: reads sample_data.json via json.load_file(),
// verifies structure, writes via json.save_file() (pretty) and json.save_compact(),
// verifies round-trip for both.

import std.vec
import std.json as json
import io

fn main() {
    // ---- Test 1: load_file ----
    Result(JsonValue, string) r1 = json.load_file("tests/samples/sample_data.json")
    match r1 {
        Err(e) => { print(f"FAIL 1: load_file error={e}"); return }
        Ok(data) => {
            if json.is_object(data) { print("PASS 1: load_file returned object") }
            else { print("FAIL 1: not an object"); return }
        }
    }

    // ---- Test 2: object_len + object_has on root ----
    Result(JsonValue, string) r2 = json.load_file("tests/samples/sample_data.json")
    match r2 {
        Err(e) => { print(f"FAIL 2: load error={e}"); return }
        Ok(data) => {
            int n = json.object_len(data)
            if n == 9 { print(f"PASS 2a: root keys={n}") }
            else { print(f"FAIL 2a: expected 9 got {n}") }

            if json.object_has(data, "metadata") { print("PASS 2b: has metadata") }
            else { print("FAIL 2b: missing metadata") }

            if json.object_has(data, "users") { print("PASS 2c: has users") }
            else { print("FAIL 2c: missing users") }
        }
    }

    // ---- Test 3: array_len on root ----
    Result(JsonValue, string) r3 = json.load_file("tests/samples/sample_data.json")
    match r3 {
        Err(e) => { print(f"FAIL 3: load error={e}"); return }
        Ok(data) => {
            int alen = json.array_len(data)
            if alen == 0 - 1 { print("PASS 3a: root is not array") }
            else { print(f"FAIL 3a: expected -1 got {alen}") }
        }
    }

    // ---- Test 4: save_compact round-trip (compact) ----
    Result(JsonValue, string) r4 = json.load_file("tests/samples/sample_data.json")
    match r4 {
        Err(e) => { print(f"FAIL 4: load error={e}"); return }
        Ok(data) => {
            string s1 = json.stringify(data)

            Result(int, string) wr = json.save_compact("json_e2e_out.json", data)
            match wr {
                Err(e) => { print(f"FAIL 4a: save_compact error={e}"); return }
                Ok(n) => { print(f"PASS 4a: compact saved {n} bytes") }
            }

            Result(JsonValue, string) r4b = json.load_file("json_e2e_out.json")
            match r4b {
                Err(e) => { print(f"FAIL 4b: load back error={e}"); return }
                Ok(data2) => {
                    string s2 = json.stringify(data2)
                    if s1.compare(s2) == 0 { print("PASS 4b: compact round-trip matches") }
                    else { print("FAIL 4b: compact round-trip mismatch") }
                }
            }
        }
    }

    // ---- Test 5: save_file round-trip (pretty, indent=2) ----
    Result(JsonValue, string) r5 = json.load_file("tests/samples/sample_data.json")
    match r5 {
        Err(e) => { print(f"FAIL 5: load error={e}"); return }
        Ok(data) => {
            string s1 = json.stringify(data)

            Result(int, string) wr = json.save_file("json_e2e_out.json", data)
            match wr {
                Err(e) => { print(f"FAIL 5a: save_file error={e}"); return }
                Ok(n) => { print(f"PASS 5a: pretty saved {n} bytes") }
            }

            // Verify the written file contains newlines (pretty-printed)
            Result(string, string) r5raw = io.read_file("json_e2e_out.json")
            match r5raw {
                Err(e) => { print(f"FAIL 5b: read raw error={e}") }
                Ok(raw) => {
                    if raw.contains("\n") { print("PASS 5b: file has newlines") }
                    else { print("FAIL 5b: file has no newlines") }
                }
            }

            // Re-parse and verify semantic round-trip
            Result(JsonValue, string) r5c = json.load_file("json_e2e_out.json")
            match r5c {
                Err(e) => { print(f"FAIL 5c: re-parse error={e}"); return }
                Ok(data2) => {
                    string s2 = json.stringify(data2)
                    if s1.compare(s2) == 0 { print("PASS 5c: pretty round-trip matches") }
                    else { print("FAIL 5c: pretty round-trip mismatch") }
                }
            }
        }
    }

    // ---- Test 6: object_keys to verify top-level keys ----
    Result(JsonValue, string) r6 = json.load_file("tests/samples/sample_data.json")
    match r6 {
        Err(e) => { print(f"FAIL 6: load error={e}"); return }
        Ok(data) => {
            Vec(string) keys = json.object_keys(data)
            if keys.len() == 9 { print(f"PASS 6: object_keys len={keys.len()}") }
            else { print(f"FAIL 6: expected 9 got {keys.len()}") }
        }
    }

    // ---- Clean up ----
    //Result(int, string) rm = io.remove("json_e2e_out.json")

    print("all done")
}
