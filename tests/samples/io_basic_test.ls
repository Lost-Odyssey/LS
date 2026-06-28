// Phase 9 v1: built-in `io` module — file I/O via libc direct calls.
// Exercises read_file / write_file / exists / open / close / read_all / write
// and the OpenMode enum + File struct.
import std.sys.io as io
import std.core.str

def main() -> int {
    Str path = "io_basic_test.tmp"

    // ---- write_file + exists + read_file roundtrip ----
    Str content = "Hello, LS!"
    match io.write_file(path, content) {
        Ok(n)  => @print(n)        // 10
        Err(e) => @print(e)
    }

    @print(io.exists(path))         // true
    @print(io.exists("__nope__"))   // false

    match io.read_file(path) {
        Ok(s)  => @print(s)         // Hello, LS!
        Err(e) => @print(e)
    }

    // ---- handle-style: open + read_all + explicit close ----
    // `io.open(...)` is an owned rvalue; the move-only File binder `f` is moved
    // out of it. read_all borrows (&File, auto-borrow); close mutates (&!File).
    match io.open(path, ReadBinary) {
        Ok(f) => {
            match io.read_all(f) {
                Ok(s)  => @print(s)        // Hello, LS!
                Err(e) => @print(e)
            }
            @print(io.close(&!f))          // 0  (later auto-close no-ops: handle nil)
        }
        Err(e) => @print(e)
    }

    // ---- handle-style write + flush ----
    match io.open(path, WriteBinary) {
        Ok(f) => {
            match io.write(f, "abc") {
                Ok(n)  => @print(n)        // 3
                Err(e) => @print(e)
            }
            match io.flush(f) {
                Ok(_)  => @print("flushed")  // flushed
                Err(e) => @print(e)
            }
            @print(io.close(&!f))          // 0
        }
        Err(e) => @print(e)
    }

    // verify the rewrite
    match io.read_file(path) {
        Ok(s)  => @print(s)            // abc
        Err(e) => @print(e)
    }

    // ---- RAII: no explicit close — File auto-closes at arm scope exit ----
    match io.open(path, ReadBinary) {
        Ok(f) => {
            match io.read_all(f) {
                Ok(s)  => @print(s)        // abc  (f auto-closes when the arm ends)
                Err(e) => @print(e)
            }
        }
        Err(e) => @print(e)
    }

    // ---- error path: open a nonexistent file for read ----
    match io.open("__definitely_not_here__", Read) {
        Ok(f)  => { io.close(&!f); @print("unexpected ok") }
        Err(e) => @print(e)            // io: open failed
    }

    @print("ALL PASS")
    return 0
}
