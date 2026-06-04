module main

// Both modules export a generic `Box(T)`. A bare use of `Box(int)` must be
// rejected with a clear cross-module ambiguity error (not silently bound to
// whichever module was imported first).
import ma
import mb

fn use_box(Box(int) b) -> int {
    return b.v
}

fn main() {
    print(0)
}
