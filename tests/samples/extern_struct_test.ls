// Phase E.1 — extern struct + extern { } block + extern def without 'from'

// --- Form A: standalone extern struct + extern def (no 'from') ---

extern struct Point2D {
    int x
    int y
}

extern struct Rect {
    Point2D origin
    int width
    int height
}

// --- Form B: extern { } block ---

extern {
    struct Color {
        int r
        int g
        int b
        int a
    }

    def abs(int x) -> int
    def strcmp(*u8 a, *u8 b) -> int
}

// --- Helper to construct and inspect extern struct values ---

def make_point(int x, int y) -> Point2D {
    Point2D p = Point2D { x: x, y: y }
    return p
}

def point_sum(Point2D p) -> int {
    return p.x + p.y
}

def make_color(int r, int g, int b, int a) -> Color {
    Color c = Color { r: r, g: g, b: b, a: a }
    return c
}

def main() {
    // --- Test 1: standalone extern struct construction and field access ---
    Point2D p = make_point(3, 4)
    int s = point_sum(p)
    if s != 7 {
        @print("FAIL: point_sum expected 7 got ")
        @print(s)
        return
    }
    @print("PASS: extern struct Point2D field read/write")

    // --- Test 2: extern struct field mutation ---
    p.x = 10
    p.y = 20
    if p.x + p.y != 30 {
        @print("FAIL: field mutation")
        return
    }
    @print("PASS: extern struct field mutation")

    // --- Test 3: nested extern struct ---
    Rect r = Rect { origin: Point2D { x: 1, y: 2 }, width: 100, height: 50 }
    if r.origin.x != 1 || r.origin.y != 2 || r.width != 100 {
        @print("FAIL: nested extern struct")
        return
    }
    @print("PASS: nested extern struct Rect")

    // --- Test 4: extern { } block struct ---
    Color col = make_color(255, 128, 0, 255)
    if col.r != 255 || col.g != 128 || col.b != 0 || col.a != 255 {
        @print("FAIL: Color field values")
        return
    }
    @print("PASS: extern block struct Color")

    // --- Test 5: extern def without 'from' (direct libc call) ---
    int av = abs(-42)
    if av != 42 {
        @print("FAIL: abs(-42) expected 42 got ")
        @print(av)
        return
    }
    @print("PASS: extern fn abs without from")

    // --- Test 6: extern def strcmp (from block) ---
    Str h1 = "hello"
    Str h2 = "hello"
    int cmp = strcmp(h1.c_str(), h2.c_str())
    if cmp != 0 {
        @print("FAIL: strcmp equal strings expected 0 got ")
        @print(cmp)
        return
    }
    @print("PASS: extern fn strcmp without from")

    @print("ALL PASS")
}
