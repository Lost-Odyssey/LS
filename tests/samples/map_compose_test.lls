// map_compose_test.ls - std.core.map M-4: composition smoke/integration test.
// Covers Map as a struct field (including whole-struct `{}` zero-init), a
// global variable with program-exit auto-drop, an enum payload, and nested
// Map(string, Map(int,int)) built imperatively (not via nested map literals).
// JIT + AOT + memcheck must stay 0/0/0.

import std.core.map
import std.core.str

def check(bool c, Str l) { if c { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

struct Holder { Map(Str, int) m; int tag }

enum Cfg {
    Empty
    Table(Map(Str, int))
}

Map(Str, int) gmap = {}

def get_int(&Map(Str, int) m, Str k) -> int {
    match m.get(k) {
        Some(v) => { return v }
        None => { return -1 }
    }
}

def fill_global() {
    gmap.set("alpha", 11)
    gmap.set("beta", 22)
}

def check_struct_field() {
    Holder h = {}
    check(h.m.empty?(), "struct field zero-init empty")
    h.m.set("one", 1)
    h.m.set("two", 2)
    h.tag = 7
    check(h.m.len() == 2, "struct field len after set")
    check(get_int(h.m, "two") == 2 && h.tag == 7, "struct field read")
}

def check_global_map() {
    fill_global()
    check(gmap.len() == 2, "global map len")
    check(get_int(gmap, "alpha") == 11, "global map get alpha")
    gmap.set("alpha", 33)
    check(get_int(gmap, "alpha") == 33, "global map overwrite")
}

def check_enum_payload() {
    Map(Str, int) local = {}
    local.set("port", 8080)
    local.set("workers", 4)
    Cfg cfg = Table(local)
    int total = 0
    match cfg {
        Table(m) => {
            total = get_int(m, "port") + get_int(m, "workers")
        }
        Empty => {
            total = -1
        }
    }
    check(total == 8084, "enum payload map values")
}

def check_nested_map() {
    Map(Str, Map(int, int)) outer = {}
    Map(int, int) inner = {}
    inner.set(1, 10)
    inner.set(2, 20)
    outer.set("group", inner)
    Map(int, int) other = {}
    other.set(7, 70)
    outer.set("other", other)
    int nlen = 0
    match outer.get("group") {
        Some(m) => {
            nlen = m.len()
        }
        None => {}
    }
    check(outer.len() == 2, "nested outer len")
    check(nlen == 2, "nested inner len")
}

def main() {
    check_struct_field()
    check_global_map()
    check_enum_payload()
    check_nested_map()
    @print("COMPOSE PASS")
}
