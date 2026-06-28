module mod_b3_drop
import std.core.str

int g_drop_count = 0

struct Resource {
    Str name
    int id
}

methods Resource {

    static def make(Str n, int id) -> Resource {
        Resource r
        r.name = n
        r.id = id
        return r
    }

    def get_id(&self) -> int {
        return self.id
    }
}

methods Resource: Destroy {
    def ~(&!self) {
        g_drop_count = g_drop_count + 1
    }
}

def get_drop_count() -> int {
    return g_drop_count
}

def create_and_drop(int n) {
    int i = 0
    for (i = 0; i < n; i = i + 1) {
        Resource r = Resource.make("item", i)
    }
}
