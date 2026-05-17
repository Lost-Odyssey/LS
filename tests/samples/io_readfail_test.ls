import io

fn main() -> int {
    print("before")
    match io.read_file("__nonexistent__") {
        Ok(s)  => print(s)
        Err(e) => print(e)
    }
    print("done")
    return 0
}
