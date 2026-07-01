import std.sys.io as io

def main() -> int {
    match io.write_file("io_wtest.tmp", "Hello") {
        Ok(n)  => @print(n)
        Err(e) => @print(e)
    }
    @print("done")
    return 0
}
