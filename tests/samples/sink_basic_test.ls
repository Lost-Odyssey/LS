// sink_basic_test.ls — Stage A smoke for std.core.sink (docs/plan_print_sink.md).
// Exercises the Sink write helpers + __sink_flush redirect (stdout/stderr/file)
// + the io.file(&!File) ownership-transfer bridge + close-on-switch, with NO
// compiler print/show change yet (sink is used directly).

import std.core.sink as sink
import std.sys.io as io

// Render a fixed byte sequence covering every write_* helper.
def render_sample() -> sink.Sink {
    sink.Sink s = {}
    s.write("n=")
    s.write_int(42)
    s.write(" neg=")
    s.write_int(0 - 1000000)
    s.write(" min=")
    s.write_i64(-9223372036854775808 as i64)   // i64 MIN: no overflow on negate
    s.write(" b=")
    s.write_bool(true)
    s.write(" c=")
    s.write_byte(65)                            // 'A'
    return s
}

int pass = 0
int fail = 0

def check(bool ok, Str what) {
    if ok { pass = pass + 1 } else { fail = fail + 1; @print(f"FAIL: {what}") }
}

def write_to_file(Str path, Str text) {
    match io.open(path, Write) {
        Ok(fa) => {
            sink.set_sink(io.file(&!fa))         // handle moves into the sink
            sink.Sink s = {}
            s.write(text)
            sink.__sink_flush(s.buf)
        }
        Err(e) => { check(false, "open for write") }
    }
    sink.reset_sink()                            // closes + flushes the file
}

def read_back(Str path) -> Str {
    match io.read_file(path) {
        Ok(c) => { return c }
        Err(e) => { return "" }
    }
}

def main() {
    // 1. full render through a file, byte-exact round-trip.
    Str pa = "sink_smoke_a.tmp"
    match io.open(pa, Write) {
        Ok(fa) => {
            sink.set_sink(io.file(&!fa))
            sink.Sink s = render_sample()
            sink.__sink_flush(s.buf)
        }
        Err(e) => { check(false, "open A") }
    }
    sink.reset_sink()
    Str got = read_back(pa)
    Str want = "n=42 neg=-1000000 min=-9223372036854775808 b=true c=A"
    check(got.eq?(want), "file render round-trip")
    if !got.eq?(want) { @print(f"  got : {got}") }

    // 2. close-on-switch: write file B then file C without resetting between;
    //    set_sink(file C) must close+flush B so its content is readable.
    Str pb = "sink_smoke_b.tmp"
    Str pc = "sink_smoke_c.tmp"
    match io.open(pb, Write) {
        Ok(fb) => {
            sink.set_sink(io.file(&!fb))
            sink.Sink s = {}
            s.write("BBB")
            sink.__sink_flush(s.buf)
        }
        Err(e) => { check(false, "open B") }
    }
    match io.open(pc, Write) {
        Ok(fc) => {
            sink.set_sink(io.file(&!fc))         // <- must fclose B here
            sink.Sink s = {}
            s.write("CCC")
            sink.__sink_flush(s.buf)
        }
        Err(e) => { check(false, "open C") }
    }
    sink.reset_sink()
    check(read_back(pb).eq?("BBB"), "B flushed on switch")
    check(read_back(pc).eq?("CCC"), "C flushed on reset")

    // 3. stderr path (mechanism only — content not asserted here).
    sink.set_sink(Stderr)
    sink.Sink se = {}
    se.write("STDERR_OK\n")
    sink.__sink_flush(se.buf)
    sink.reset_sink()

    // 4. default stdout path.
    sink.Sink so = {}
    so.write("STDOUT_OK")
    sink.__sink_flush(so.buf)
    @print("")                                    // flush adds no newline

    // cleanup
    io.remove(pa)
    io.remove(pb)
    io.remove(pc)

    if fail == 0 { @print("ALL PASS") } else { @print(f"{fail} FAILED") }
}
