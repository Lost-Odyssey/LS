module mod_counter

int total = 0

fn inc() {
    total = total + 1
}

fn get() -> int {
    return total
}

fn reset() {
    total = 0
}
