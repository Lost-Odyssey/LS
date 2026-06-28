// Negative: @derive(Show) on a generic Box(T) adds NO `where T: Show` bound, so the
// synthesized show() just calls self.value.show(). Instantiating with a T that has no
// .show() — here a plain struct that didn't derive Show — fails at monomorphization
// with a clear missing-method error (NOT a silent wrong value).
@derive(Show)
struct Box(T) { T value }

struct Plain { int x }     // no @derive(Show) → no .show()

def main() {
    Box(Plain) b = Box(Plain) { value: Plain { x: 1 } }
    @print(to_str(b))        // monomorphizes Box(Plain).show() → Plain.show(): ERROR
}
