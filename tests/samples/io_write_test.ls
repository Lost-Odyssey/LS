import io

fn main() -> int {
    match io.write_file("io_wtest.tmp", "Hello") {
        Ok(n)  => print(n)
        Err(e) => print(e)
    }
    print("done")
    return 0
}
