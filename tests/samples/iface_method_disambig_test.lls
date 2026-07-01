// iface_method_disambig_test.ls — L-002: same-name methods from different origins
// coexist; inherent priority on bare dispatch; qualified call `Iface.m(recv)`.
// Covers scenario A (two interfaces), B (inherent + interface), C (generic fn),
// &!self mutation through a qualified call, owned-Str move-out, and the
// single-provider regression (symbol must stay `T.m`, not get mangled).
import std.core.str

// --- Scenario A: two interfaces, same method name, NO inherent ---
interface Source { def close(&!self) -> int }
interface Sink   { def close(&!self) -> int }
struct Pipe { int n }
methods Pipe: Source { def close(&!self) -> int { return 100 } }
methods Pipe: Sink   { def close(&!self) -> int { return 200 } }

// --- Scenario B: inherent + interface, same name, owned Str payload ---
interface Show2 { def render(&self) -> Str }
struct Doc { Str body }
methods Doc { def render(&self) -> Str { return self.body } }
methods Doc: Show2 { def render(&self) -> Str { return f"<{self.body}>" } }

// --- &!self mutation through qualified calls ---
interface Inc { def step(&!self) }
interface Dec { def step(&!self) }
struct Ctr { int v }
methods Ctr: Inc { def step(&!self) { self.v = self.v + 10 } }
methods Ctr: Dec { def step(&!self) { self.v = self.v - 1 } }

// --- Scenario C: generic fn using a qualified interface call in its body ---
interface Named { def name(&self) -> Str }
struct Cat { Str who }
methods Cat: Named { def name(&self) -> Str { return self.who } }
def label(T: Named)(T x) -> Str { return Named.name(&x) }

// --- Single-provider interface (regression: symbol stays `T.m`) ---
interface Greeter { def greet(&self) -> Str }
struct Robot { Str id }
methods Robot: Greeter { def greet(&self) -> Str { return self.id } }

def main() -> int {
    Pipe p = Pipe { n: 0 }
    @print(Source.close(&!p))   // 100
    @print(Sink.close(&!p))     // 200

    Doc d = Doc { body: "hi" }
    @print(d.render())          // inherent: hi
    @print(Show2.render(&d))    // interface: <hi>
    Str s = Show2.render(&d)    // owned move-out from a qualified call
    @print(s)                   // <hi>

    Ctr c = Ctr { v: 5 }
    Inc.step(&!c)               // +10 -> 15
    Dec.step(&!c)               // -1  -> 14
    @print(c.v)                 // 14

    Cat cat = Cat { who: "Tom" }
    @print(label(cat))          // Tom

    Robot r = Robot { id: "R2" }
    @print(r.greet())           // single provider, bare: R2
    @print(Greeter.greet(&r))   // single provider, qualified (plain symbol): R2

    @print("ALL OK")
    return 0
}
