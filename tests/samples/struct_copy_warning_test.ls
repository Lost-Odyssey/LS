struct File {
    *u8 data;
}

methods File {
}

methods File: Destroy {
    def ~(&!self) {
        @print("File.__drop called")
    }
}

def make_file() -> File {
    File f
    return f
}

def main() {
    // Test 1: Copy assignment - should warn
    File a = make_file()
    File b = a  // Should warn: copy of struct with __drop
    @print("main ended")
}
