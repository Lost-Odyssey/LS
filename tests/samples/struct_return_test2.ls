// Test: verify struct return ownership - destructor called on caller side
struct Item {
    string name;
    int value;
}

fn make() -> Item {
    Item it
    it.name = "world".upper()
    it.value = 1
    print("make: returning it")
    return it
}

fn main() {
    print("main: before make()")
    Item it = make()
    print("main: after make(), it.name =", it.name)
    // it goes out of scope here - destructor should be called
    print("main: it going out of scope")
}