// Minimal io test - just test exists() which only uses fopen/fclose
import io

fn main() -> int {
    bool e = io.exists("__nonexistent__file__")
    if e {
        print("WRONG")
    } else {
        print("OK exists works")
    }
    return 0
}
