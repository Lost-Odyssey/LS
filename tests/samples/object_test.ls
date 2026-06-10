fn main() -> int {
    // 1. Basic: *T -> object implicit conversion
    int x = 42
    *int p = &x
    object obj = p
    print(f"object from *int: {obj}")

    // 2. object from nil
    object null_obj = nil
    print(f"null object: {null_obj}")

    // 3. object -> *T explicit cast
    *int q = obj as *int
    print(f"cast back to *int, deref: {*q}")

    // 4. object == nil comparison
    if (null_obj == nil) {
        print("null_obj is nil: correct")
    }
    if (obj != nil) {
        print("obj is not nil: correct")
    }

    // 5. object as function parameter
    print(f"print object directly: {obj}")

    // 6. object in a struct field
    // (use *u8 via malloc for a more realistic example)
    *u8 buf = std.c.malloc(64 as i64)
    object data = buf
    *u8 buf2 = data as *u8
    std.c.free(buf2)
    print("malloc -> object -> free: ok")

    // 7. Multiple pointer types -> object
    f64 pi = 3.14
    *f64 fp = &pi
    object obj2 = fp
    *f64 fp2 = obj2 as *f64
    print(f"f64 round-trip: {*fp2}")

    // 8. object to integer cast (like intptr_t)
    i64 addr = obj as i64
    print(f"object as i64 address: {addr}")
    object obj3 = addr as object
    *int r = obj3 as *int
    print(f"i64 -> object -> *int deref: {*r}")

    return 0
}
