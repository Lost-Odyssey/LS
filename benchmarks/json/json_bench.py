# json_bench.py — Python reference for JSON benchmark.
#   python json_bench.py [n] [iters]
import sys
import json
import time


def build_json(n):
    records = []
    for i in range(n):
        records.append({
            "id": i,
            "name": f"user_{i}",
            "score": i * 7,
            "active": i % 2 == 0,
        })
    return json.dumps(records)


def main():
    n = int(sys.argv[1]) if len(sys.argv) >= 2 else 1000
    iters = int(sys.argv[2]) if len(sys.argv) >= 3 else 5

    raw = build_json(n)

    # warm-up
    _w = json.dumps(json.loads(raw))

    parse_ns = 0
    stringify_ns = 0
    chk = 0
    for _ in range(iters):
        t0 = time.perf_counter_ns()
        obj = json.loads(raw)
        t1 = time.perf_counter_ns()
        parse_ns += t1 - t0

        t2 = time.perf_counter_ns()
        out = json.dumps(obj)
        t3 = time.perf_counter_ns()
        stringify_ns += t3 - t2
        chk += len(out)

    parse_mean = parse_ns / iters
    str_mean = stringify_ns / iters
    total_mean = parse_mean + str_mean
    print(f"result: {chk}")
    print(f"[@bench] mean {total_mean:.1f} ns ({iters} iterations)")
    print(f"  parse:     {parse_mean:.1f} ns")
    print(f"  stringify: {str_mean:.1f} ns")


main()
