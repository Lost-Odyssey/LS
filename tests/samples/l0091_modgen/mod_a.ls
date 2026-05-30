module mod_a

/* Generic function used INSIDE this module — pre-A1 this was silently dropped
   (instantiation discarded), so use_int() failed with "undefined function". */
fn box(T)(T x) -> T { return x }

/* Same generic name as mod_b.tag but DIFFERENT body — pre-A2 both mangled to
   tag(int) and collided (silent-wrong). */
fn tag(T)(T x) -> int { return 1 }

fn use_int() -> int { return box(int)(7) }
fn run_tag() -> int { return tag(int)(0) }
