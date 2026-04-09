struct File {
    *u8 data;
}

impl File {
    fn __drop() {
        print("File.__drop called")
    }
}

fn make_file() -> File {
    File f
    return f
}

fn main() {
    // Test 1: Copy assignment - should warn
    File a = make_file()
    File b = a  // Should warn: copy of struct with __drop
    print("main ended")
}
