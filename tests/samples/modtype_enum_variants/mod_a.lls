module mod_a

/* Same enum name AND same variant names as mod_b — the hard case. */
enum Res { Ok, Err }
def good() -> Res { return Ok }
def code(Res r) -> int { match r { Ok => { return 1 } Err => { return 2 } } }
