// std/string.ls — extension methods on the builtin `string` type.
//
// Phase 2.5 of the vec-replacement plan (docs/plan_impl_builtin_types.md):
// the compiler no longer hard-codes string.split / .lines / .chars / .join
// (those used to construct the builtin vec in C). They now live here as pure-LS
// `impl string` methods returning the pure-LS Vec(T) from std.vec.
//
// Usage: a file that calls `s.split(...)` etc. must `import std.string`.
// Core string primitives (`upper`, `find`, `substr`, `length`, ...) remain
// builtin and need no import.

import std.vec

impl string {
    // Split `self` on every (non-overlapping) occurrence of `sep`.
    //   "a,b,c".split(",")  -> ["a", "b", "c"]
    //   "a,b,".split(",")   -> ["a", "b", ""]
    //   "".split(",")       -> [""]
    // Empty separator yields a single element equal to the whole string
    // (matches the old builtin behavior).
    fn split(&self, string sep) -> Vec(string) {
        Vec(string) out = {}
        int sn = sep.length
        if sn == 0 {
            string whole = self.substr(0, self.length)
            out.push(whole)
            return out
        }
        // Walk a shrinking copy of the remaining text. `find` returns the first
        // occurrence index (or -1); there is no offset-find primitive yet.
        string rest = self.substr(0, self.length)
        bool go = true
        while go {
            int idx = rest.find(sep)
            if idx < 0 {
                string tail = rest.substr(0, rest.length)
                out.push(tail)
                go = false
            } else {
                string piece = rest.substr(0, idx)
                out.push(piece)
                int rl = rest.length
                string nrest = rest.substr(idx + sn, rl - idx - sn)
                rest = nrest
            }
        }
        return out
    }

    // Split `self` into lines on '\n', stripping a '\r' immediately before the
    // '\n' (CRLF). A trailing newline does NOT yield an empty final element.
    //   "a\nb\n"   -> ["a", "b"]
    //   "a\r\nb"   -> ["a", "b"]
    //   "a\n\nb"   -> ["a", "", "b"]
    //   ""         -> []
    fn lines(&self) -> Vec(string) {
        Vec(string) out = {}
        int n = self.length
        if n == 0 { return out }
        int start = 0
        int i = 0
        while i < n {
            int ch = self.at_unsafe(i)
            if ch == 10 {
                int cut = i
                if cut > start {
                    int prev = self.at_unsafe(cut - 1)
                    if prev == 13 { cut = cut - 1 }
                }
                string line = self.substr(start, cut - start)
                out.push(line)
                start = i + 1
            }
            i = i + 1
        }
        // Trailing segment with no closing newline.
        if start < n {
            string line = self.substr(start, n - start)
            out.push(line)
        }
        return out
    }

    // Byte-level codepoints (v1: one int per byte, 0..255).
    //   "AB".chars() -> [65, 66]
    fn chars(&self) -> Vec(int) {
        Vec(int) out = {}
        int n = self.length
        int i = 0
        while i < n {
            out.push(self.at_unsafe(i))
            i = i + 1
        }
        return out
    }

    // Join `parts` using `self` as the separator (reverse of split):
    //   ",".join(["a", "b", "c"]) -> "a,b,c"
    fn join(&self, &Vec(string) parts) -> string {
        string result = ""
        int n = parts.len()
        int i = 0
        while i < n {
            if i > 0 { result.append(self) }
            string part = parts.get(i)
            result.append(part)
            i = i + 1
        }
        return result
    }
}
