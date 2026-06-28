// P5-4 S-1 negative smoke: the builtin `string` type keyword was removed —
// `string` is a plain identifier now, so a `string x` declaration must be
// rejected with a clear "unknown type 'string'" error (use Str instead).
def main() {
    string s = "hello"
    @print(s)
}
