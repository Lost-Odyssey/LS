// Test: 5-level deep nested struct with Str at bottom
struct L5 {
    Str data;
}
struct L4 {
    L5 l5;
}
struct L3 {
    L4 l4;
}
struct L2 {
    L3 l3;
}
struct L1 {
    L2 l2;
}

def main() {
    L1 l1
    l1.l2.l3.l4.l5.data = "deep5".upper()
    @print(l1.l2.l3.l4.l5.data)
    @print("exiting...")
    // All 5 levels should be cleaned up properly
}