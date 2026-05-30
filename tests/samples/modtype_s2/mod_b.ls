module mod_b

struct Config {
    int x
    int y
    int z
}

fn make() -> Config {
    Config c
    c.x = 10
    c.y = 20
    c.z = 30
    return c
}
