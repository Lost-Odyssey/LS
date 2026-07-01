// Negative: the comptime block returns an array whose length does not match the
// declared array length — a clean compile error (not a crash, not silent truncation).
comptime array(int, 10) BAD = comptime {
    array(int, 4) t = {}
    for i in 0..4 { t[i] = i }
    return t
}
def main() { @print("unreachable") }
