import std.sys.io as io
import std.core.str

def main() -> int {
    Str path = "io_step_test.tmp"
    @print("step1")
    Str content = "Hello, LS!"
    @print("step2")
    match io.write_file(path, content) {
        Ok(n)  => @print(n)
        Err(e) => @print(e)
    }
    @print("step3")
    return 0
}
