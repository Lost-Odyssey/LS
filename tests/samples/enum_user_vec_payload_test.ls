// enum_user_vec_payload_test.ls -- user-defined Vec(T) as enum payload.
// Regression for variant-0 generic struct payload plus later variants, and
// for enum drop/clone dispatching Vec(T).__drop / __clone instead of raw fields.

import std.core.vec
import std.core.str

enum UserVecData {
    Nums(Vec(int) nums)
    Mixed(Str label, Vec(Str) items)
    Empty
}

def make_numbers() -> UserVecData {
    Vec(int) v = [10, 20, 30]
    return Nums(v)
}

def make_mixed() -> UserVecData {
    Vec(Str) items = []
    items.push("hello")
    items.push("world")
    return Mixed("test", items)
}

def process(UserVecData d) {
    match d {
        Nums(nums) => { @print(f"user numbers: len={nums.len()}") }
        Mixed(label, items) => { @print(f"user mixed: {label} len={items.len()}") }
        Empty => { @print("user empty") }
    }
}

def main() {
    UserVecData d1 = make_numbers()
    process(d1)

    UserVecData d2 = make_mixed()
    process(d2)

    UserVecData d3 = Empty
    process(d3)

    @print("user vec enum done")
}
