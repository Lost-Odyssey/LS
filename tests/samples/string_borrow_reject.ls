// P4 negative: &string / &!string parameter types have been removed
// (superseded by &Str / &!Str, pointer ABI). Expect a checker error
// pointing the user at &Str.
fn peek(&string s) -> int {
    return s.length
}

fn main() -> int {
    string x = "hello"
    return peek(x)
}
