struct Resource { int id; }

methods Resource {
}

methods Resource: Destroy {
    def ~(&!self) {
        @print("Resource dropped")
    }
}

def test_return_in_block() -> Resource {
    Resource r
    r.id = 1
    @print("r created")
    {
        return r
    }
}

def main() {
    @print("=== Test: return in block ===")
    Resource result = test_return_in_block()
    @print("result.id:")
    @print(result.id)
}
