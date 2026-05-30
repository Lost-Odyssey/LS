/* B-4: importing two modules that both define `Config` is now allowed (no error
   at import). The error fires only on a BARE (unqualified) reference to the
   ambiguous type — the user must qualify it as `mod_a.Config`. The diagnostic
   still contains "multiple imported modules". */
import mod_a
import mod_b

fn main() -> int {
    Config c = mod_a.make()   // bare `Config` is ambiguous → compile error
    return 0
}
