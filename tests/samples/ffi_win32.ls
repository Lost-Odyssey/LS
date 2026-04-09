// FFI: Calling Windows system DLLs

lib kernel32 = load("kernel32.dll")
lib msvcrt   = load("msvcrt.dll")

// Declare extern functions with type signatures
extern fn GetCurrentProcessId() -> int from kernel32
extern fn puts(string s) -> int from msvcrt

fn main() -> int {
    // 1. Call kernel32.dll — get current process ID
    int pid = GetCurrentProcessId()
    print(f"Current Process ID: {pid}")

    // 2. Call msvcrt.dll — puts
    puts("Hello from msvcrt.dll puts!")

    // 3. Dynamic call (no type checking, unsafe)
    msvcrt.call("puts", "Dynamic call to msvcrt!")

    // 4. Call kernel32 dynamically
    kernel32.call("Sleep", 0)
    print("Sleep(0) returned — kernel32 dynamic call works!")

    return 0
}
