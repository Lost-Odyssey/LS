// trait_dup_reject.ls — should produce compile error: duplicate trait name

interface Printable {
    def to_string(&self) -> Str
}

interface Printable {
    def display(&self) -> Str
}

def main() {
    @print(42)
}
