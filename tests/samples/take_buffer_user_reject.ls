// Negative test: __string_take_buffer must be rejected outside stdlib/.

fn main() {
    *u8 buf = std.c.malloc(8)
    string s = __string_take_buffer(buf, 0)
    print(s)
}
