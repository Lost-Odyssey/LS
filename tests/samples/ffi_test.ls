// FFI test — load msvcrt.dll on Windows and call C functions.
// FFI red line: extern fns take *u8; Str crosses the boundary via c_str().
import std.str

lib msvcrt = load("msvcrt.dll")

extern fn puts(*u8 s) -> int from msvcrt

fn main() -> int {
    // Test 1: extern fn call through loaded library
    Str a = "Hello from FFI!"
    puts(a.c_str())

    // Test 2: dynamic call via lib.call() (*u8 arg passes as C pointer)
    Str b = "Dynamic FFI call works!"
    msvcrt.call("puts", b.c_str())

    // Test 3: print works alongside FFI
    print("FFI test passed")
    return 0
}
