// @derive on a user-defined generic struct — the FULL matrix (all seven traits).
// Equal/Hash/Order/Reflect were the first wave; Show/Serialize/Deserialize complete
// it: a type-parameter field `T` lowers to self.f.show()/.to_value()/T.from_value(),
// and std.core.{show,value,str} provide those for every primitive + Str, so the
// monomorphized Box(int)/Box(Str)/Box(f64) all resolve. No `where T: Trait` bound is
// added — an instantiation whose T lacks the operation fails at monomorphization.
//
// A static method on a generic instance is called DIRECTLY — `Box(int).reflect()`,
// `Box(Str).from_value(v)` — no type alias needed (③: the parser handles type-keyword
// args, the checker reinterprets user-type-arg call forms). A type alias still works
// too (BI.reflect() below), for back-compat.
import std.core.map
import std.core.reflect
import std.core.value

@derive(Equal, Hash, Order, Reflect, Show, Serialize, Deserialize)
struct Box(T) { T value; Str label }

methods(T) Box(T) {
    def get(&self) -> T { return self.value }
}

type BI = Box(int)   // alias path still supported (used for reflect below)

def main() {
    // ---- Equal / Order ----
    Box(int) a = Box(int) { value: 5, label: "k" }
    Box(int) b = Box(int) { value: 5, label: "k" }
    Box(int) z = Box(int) { value: 9, label: "k" }
    if a == b { @print("eq PASS") } else { @print("eq FAIL") }
    if a < z { @print("ord PASS") } else { @print("ord FAIL") }

    // ---- Hash + Equal: Box(int) (with a Str field) as a Map key ----
    Map(Box(int), int) m = {}
    m.set(a, 100)
    @print(m.get(b).unwrap_or(0))         // 100

    // ---- Show (T field via T.show(), Str field via f-string) ----
    @print(to_str(a))                      // Box { value: 5, label: k }
    Box(Str) bs = Box(Str) { value: "inner", label: "outer" }
    @print(to_str(bs))                     // Box { value: inner, label: outer }
    Box(f64) bf = Box(f64) { value: 1.5, label: "f" }
    @print(to_str(bf))                     // Box { value: 1.500000, label: f }

    // ---- Serialize / Deserialize round-trip ----
    @print(a.to_value().to_json())        // {"value":5,"label":"k"}
    Box(int) ra = Box(int).from_value(a.to_value())
    if a == ra { @print("int roundtrip PASS") } else { @print("int roundtrip FAIL") }

    @print(bs.to_value().to_json())       // {"value":"inner","label":"outer"}
    Box(Str) rs = Box(Str).from_value(bs.to_value())
    if bs == rs { @print("str roundtrip PASS") } else { @print("str roundtrip FAIL") }

    Box(f64) rf = Box(f64).from_value(bf.to_value())
    if bf == rf { @print("f64 roundtrip PASS") } else { @print("f64 roundtrip FAIL") }

    // ---- Sized-int type params (full primitive coverage, not just int/i64) ----
    Box(i16) si = Box(i16) { value: 300 as i16, label: "s" }
    @print(to_str(si))                     // Box { value: 300, label: s }
    Box(i16) rsi = Box(i16).from_value(si.to_value())
    if si == rsi { @print("i16 roundtrip PASS") } else { @print("i16 roundtrip FAIL") }

    Box(u8) u = Box(u8) { value: 200 as u8, label: "u" }
    Box(u8) ru = Box(u8).from_value(u.to_value())
    if u == ru { @print("u8 roundtrip PASS") } else { @print("u8 roundtrip FAIL") }

    // ---- Reflect (a type-param field reflects its CONCRETE instantiated type
    //      via __type_name(T): BI = Box(int) -> "value: int", not "value: T") ----
    // for-in over a generic reflect() result's Vec works (the Iterator-protocol path
    // ensures Vec(FieldInfo)'s iter()/next() on demand — VR-LIM-018). No index loop.
    TypeInfo ti = BI.reflect()
    @print(ti.name)                       // Box
    for fi in ti.fields {
        @print(fi.name + ": " + fi.type_name)   // value: int  /  label: Str
    }
    @print("GENERIC DERIVE DONE")
}
