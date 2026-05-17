import io

fn main() -> int {
    string path = "io_rft.tmp"
    string content = "Hello, LS!"
    match io.write_file(path, content) {
        Ok(n)  => print(n)
        Err(e) => print(e)
    }
    print(io.exists(path))
    match io.read_file(path) {
        Ok(s)  => print(s)
        Err(e) => print(e)
    }
    print("done")
    return 0
}
