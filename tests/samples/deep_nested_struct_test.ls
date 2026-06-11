// Test: 3-level nested struct with Str at deepest level
struct Level3 {
    Str data;
}

struct Level2 {
    Level3 l3;
}

struct Level1 {
    Level2 l2;
}

fn main() {
    Level1 l1
    l1.l2.l3.data = "deep".upper()
    print(l1.l2.l3.data)
    // l1 goes out of scope - should l3.data be freed?
}