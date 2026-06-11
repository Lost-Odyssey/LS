// Str method-port verification: the common builtin-string methods reimplemented
// as pure-LS `impl Str` over the byte buffer (find/contains?/starts_with?/
// ends_with?/substr/upper/lower/trim/concat/repeat). All return owned Str or a
// bool/int; borrowed args use &Str pointer ABI. JIT+AOT+memcheck 0/0/0.
import std.str

fn check(bool ok, string what) {
    if !ok { print(f"STRM FAIL: {what}") }
}

fn main() {
    // NOTE (pre-P5): a literal passed to a `&Str` param now DOES coerce (see
    // str_lit_borrow.ls). Still pending: a bare string-literal *receiver*
    // (`"x".trim()`) — no expected type at the receiver, so the literal stays
    // builtin string until P5 flips the default. We bind literals to Str vars here.
    Str s = "Hello, World"
    Str comma = ", "
    Str lo = "hello"
    Str wo = "World"
    Str hello = "Hello"
    Str zzz = "zzz"

    // find / contains? (Str-var args auto-borrow into &Str)
    check(s.find(comma) == 5, "find")
    check(s.find(wo) == 7, "find World")
    check(s.find(zzz) == -1, "find miss")
    check(s.contains?(wo), "contains")
    check(!s.contains?(zzz), "contains miss")

    // starts_with? / ends_with?
    check(s.starts_with?(hello), "starts")
    check(!s.starts_with?(wo), "starts no")
    check(s.ends_with?(wo), "ends")
    check(!s.ends_with?(hello), "ends no")

    // substr (owned)
    Str hi = s.substr(0, 5)
    check(hi.eq?("Hello"), "substr")
    Str clip = s.substr(7, 999)            // lenient clamp
    check(clip.eq?("World"), "substr clamp")
    Str none = s.substr(100, 5)            // out-of-range start -> empty
    check(none.empty?(), "substr oob")

    // upper / lower (ASCII)
    check(lo.upper().eq?("HELLO"), "upper")
    check(wo.lower().eq?("world"), "lower")

    // trim
    Str padded = "  spaced \t"
    Str nows = "nows"
    check(padded.trim().eq?("spaced"), "trim")
    check(nows.trim().eq?("nows"), "trim none")

    // concat (owned)
    Str greet = lo.concat(comma)
    Str full = greet.concat(wo)
    check(full.eq?("hello, World"), "concat")

    // repeat
    Str ab = "ab"
    Str x = "x"
    check(ab.repeat(3).eq?("ababab"), "repeat")
    check(x.repeat(0).empty?(), "repeat zero")

    // chained owned rvalues drop clean (memcheck guards this)
    check(s.substr(0, 5).lower().eq?("hello"), "chain")

    // borrowed args don't consume their source
    check(s.contains?(wo), "no consume")
    check(wo.eq?("World"), "src alive")

    print("STRM PASS")
}
