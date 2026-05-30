module mod_b

struct Box {
    int value
}

fn make(int v) -> Box {
    Box b
    b.value = v
    return b
}
