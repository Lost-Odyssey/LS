module mod_a

struct Box {
    int value
    int padding
}

fn make(int v) -> Box {
    Box b
    b.value = v
    b.padding = 0
    return b
}
