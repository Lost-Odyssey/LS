// B-5 regression: two import spellings that resolve to the SAME stdlib file
// must be loaded / type-checked / code-generated ONCE, and calls through either
// spelling must reach that single emitted copy.
//
//   `import std.text.strconv`  resolves to lib/std/text/strconv.ls (Try 1)
//   `import text.strconv`      resolves to lib/std/<rel> = lib/std/text/strconv.ls (Try 2)
//
// Pre-fix: registered twice (keyed by spelling) → int_to_hex emitted under both
// `std_text_strconv__` and `text_strconv__` prefixes. Post-fix: deduped by the
// resolved file → only the canonical (first-loaded) `std_text_strconv__` prefix
// exists, and the alias call mangles to it.
import std.text.strconv
import text.strconv as sc

def main() -> int {
    Str a = std.text.strconv.int_to_hex(255)   // canonical-path call
    Str b = sc.int_to_hex(255)                  // alias for the 2nd spelling
    @print(a)
    @print(b)
    @print("MODULE_DEDUP PASS")
    return 0
}
