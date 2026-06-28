/* B-4: bare reference to `Box` (defined by two imported modules) is ambiguous →
   compile error advising qualification. Import alone is now allowed. */
import mod_a
import mod_b

def main() -> int {
    Box b = mod_a.make(5)   // bare `Box` is ambiguous → compile error
    return 0
}
