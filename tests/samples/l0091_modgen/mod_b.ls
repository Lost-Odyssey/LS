module mod_b

def tag(T)(T x) -> int { return 2 }

def run_tag() -> int { return tag(int)(0) }
