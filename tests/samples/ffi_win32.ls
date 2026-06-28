// FFI: Calling Windows system DLLs.
// FFI red line: extern fns take *u8; Str crosses the boundary via c_str().
import std.core.str

lib kernel32 = load("kernel32.dll")
lib msvcrt   = load("msvcrt.dll")

// Declare extern functions with type signatures
extern def GetCurrentProcessId() -> int from kernel32
extern def puts(*u8 s) -> int from msvcrt

def main() -> int {
    // 1. Call kernel32.dll — get current process ID
    int pid = GetCurrentProcessId()
    @print(f"Current Process ID: {pid}")

    // 2. Call msvcrt.dll — puts
    Str a = "Hello from msvcrt.dll puts!"
    puts(a.c_str())

    // 3. Dynamic call (no type checking, unsafe; *u8 arg passes as C pointer)
    Str b = "Dynamic call to msvcrt!"
    msvcrt.call("puts", b.c_str())

    // 4. Call kernel32 dynamically
    kernel32.call("Sleep", 0)
    @print("Sleep(0) returned — kernel32 dynamic call works!")

    return 0
}
