module mod_a

struct Config {
    int x
}

fn make() -> Config {
    Config c
    c.x = 1
    return c
}
