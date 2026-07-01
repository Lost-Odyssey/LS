/* Phase 5.8: &!struct can downgrade to &struct at call site. */
struct Box {
    int w;
    int h;
}

def area(&Box b) -> int {
    return b.w * b.h
}

def grow(&!Box b) -> int {
    b.w = b.w + 1
    return area(b)            /* downgrade &!Box -> &Box */
}

def main() -> int {
    Box b = Box { w: 4, h: 5 }
    @print(grow(&!b))          /* expect: 25 ((4+1)*5) */
    @print(b.w)                /* expect: 5 */
    return 0
}
