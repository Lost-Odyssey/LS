module main

struct Point {
    int x
    int y
}

methods Point {

    static def create(int x, int y) -> Point {
        Point p
        p.x = x
        p.y = y
        return p
    }
}

methods Point: Destroy {
    def ~(&!self) {
        @print("  [drop] Point:", self.x, ",", self.y)
    }
}

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

def take_by_value(Point p) {
    @print("  In take_by_value:", p.x, p.y)
}

def take_resource(Resource r) {
    @print("  In take_resource:", r.id)
}

def identity_point(Point p) -> Point {
    @print("  In identity_point:", p.x, p.y)
    return p
}

def identity_resource(Resource r) -> Resource {
    @print("  In identity_resource:", r.id)
    return r
}

def print_point(Point p) {
    @print("  [print_point]", p.x, p.y)
}

def print_resource(Resource r) {
    @print("  [print_resource]", r.id)
}

def main() -> int {
    @print("==========================================")
    @print("   Struct Static Method E2E Test")
    @print("==========================================")
    @print("")

    @print("=== Test: Static create for Point ===")
    Point p1 = Point.create(10, 20)
    @print("  Created p1:", p1.x, p1.y)
    @print("")

    @print("=== Test: Static create for Resource ===")
    Resource r1 = Resource.create(100)
    @print("  Created r1:", r1.id)
    @print("")

    @print("=== Test: Pass Point to function ===")
    take_by_value(p1)
    @print("  (p1 moved to take_by_value)")
    @print("")

    @print("=== Test: Pass Resource to function ===")
    take_resource(r1)
    @print("  (r1 moved to take_resource)")
    @print("")

    @print("=== Test: Identity with Point ===")
    Point p2 = identity_point(Point.create(30, 40))
    @print("  Got identity point:", p2.x, p2.y)
    @print("")

    @print("=== Test: Identity with Resource ===")
    Resource r2 = identity_resource(Resource.create(200))
    @print("  Got identity resource:", r2.id)
    @print("")

    @print("=== Test: Multiple static creates ===")
    Point p3 = Point.create(50, 60)
    Resource r3 = Resource.create(300)
    @print("  p3:", p3.x, p3.y, "r3:", r3.id)
    @print("")

    @print("=== Test: Chain static methods ===")
    Resource r_chain = identity_resource(identity_resource(Resource.create(400)))
    @print("  After chain:", r_chain.id)
    @print("")

    @print("==========================================")
    @print("   All static method tests completed")
    @print("==========================================")
    return 0
}
