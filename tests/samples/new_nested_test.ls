struct Color {
    int r;
    int g;
    int b;
}

struct Pixel {
    f64 x;
    f64 y;
    Color color;
}

fn main() -> int {
    // Stack-allocated nested struct value
    Color red
    red.r = 255
    red.g = 0
    red.b = 0

    // Heap-allocated struct with nested value-type field
    *Pixel px = new Pixel { x: 10.0, y: 20.0, color: red }
    print(px.x, px.y)                           // 10.000000 20.000000
    print(px.color.r, px.color.g, px.color.b)   // 255 0 0

    free(px)
    print("done")                               // done
    return 0
}
