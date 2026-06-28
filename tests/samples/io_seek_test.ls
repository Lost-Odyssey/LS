// Phase 9 v2: io seek/tell/size/rewind + append_file + remove + SeekFrom enum.
// Binary-mode gate: seek/tell/size require a file opened in *Binary mode.
import std.sys.io as io
import std.core.str

def main() -> int {
    Str path = "io_seek_test.tmp"

    // Set up a 26-byte file: "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    match io.write_file(path, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") {
        Ok(n)  => @print(n)            // 26
        Err(e) => @print(e)
    }

    // ---- size + tell + seek (binary mode) ----
    match io.open(path, ReadBinary) {
        Ok(f) => {
            // size: query without disturbing position
            match io.size(f) {
                Ok(s)  => @print(s)        // 26
                Err(e) => @print(e)
            }

            // tell after open should be 0
            match io.tell(f) {
                Ok(p)  => @print(p)        // 0
                Err(e) => @print(e)
            }

            // seek 10 from Start, then read 5 bytes
            match io.seek(f, 10, Start) {
                Ok(p)  => @print(p)        // 10
                Err(e) => @print(e)
            }
            // read_all from offset 10 should give "KLMNOPQRSTUVWXYZ" (16 bytes)
            match io.read_all(f) {
                Ok(s)  => @print(s)        // KLMNOPQRSTUVWXYZ
                Err(e) => @print(e)
            }

            // rewind brings position back to 0
            match io.rewind(f) {
                Ok(_)  => @print("rewound")
                Err(e) => @print(e)
            }
            match io.tell(f) {
                Ok(p)  => @print(p)        // 0
                Err(e) => @print(e)
            }

            // seek -3 from End, tell
            match io.seek(f, -3, End) {
                Ok(p)  => @print(p)        // 23
                Err(e) => @print(e)
            }

            // seek +2 from Current, tell
            match io.seek(f, 2, Current) {
                Ok(p)  => @print(p)        // 25
                Err(e) => @print(e)
            }

            @print(io.close(&!f))
        }
        Err(e) => @print(e)
    }

    // ---- binary-mode gate: seek on text-mode file should Err ----
    match io.open(path, Read) {
        Ok(f) => {
            match io.seek(f, 0, Start) {
                Ok(p)  => @print(p)
                Err(e) => @print(e)        // io: file is text-mode or closed (...)
            }
            @print(io.close(&!f))
        }
        Err(e) => @print(e)
    }

    // ---- append_file: append to existing ----
    match io.append_file(path, "!!!") {
        Ok(n)  => @print(n)                // 3
        Err(e) => @print(e)
    }
    match io.read_file(path) {
        Ok(s)  => @print(s)                // ABCDEFGHIJKLMNOPQRSTUVWXYZ!!!
        Err(e) => @print(e)
    }

    // ---- remove ----
    match io.remove(path) {
        Ok(_)  => @print(io.exists(path))  // false
        Err(e) => @print(e)
    }

    // remove nonexistent → Err
    match io.remove("__nope__") {
        Ok(_)  => @print("unexpected ok")
        Err(e) => @print(e)                // io: remove failed
    }

    @print("ALL PASS")
    return 0
}
