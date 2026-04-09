struct Resource { int id; }

impl Resource {
    fn __drop() {
        print("Resource dropped")
    }
}

fn test_return_in_block() -> Resource {
    Resource r
    r.id = 1
    print("r created")
    {
        return r
    }
}

fn main() {
    print("=== Test: return in block ===")
    Resource result = test_return_in_block()
    print("result.id:")
    print(result.id)
}
