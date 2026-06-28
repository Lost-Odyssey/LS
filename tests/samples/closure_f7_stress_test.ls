import std.core.vec
import std.core.str

/* Phase F.7 stress test: 1000-iteration loop over all closure capture patterns.
   Each iteration exercises:
     S1. POD capture (int by-copy)
     S2. Str by-move capture + factory
     S3. struct(has_drop) with Block field
     S4. Vec(Block) push + call + drop
     S5. enum(has_drop) by-move capture (Option(Str)) — exercises emit_enum_clone_val
     S6. [move] inline Vec capture (explicit by-move on local variable)
   Expected output:
     stress ok
*/

type Counter  = Block() -> int
type Greeter  = Block() -> Str
type IntOp    = Block(int) -> int

/* ── S1: POD capture ─────────────────────────────────────────────── */
def make_counter(int start) -> Counter {
    return || { return start }
}

/* ── S2: Str by-move capture ──────────────────────────────────── */
def make_greeter(Str name) -> Greeter {
    return || { return f"hi {name}" }
}

/* ── S3: struct with Block field ─────────────────────────────────── */
struct Transformer {
    IntOp step
    int   base
}

def make_transformer(int b) -> Transformer {
    return Transformer { step: |x| { return x * 2 }, base: b }
}

def transform_run(Transformer t) -> int {
    return t.step(t.base)
}

/* ── S5: enum capture ────────────────────────────────────────────── */
def make_opt_getter(Option(Str) val) -> Greeter {
    return || {
        match val {
            Some(s) => { return s }
            None    => { return "none" }
        }
    }
}

def run_iteration(int i) {
    /* S1: POD capture */
    Counter c = make_counter(i)
    int cv = c()
    if cv != i { @print("S1 fail") }

    /* S2: Str capture */
    Str name = f"user{i}"
    Greeter g = make_greeter(name)
    Str gv = g()

    /* S3: struct with Block field */
    Transformer t = make_transformer(i)
    int tv = transform_run(t)
    if tv != i * 2 { @print("S3 fail") }

    /* S4: Vec(Block) */
    Vec(Counter) ops = {}
    int j = 0
    while j < 3 {
        int captured = j
        ops.push(|| { return captured })
        j = j + 1
    }
    int sum4 = ops[0]() + ops[1]() + ops[2]()
    if sum4 != 3 { @print("S4 fail") }

    /* S5: enum(has_drop) capture - named variable path exercises emit_enum_clone_val */
    Str payload = f"msg{i}"
    Option(Str) opt = Some(payload)
    Greeter og = make_opt_getter(opt)
    Str ov = og()
    if !ov.eq?(f"msg{i}") { @print("S5 fail") }
}

def main() {
    int i = 0
    while i < 1000 {
        run_iteration(i)
        i = i + 1
    }
    @print("stress ok")
}
