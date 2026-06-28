module mod_counter

int total = 0

def inc() {
    total = total + 1
}

def get() -> int {
    return total
}

def reset() {
    total = 0
}
