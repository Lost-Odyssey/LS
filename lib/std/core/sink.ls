// std/core/sink.ls — a byte sink for the unified render protocol (Show).
//
// Rendering = "write bytes into a sink", not "return a Str" (Rust Display /
// Formatter model). A `Sink` is just a byte buffer (its `buf` IS the Str a
// to_str harvests) plus typed write helpers, so nested rendering appends into
// ONE buffer with zero intermediate Str allocations.
//
// `@print` builds a Sink, shows each arg into it, then flushes `buf` to the
// current global destination `__sink_dest` (stdout / stderr / a file). `to_str`
// builds a Sink, shows into it, and harvests `buf` — never flushing.
//
// Depends ONLY on std.sys.c (fclose/fwrite + stdout/stderr handles) and
// std.core.str — NOT std.sys.io — so prelude injection of this module stays
// light (the File->SinkDest bridge lives in std.sys.io, one-way io -> sink).
// See docs/plan_print_sink.md.

import std.sys.c as c
import std.core.str

// ---- the sink ----

struct Sink { Str buf }

methods Sink {
    // Append a borrowed Str (the common leaf for Str values + literal chunks).
    def write(&!self, &Str s) { self.buf.push_str(s) }

    // Append one raw byte.
    def write_byte(&!self, int b) { self.buf.push_byte(b) }

    def write_bool(&!self, bool v) {
        if v { self.write("true") } else { self.write("false") }
    }

    // Unsigned decimal, high-power-of-ten division (no temp buffer, no reverse).
    def write_u64(&!self, u64 v) {
        u64 ten = 10 as u64
        u64 div = 1 as u64
        while v / div >= ten { div = div * ten }
        while div > (0 as u64) {
            u64 d = (v / div) % ten
            self.buf.push_byte((48 as int) + (d as int))
            div = div / ten
        }
    }

    // Signed decimal. The magnitude is computed in u64 via two's-complement
    // negate (`0 - v`), which is correct even for i64 MIN (no overflow on -n).
    def write_i64(&!self, i64 n) {
        if n < 0 {
            self.buf.push_byte(45)              // '-'
            u64 mag = (0 as u64) - (n as u64)
            self.write_u64(mag)
        } else {
            self.write_u64(n as u64)
        }
    }

    def write_int(&!self, int n) { self.write_i64(n as i64) }

    // Float via the runtime "%.*f" formatter (digits=6 == default "%f"), so the
    // bytes match printf exactly. from_cstr copies the NUL-terminated buffer.
    def write_f64(&!self, f64 x) {
        c.__ls_float_fixed_exec(x, 6)
        object p = c.__ls_float_fixed_ptr()
        Str s = from_cstr(p)
        self.buf.push_str(s)
    }
}

// ---- output destination + global redirect ----

// POD enum (NOT has_drop, no Destroy) — see docs/plan_print_sink.md §2.4: the
// File handle's ownership is managed explicitly by set_sink (close-on-switch)
// and the CRT (flush/close at exit), NOT by a SinkDest destructor. A has_drop
// SinkDest would fight the by-value `set_sink(d)` param's own destructor.
enum SinkDest {
    Stdout
    Stderr
    Fp(object)          // raw FILE* handle (ownership transferred in via std.sys.io.file)
}                       // named Fp (not File) to avoid colliding with io.File at the bridge

// Point @print() at a destination. The C runtime owns the current-stream state +
// close-on-switch (a single fclose of the previous redirect file); @print()'s
// codegen (emit_printf -> __ls_printf) writes to that same stream, so this
// redirects ALL print output. Stdout/Stderr are process streams (owned=0); Fp is
// a file whose ownership transferred in via io.file (owned=1 => fclose on switch
// or at exit). The previous redirect file is closed exactly once (no double
// close: the source File's destructor was nil'd by io.file).
def set_sink(SinkDest d) {
    match d {
        Stdout => { c.__ls_sink_set(c.__ls_stdout(), 0) }
        Stderr => { c.__ls_sink_set(c.__ls_stderr(), 0) }
        Fp(h)  => { c.__ls_sink_set(h, 1) }
    }
}

def reset_sink() { set_sink(Stdout) }

// Flush a fully-rendered buffer to the current print destination (so to_str-style
// sink output honors the redirect too).
def __sink_flush(&Str buf) {
    c.fwrite(buf.as_ptr() as *u8, 1 as i64, buf.len() as i64, c.__ls_sink_stream())
}
