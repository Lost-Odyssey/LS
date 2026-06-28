struct Inner {
    *u8 data;
}

methods Inner {
}

methods Inner: Destroy {
    def ~(&!self) {
        @print("Inner.__drop called")
    }
}

struct Outer {
    Inner a;
    Inner b;
}

methods Outer {
}

methods Outer: Destroy {
    def ~(&!self) {
        @print("Outer.__drop called")
    }
}

def main() {
    @print("test")
}
