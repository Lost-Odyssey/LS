module mod_a

struct Config { int a }

def make() -> Config { return Config { a: 1 } }
def get(Config c) -> int { return c.a }
