// type_name_distinct_reject.ls — a type-mismatch error involving a nested Block
// type must show the REAL expected and got names. Regression for the type_name
// static-buffer self-clobber: building a Block(...) name recursively used to
// wrap the rotating pool back onto the half-built outer slot, so `expected %s
// got %s` printed the SAME (corrupted) name for both. Here `bad` is def(int,int)
// passed where Block(&int,&int)->int is expected; the error must contain the
// distinct `got 'def(int, int) -> int'`.
import std.core.vec

def bad(int a, int b) -> int { return a - b }

def main() -> int {
    Vec(int) v = [3, 1, 2]
    v.sort_by(bad)
    return 0
}
