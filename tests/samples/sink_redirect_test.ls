// sink_redirect_test.ls — Stage C-1: set_sink redirects @print() itself (not just
// manual __sink_flush). print writes to the current stream (emit_printf ->
// __ls_printf), so set_sink(file/stderr) captures ALL print output, then reset
// returns to stdout. Verifies byte-exact captured content + close-on-switch.
// docs/plan_print_sink.md Stage C.

import std.core.sink as sink
import std.sys.io as io

struct Point { int x; int y }

def read_back(Str path) -> Str {
    match io.read_file(path) {
        Ok(c)  => { return c }
        Err(e) => { return "" }
    }
}

def main() {
    int pass = 0
    int fail = 0

    // Redirect @print() to a file, emit mixed types, then restore stdout.
    Str pa = "sink_redir_a.tmp"
    match io.open(pa, WriteBinary) {
        Ok(f) => { sink.set_sink(io.file(&!f)) }
        Err(e) => { fail = fail + 1; @print("open A fail") }
    }
    @print("line A")                       // -> file
    @print(42)                             // -> file (POD fast path)
    Point p = Point { x: 1, y: 2 }
    @print(p)                              // -> file (structural, no Show)
    sink.reset_sink()                     // closes A, back to stdout

    Str got = read_back(pa)
    Str want = "line A\n42\nPoint{x=1, y=2}\n"
    if got.eq?(want) { pass = pass + 1 } else { fail = fail + 1; @print("redirect mismatch:"); @print(got) }

    // Close-on-switch: file B then file C without reset; switching must close B.
    Str pb = "sink_redir_b.tmp"
    Str pc = "sink_redir_c.tmp"
    match io.open(pb, WriteBinary) { Ok(f) => { sink.set_sink(io.file(&!f)) } Err(e) => { fail = fail + 1 } }
    @print("BBB")
    match io.open(pc, WriteBinary) { Ok(f) => { sink.set_sink(io.file(&!f)) } Err(e) => { fail = fail + 1 } }
    @print("CCC")
    sink.reset_sink()
    if read_back(pb).eq?("BBB\n") { pass = pass + 1 } else { fail = fail + 1; @print("B switch fail") }
    if read_back(pc).eq?("CCC\n") { pass = pass + 1 } else { fail = fail + 1; @print("C reset fail") }

    // stderr redirect (mechanism only; not asserted).
    sink.set_sink(Stderr)
    @print("STDERR LINE")
    sink.reset_sink()

    // File-husk (ownership transfer): io.file(&!f) steals the handle and nils the
    // source File, so the source's destructor no-ops and a later io.write fails
    // ("closed from this side") — only the sink owns the handle now.
    Str pd = "sink_redir_d.tmp"
    bool husk_ok = false
    match io.open(pd, WriteBinary) {
        Ok(f) => {
            sink.set_sink(io.file(&!f))       // steals f.handle, nils it
            match io.write(&f, "x") {          // source File is now a husk
                Ok(n)  => { }
                Err(e) => { husk_ok = true }
            }
        }
        Err(e) => { fail = fail + 1 }
    }
    sink.reset_sink()
    if husk_ok { pass = pass + 1 } else { fail = fail + 1; @print("husk not nil'd") }

    io.remove(pa)
    io.remove(pb)
    io.remove(pc)
    io.remove(pd)

    if fail == 0 { @print("ALL PASS") } else { @print(f"{fail} FAILED") }
}
