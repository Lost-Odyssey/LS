/* Phase F.7 stress test: 1000-iteration loop over all closure capture patterns.
   Each iteration exercises:
     S1. POD capture (int by-copy)
     S2. string by-move capture + factory
     S3. struct(has_drop) with Block field
     S4. vec(Block) push + call + drop
     S5. enum(has_drop) by-move capture (Option(string)) — exercises emit_enum_clone_val
     S6. [move] inline vec capture (explicit by-move on local variable)
   Expected output:
     stress ok
*/

type Counter  = Block() -> int
type Greeter  = Block() -> string
type IntOp    = Block(int) -> int

/* ── S1: POD capture ─────────────────────────────────────────────── */
fn make_counter(int start) -> Counter {
    return || { return start }
}

/* ── S2: string by-move capture ──────────────────────────────────── */
fn make_greeter(string name) -> Greeter {
    return || { return f"hi {name}" }
}

/* ── S3: struct with Block field ─────────────────────────────────── */
struct Transformer {
    IntOp step
    int   base
}

fn make_transformer(int b) -> Transformer {
    return Transformer { step: |x| { return x * 2 }, base: b }
}

fn transform_run(Transformer t) -> int {
    return t.step(t.base)
}

/* ── S5: enum capture ────────────────────────────────────────────── */
fn make_opt_getter(Option(string) val) -> Greeter {
    return || {
        match val {
            Some(s) => { return s }
            None    => { return "none" }
        }
    }
}

fn run_iteration(int i) {
    /* S1: POD capture */
    Counter c = make_counter(i)
    int cv = c()
    if cv != i { print("S1 fail") }

    /* S2: string capture */
    string name = f"user{i}"
    Greeter g = make_greeter(name)
    string gv = g()

    /* S3: struct with Block field */
    Transformer t = make_transformer(i)
    int tv = transform_run(t)
    if tv != i * 2 { print("S3 fail") }

    /* S4: vec(Block) */
    vec(Counter) ops = []
    int j = 0
    while j < 3 {
        int captured = j
        ops.push(|| { return captured })
        j = j + 1
    }
    int sum4 = ops[0]() + ops[1]() + ops[2]()
    if sum4 != 3 { print("S4 fail") }

    /* S5: enum(has_drop) capture - named variable path exercises emit_enum_clone_val */
    string payload = f"msg{i}"
    Option(string) opt = Some(payload)
    Greeter og = make_opt_getter(opt)
    string ov = og()
    if ov != f"msg{i}" { print("S5 fail") }
}

fn main() {
    int i = 0
    while i < 1000 {
        run_iteration(i)
        i = i + 1
    }
    print("stress ok")
}
