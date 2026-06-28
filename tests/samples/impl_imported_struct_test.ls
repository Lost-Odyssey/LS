// B-3 (docs/bugs_deferred_p5_4.md §B-3): a user `impl` on an IMPORTED struct
// (here `impl Str`, Str coming from std.core.str) must emit method symbols under the
// struct's prefixed llvm_name ("std_str__Str.shout") so they match the dispatch
// site — previously the impl emitted bare "Str.shout" → JIT "Symbols not found".
//
// Self-verifying: prints "IMPLIMP PASS" on success.

import std.core.str

methods Str {
    def shout(&self) -> Str {
        return self.upper()
    }
    def exclaim(&self) -> Str {
        return self.upper() + "!"
    }
}

def main() -> int {
    Str s = "hi"
    Str a = s.shout()
    Str b = s.exclaim()
    if a.eq?("HI") && b.eq?("HI!") {
        @print("IMPLIMP PASS")
    } else {
        @print("FAIL: got " + a + " / " + b)
    }
    return 0
}
