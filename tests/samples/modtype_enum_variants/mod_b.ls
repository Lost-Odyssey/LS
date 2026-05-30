module mod_b

enum Res { Ok, Err }
fn good() -> Res { return Ok }
fn code(Res r) -> int { match r { Ok => { return 10 } Err => { return 20 } } }
