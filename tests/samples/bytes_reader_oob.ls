// Reader bounds check: reading 4 bytes from a 2-byte buffer must abort.
import std.c as c
import std.bytes as b
fn main() {
    *u8 buf = c.malloc(2)
    buf[0] = 0xAA as u8
    buf[1] = 0xBB as u8
    Reader r = b.borrow(buf, 2)
    print("before")
    u32 v = r.be_u32()     // only 2 bytes available -> abort
    print("AFTER")          // must NOT run
}
