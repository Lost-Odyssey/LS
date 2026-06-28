// Destroy + Clone interfaces (folding traits): generic + non-generic.
//   Destroy: methods X: Destroy { def ~(&!self) }       (C++-style `~`)
//   Clone:   methods X: Clone   { def clone(&self)->Self }
int drops = 0
int clones = 0

struct Box(T) { Str tag }
methods(T) Box(T) {
    def deepcopy(&self) -> Box(T) { clones = clones + 1; return Box(T){ tag: self.tag.copy() } }
}
methods(T) Box(T): Destroy { def ~(&!self) { drops = drops + 1 } }
methods(T) Box(T): Clone   { def clone(&self) -> Box(T) { return self.deepcopy() } }

struct Handle { Str name }
methods Handle: Destroy { def ~(&!self) { drops = drops + 1 } }

def gen_scope() {
    Vec(Box(int)) v = {}
    v.push(Box(int){ tag: "a" })
    Box(int) c = v[0]            // triggers Box.__clone
}
def nongen_scope() {
    Handle h = Handle{ name: "x" }
}
def main() -> int {
    gen_scope()
    nongen_scope()
    // gen: Box c + Box in v drop (~)=2 ; nongen: Handle h (~)=1 ; total 3.
    // v[0] read-by-value triggers Box.clone (→ __clone) once.
    if drops == 3 && clones == 1 {
        @print("DESTROY OK")
    } else {
        @print("DESTROY FAIL")
        @print(drops)
        @print(clones)
    }
    return 0
}
