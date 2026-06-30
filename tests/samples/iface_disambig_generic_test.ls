// iface_disambig_generic_test.ls — L-002 v2: same-name interface methods coexist
// on GENERIC types too. The generic fold preserves each method's origin interface,
// so monomorphized instances get inherent-priority bare dispatch + contended
// interface methods emitted as `T(args).<Iface>.m`. Covers inherent+2 interfaces,
// owned Str payload, and two distinct instantiations.
import std.core.str

interface Show3 { def tag(&self) -> Str }
interface Mark  { def tag(&self) -> Str }
struct Box(T) { T val; Str note }
methods(T) Box(T)        { def tag(&self) -> Str { return f"in:{self.note}" } }  // inherent
methods(T) Box(T): Show3 { def tag(&self) -> Str { return f"s3:{self.note}" } }
methods(T) Box(T): Mark  { def tag(&self) -> Str { return f"mk:{self.note}" } }

def main() -> int {
    Box(int) b = Box(int) { val: 1, note: "i" }
    @print(b.tag())          // inherent: in:i
    @print(Show3.tag(&b))    // s3:i
    @print(Mark.tag(&b))     // mk:i
    Str s = Show3.tag(&b)    // owned move-out from a qualified generic call
    @print(s)                // s3:i

    Box(Str) q = Box(Str) { val: "z", note: "j" }   // distinct instantiation
    @print(q.tag())          // in:j
    @print(Mark.tag(&q))     // mk:j

    @print("ALL OK")
    return 0
}
