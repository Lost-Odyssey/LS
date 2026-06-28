// Stage 2: @derive(Serialize, Deserialize) over a neutral value tree.
//  Serialize:   to_value() -> Value;  Value.to_json() renders JSON.
//  Deserialize: from_value(Value) -> T (best-effort rebuild).
// POD/Str leaves + recursive nested struct; round-trips through the tree.
import std.core.value as value

@derive(Serialize, Deserialize, Equal)
struct Point { int x; int y }

@derive(Serialize, Deserialize, Equal)
struct Config { Str host; int port; bool tls; Point origin }

def main() {
    Config cfg = Config { host: "example.com", port: 8080, tls: true, origin: Point { x: 1, y: 2 } }
    @print(cfg.to_value().to_json())

    // round-trip: struct -> Value tree -> struct
    Config back = Config.from_value(cfg.to_value())
    if cfg == back { @print("ROUNDTRIP PASS") } else { @print("ROUNDTRIP FAIL") }

    // text round-trip: struct -> JSON string -> Value tree (from_json) -> struct
    Str j = cfg.to_value().to_json()
    match value.from_json(j) {
        Ok(v) => {
            Config back2 = Config.from_value(v)
            if cfg == back2 { @print("TEXT ROUNDTRIP PASS") } else { @print("TEXT ROUNDTRIP FAIL") }
        }
        Err(e) => { @print("TEXT PARSE FAIL: " + e) }
    }
    // malformed input -> Err (not a crash)
    match value.from_json("{bad") {
        Ok(_) => { @print("MALFORMED FAIL") }
        Err(e) => { @print("MALFORMED OK") }
    }

    // strict: try_from_value validates — Ok on good input, Err on a missing/mismatched
    // field (instead of from_value's silent default).
    match Config.try_from_value(cfg.to_value()) {
        Ok(c) => { if c == cfg { @print("STRICT OK") } else { @print("STRICT FAIL") } }
        Err(e) => { @print("STRICT ERR: " + e) }
    }
    match Config.try_from_value(value.from_json("{\"host\":\"h\",\"tls\":true,\"origin\":{\"x\":1,\"y\":2}}").unwrap()) {
        Ok(_) => { @print("STRICT MISSING FAIL") }
        Err(e) => { @print("STRICT MISSING OK") }
    }

    @print("DERIVE SERIALIZE DONE")
}
