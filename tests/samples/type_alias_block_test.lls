// Phase A: type alias + Block type parses & type-checks.
// Phase A does NOT yet implement closure codegen, so we only exercise paths
// that don't require lowering a TYPE_BLOCK value. Aliases over Block can be
// declared and forward-referenced; aliases over primitives/structs are usable
// at runtime.

// Block-typed aliases — declared, not used at runtime.
type Adder       = Block(int) -> int
type Predicate   = Block(int) -> bool
type Comparator  = Block(int, int) -> bool
type LineHandler = Block(Str)
type Nop         = Block()

// Aliases over primitive types should be transparent.
type MyInt    = int
type MyString = Str

// Aliases over user types.
struct Point {
    int x
    int y
}
type Pt = Point

def main() {
    MyInt a = 42
    MyString s = "hello"
    Pt p = Point{x: 1, y: 2}
    @print(a)
    @print(s)
    @print(p.x + p.y)
}
