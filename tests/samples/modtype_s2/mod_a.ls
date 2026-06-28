module mod_a

struct Config {
    int x
}

def make() -> Config {
    Config c
    c.x = 1
    return c
}
