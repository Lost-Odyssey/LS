fn string_op(string in_) {
    string in = in_ + "_test"
    print(in)
}

fn main() -> int {
    string s = "static"
    string_op(s.upper())
    print(s)
    return 0
}
