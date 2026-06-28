// bit-pattern subject must be an integer type — Str must be rejected.
def main() {
    Str s = "hi"
    match s {
        bits[4:a][4:b] => { @print("x") }
        _ => {}
    }
}
