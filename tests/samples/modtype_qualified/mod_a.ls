module mod_a

struct Config { int a }

fn make() -> Config { return Config { a: 1 } }
fn get(Config c) -> int { return c.a }
