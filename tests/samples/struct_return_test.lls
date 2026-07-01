// Test: return struct with Str field - ownership transfer
import std.core.str

struct Item {
    Str name
    int value
}

def make() -> Item {
    Item it
    Str w = "world"
    it.name = w.upper()
    it.value = 1
    return it
    // 'it' is marked as is_returning, so its destructor is skipped
    // Ownership of it.name transfers to caller
}

def main() {
    Item it = make()
    @print(it.name, it.value)
    // Caller is responsible for freeing it.name when 'it' goes out of scope
}
