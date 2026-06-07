// impl_string_test.ls — Phase 2.5: `impl string` extension methods.
// Exercises both the new language feature (user methods on a builtin type) and
// the migrated split/lines/chars/join now living in std/string.ls.

import std.vec
import std.string

// A user-defined extension method on the builtin string type.
impl string {
    fn shout(&self) -> string {
        return self.upper() + "!"
    }
    fn repeat_n(&self, int n) -> string {
        string out = ""
        int i = 0
        while i < n {
            out.append(self)
            i = i + 1
        }
        return out
    }
}

fn check(bool cond, string label) {
    if cond {
        print(f"PASS {label}")
    } else {
        print(f"FAIL {label}")
    }
}

fn main() {
    // ---- user extension methods ----
    string h = "hi"
    check(h.shout() == "HI!", "shout")
    check(h.repeat_n(3) == "hihihi", "repeat_n")

    // builtin methods still resolve to the builtin (not overridable)
    check("AbC".lower() == "abc", "builtin lower still works")

    // ---- migrated split ----
    Vec(string) parts = "a,b,c".split(",")
    check(parts.len() == 3, "split len")
    check(parts.get(0) == "a", "split[0]")
    check(parts.get(2) == "c", "split[2]")

    Vec(string) trail = "a,b,".split(",")
    check(trail.len() == 3, "split trailing sep len")
    check(trail.get(2) == "", "split trailing empty")

    Vec(string) one = "abc".split(",")
    check(one.len() == 1, "split no-sep len")
    check(one.get(0) == "abc", "split no-sep elem")

    Vec(string) emptysep = "abc".split("")
    check(emptysep.len() == 1, "split empty-sep len")
    check(emptysep.get(0) == "abc", "split empty-sep elem")

    // ---- migrated lines ----
    Vec(string) ls = "a\nb\nc".lines()
    check(ls.len() == 3, "lines len")
    check(ls.get(1) == "b", "lines[1]")

    Vec(string) crlf = "x\r\ny".lines()
    check(crlf.len() == 2, "crlf lines len")
    check(crlf.get(0) == "x", "crlf strip")

    Vec(string) tnl = "p\nq\n".lines()
    check(tnl.len() == 2, "trailing newline no empty")

    Vec(string) blanks = "a\n\nb".lines()
    check(blanks.len() == 3, "blank line preserved")
    check(blanks.get(1) == "", "blank line empty")

    // ---- migrated chars ----
    Vec(int) cs = "AB".chars()
    check(cs.len() == 2, "chars len")
    check(cs.get(0) == 65, "chars[0]")
    check(cs.get(1) == 66, "chars[1]")

    // ---- migrated join (round-trip with split) ----
    Vec(string) jp = "a,b,c".split(",")
    string joined = ",".join(jp)
    check(joined == "a,b,c", "join round-trip")

    string dash = "-".join(jp)
    check(dash == "a-b-c", "join other sep")

    print("DONE")
}
