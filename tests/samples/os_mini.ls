import os
fn main() {
    object r = os.raw_getenv("PATH")
    if r != nil {
        print("found PATH")
    }
}
