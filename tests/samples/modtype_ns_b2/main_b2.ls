import mod_b2

def main() -> int {
    Point p = mod_b2.make_point(3, 7)
    int s = mod_b2.sum(p)
    @print(f"sum={s}")

    Color c = Green
    int id = mod_b2.color_id(c)
    @print(f" color={id}")

    @print("\n")
    return 0
}
