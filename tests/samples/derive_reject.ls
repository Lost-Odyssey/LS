// Negative: an unsupported trait must be rejected at compile time with a clear
// message (not silently ignored). Equal/Hash/Order are supported; this isn't.
@derive(Frobnicate)
struct P { int x }

def main() { @print("unreachable") }
