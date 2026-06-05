// json_bench.ls — JSON parse + stringify benchmark.
// Exercises the full LS runtime stack: recursive-descent parser (enum/match/
// vec/map/string allocation) + recursive serializer (f-string/concat/match).
//
//   ls run json_bench.ls [n]
//   ls compile json_bench.ls -o json_bench.exe && json_bench.exe [n]
//
// n = number of records in the JSON array.

import std.json as json
import perf
import proc

fn parse_n(int dflt) -> int {
    vec(string) a = proc.args()
    if a.length >= 1 {
        Result(int, string) r = a[0].to_int()
        match r {
            Ok(v)  => { return v }
            Err(e) => { return dflt }
        }
    }
    return dflt
}

fn build_json(int n) -> string {
    string s = "["
    for i in 0..n {
        if i > 0 { s = s + "," }
        string name = f"user_{i}"
        int score = i * 7
        string bval = "true"
        if i % 2 != 0 { bval = "false" }
        s = s + "{\"id\":" + f"{i}" + ",\"name\":\"" + name + "\",\"score\":" + f"{score}" + ",\"active\":" + bval + "}"
    }
    s = s + "]"
    return s
}

fn main() -> int {
    int n = parse_n(1000)
    int iters = 5

    // Build the JSON text once (not timed)
    string raw = build_json(n)

    // Warm-up
    Result(JsonValue, string) rw = json.parse(raw)
    match rw {
        Ok(v) => { string ws = json.stringify(v) }
        Err(e) => { print(f"FAIL: {e}"); return 1 }
    }

    i64 parse_ns = 0
    i64 stringify_ns = 0
    i64 chk = 0
    for (int _i = 0; _i < iters; _i = _i + 1) {
        // Parse
        i64 t0 = perf.now()
        Result(JsonValue, string) rp = json.parse(raw)
        i64 t1 = perf.now()
        parse_ns = parse_ns + (t1 - t0)

        match rp {
            Ok(v) => {
                // Stringify
                i64 t2 = perf.now()
                string out = json.stringify(v)
                i64 t3 = perf.now()
                stringify_ns = stringify_ns + (t3 - t2)
                chk = chk + out.length as i64
            }
            Err(e) => { print(f"FAIL: {e}"); return 1 }
        }
    }

    f64 parse_mean = parse_ns as f64 / iters as f64
    f64 str_mean = stringify_ns as f64 / iters as f64
    f64 total_mean = parse_mean + str_mean
    print(f"result: {chk}")
    print(f"[@bench] mean {total_mean:.1f} ns ({iters} iterations)")
    print(f"  parse:     {parse_mean:.1f} ns")
    print(f"  stringify: {str_mean:.1f} ns")
    return 0
}
