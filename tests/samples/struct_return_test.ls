// Test: return struct with string field - ownership transfer
struct Item {
    string name;
    int value;
}

fn make() -> Item {
    Item it
    it.name = "world".upper()
    it.value = 1
    return it
    // 'it' is marked as is_returning, so its destructor is skipped
    // Ownership of it.name transfers to caller
}

fn main() {
    Item it = make()
    print(it.name, it.value)
    // Caller is responsible for freeing it.name when 'it' goes out of scope
}