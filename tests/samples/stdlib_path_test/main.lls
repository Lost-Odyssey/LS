// Phase E.3.4 — stdlib path resolution
// `import path` uses a SHORT name with no user-relative file, so it must
// resolve via the stdlib fallback <LS_HOME>/lib/std/path.ls (Try 2).
// `import std.core.str` uses a DOTTED name resolving to <LS_HOME>/lib/std/str.ls
// (Try 1). Together they cover both stdlib lookup paths after the
// std/ -> lib/std/ relocation. std.sys.path is pure-LS and deterministic.

import std.sys.path as path
import std.core.str

def main() {
    Str base = path.basename("a/b/c")
    if !base.eq?("c") {
        @print("FAIL: stdlib path.basename expected 'c'")
        @print(base)
        return
    }
    @print("PASS: stdlib path.basename() = c")

    Str e = path.ext("file.txt")
    if !e.eq?(".txt") {
        @print("FAIL: stdlib path.ext expected '.txt'")
        @print(e)
        return
    }
    @print("PASS: stdlib path.ext")

    @print("ALL PASS")
}
