// Negative: enum derive supports Equal/Hash/Order/Show; Serialize/Deserialize/
// Reflect on enums are not supported yet and must be rejected at compile time
// (not silently ignored).
@derive(Serialize)
enum X { A; B }

def main() { @print("unreachable") }
