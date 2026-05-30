module mod_b

fn tag(T)(T x) -> int { return 2 }

fn run_tag() -> int { return tag(int)(0) }
