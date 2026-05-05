// Phase 9 v1: built-in `io` module — file I/O via libc direct calls.
// Exercises read_file / write_file / exists / open / close / read_all / write
// and the OpenMode enum + File struct.
import io

fn main() -> int {
    string path = "io_basic_test.tmp"

    // ---- write_file + exists + read_file roundtrip ----
    string content = "Hello, LS!"
    match io.write_file(path, content) {
        Ok(n)  => print(n)        // 10
        Err(e) => print(e)
    }

    print(io.exists(path))         // true
    print(io.exists("__nope__"))   // false

    match io.read_file(path) {
        Ok(s)  => print(s)         // Hello, LS!
        Err(e) => print(e)
    }

    // ---- handle-style: open + read_all + close ----
    match io.open(path, ReadBinary) {
        Ok(f) => {
            match io.read_all(f) {
                Ok(s)  => print(s)        // Hello, LS!
                Err(e) => print(e)
            }
            print(io.close(f))            // 0
        }
        Err(e) => print(e)
    }

    // ---- handle-style write ----
    match io.open(path, WriteBinary) {
        Ok(f) => {
            match io.write(f, "abc") {
                Ok(n)  => print(n)        // 3
                Err(e) => print(e)
            }
            print(io.close(f))            // 0
        }
        Err(e) => print(e)
    }

    // verify the rewrite
    match io.read_file(path) {
        Ok(s)  => print(s)            // abc
        Err(e) => print(e)
    }

    // ---- error path: open a nonexistent file for read ----
    match io.open("__definitely_not_here__", Read) {
        Ok(f)  => { io.close(f); print("unexpected ok") }
        Err(e) => print(e)            // io: open failed
    }

    print("ALL PASS")
    return 0
}
