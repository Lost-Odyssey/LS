module main

struct Resource {
    int id
}

methods Resource {

    static def create(int val) -> Resource {
        Resource r
        r.id = val
        return r
    }
}

methods Resource: Destroy {
    def ~(&!self) {
        @print("  [drop] Resource:", self.id)
    }
}

def basic_scope_test() {
    @print("=== Basic scope test ===")
    Resource r1 = Resource.create(1)
    @print("  created r1")
    Resource r2 = Resource.create(2)
    @print("  created r2")
    @print("  exiting basic_scope_test")
}

def nested_scope_test() {
    @print("=== Nested scope test ===")
    Resource outer = Resource.create(10)
    @print("  created outer")

    if (true) {
        Resource inner = Resource.create(11)
        @print("  created inner in if block")
        Resource inner2 = Resource.create(12)
        @print("  created inner2, leaving block")
    }
    @print("  left if block")

    @print("  exiting nested_scope_test")
}

def loop_test() {
    @print("=== Loop test ===")
    for i in 3 {
        Resource r = Resource.create(100 + i)
        @print("  iteration", i)
    }
    @print("  exited loop")
}

def conditional_test(bool flag) {
    @print("=== Conditional test ===")
    Resource a = Resource.create(200)
    Resource b = Resource.create(201)

    if (flag) {
        Resource c = Resource.create(202)
        @print("  created c, leaving if block")
    }
    @print("  flag processed")

    @print("  exiting conditional_test")
}

def main() -> int {
    @print("=== Destructor End-to-End Test ===")
    @print("")

    basic_scope_test()
    @print("")

    nested_scope_test()
    @print("")

    loop_test()
    @print("")

    conditional_test(true)
    @print("")

    conditional_test(false)
    @print("")

    @print("=== All tests completed ===")
    @print("  Destructors should be called in reverse order")
    @print("  when each function returns")
    return 0
}
