module mod_b

/* Same name as mod_a.Config but a DIFFERENT layout — pre-B-4 this could not
   coexist with mod_a.Config in one program (B-1 errored at import). */
struct Config { int x; int y; int z }

fn make() -> Config { return Config { x: 10, y: 20, z: 30 } }
fn sum(Config c) -> int { return c.x + c.y + c.z }
