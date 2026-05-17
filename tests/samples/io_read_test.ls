import io

fn main() -> int {
    io.write_file("io_rtest.tmp", "Hello World")
    match io.read_file("io_rtest.tmp") {
        Ok(s)  => print(s)
        Err(e) => print(e)
    }
    print("done")
    return 0
}
