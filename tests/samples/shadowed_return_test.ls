struct Resource { int id; }

impl Resource {
    fn __drop() {
        print("Resource dropped")
    }
}

fn test_shadowed() -> string {
    string outer = "outer string"
    print("outer created")
    
    {
        string inner = "inner string"
        print("inner created")
        return inner
    }
    
    print("unreachable")
    return outer
}

fn test_struct_shadowed() -> Resource {
    Resource outer_r
    outer_r.id = 100
    print("outer_r created")
    
    {
        Resource inner_r
        inner_r.id = 200
        print("inner_r created")
        return inner_r
    }
    
    print("unreachable")
    return outer_r
}

fn main() {
    print("=== Test 1: shadowed string ===")
    string result1 = test_shadowed()
    print("result1:")
    print(result1)
    
    print("=== Test 2: shadowed struct ===")
    Resource result2 = test_struct_shadowed()
    print("result2.id:")
    print(result2.id)
}
