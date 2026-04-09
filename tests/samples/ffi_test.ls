// FFI test — load msvcrt.dll on Windows and call C functions

lib msvcrt = load("msvcrt.dll")

extern fn puts(string s) -> int from msvcrt

fn main() -> int {
    // Test 1: extern fn call through loaded library
    puts("Hello from FFI!")

    // Test 2: dynamic call via lib.call()
    msvcrt.call("puts", "Dynamic FFI call works!")

    // Test 3: print works alongside FFI
    print("FFI test passed")
    return 0
}
