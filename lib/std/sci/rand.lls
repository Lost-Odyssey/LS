// std/rand.ls — small, fast pseudo-random generator (xorshift64*) for LS.
//
// Follows LS's own style (not a copy of Python random / Rust rand naming): an
// explicitly seedable generator Rng (holds u64 state, no global state), with
// concise, expressive method names —
//   seed(s) -> Rng     seed a generator (free function)
//   r.unit()           next f64 in [0,1) (the "unit interval")
//   r.between(lo, hi)  next int in [lo, hi)
//   r.normal()         next standard normal N(0,1) (Box–Muller)
//   r.next_u64()       low-level step: raw u64 (advanced use)
// Bulk fills for tensor initialization:
//   uniforms(&!r, n) -> Vec(f64)   n values in [0,1)
//   normals(&!r, n)  -> Vec(f64)   n values N(0,1)
//   — pair with Tensor.init_from(shape, vec) for a random tensor.
//
// All constants stay within i64-representable range (top bit 0) to avoid u64
// literal overflow.

import std.core.vec
import std.core.math as math

struct Rng { u64 state }

// xorshift64* multiplier (< 2^63, i64-safe).
def rng_mul() -> u64 { return 0x2545F4914F6CDD1D as u64 }

// Seed: scatter the seed into a non-zero state (the generator mixes further).
def seed(int s) -> Rng {
    u64 z = (s as u64) * rng_mul()
    z = z + (0x123456789ABCDEF as u64)
    z = z ^ (z >> (33 as u64))
    z = z * rng_mul()
    z = z ^ (z >> (29 as u64))
    if z == (0 as u64) { z = rng_mul() }
    return Rng{ state: z }
}

methods Rng {
    // Low-level step: xorshift64*, advance the state and return a scrambled u64.
    def next_u64(&!self) -> u64 {
        u64 x = self.state
        x = x ^ (x >> (12 as u64))
        x = x ^ (x << (25 as u64))
        x = x ^ (x >> (27 as u64))
        self.state = x
        return x * rng_mul()
    }

    // f64 in [0,1): take the high 53 bits / 2^53.
    def unit(&!self) -> f64 {
        u64 bits = self.next_u64() >> (11 as u64)
        return (bits as f64) / 9007199254740992.0
    }

    // int in [lo, hi) (requires hi > lo). Modulo bias is negligible for v1.
    def between(&!self, int lo, int hi) -> int {
        int span = hi - lo
        if span <= 0 {
            @print(f"Rng.between empty range [{lo},{hi})")
            std.sys.c.abort()
        }
        u64 m = self.next_u64() % (span as u64)
        return lo + (m as int)
    }

    // Standard normal N(0,1), Box–Muller.
    def normal(&!self) -> f64 {
        f64 u1 = self.unit()
        if u1 < 0.000000000001 { u1 = 0.000000000001 }
        f64 u2 = self.unit()
        f64 mag = math.sqrt(0.0 - 2.0 * math.log(u1))
        return mag * math.cos(6.283185307179586 * u2)
    }
}

// n values of f64 in [0,1) (uniform random init).
def uniforms(&!Rng r, int n) -> Vec(f64) {
    Vec(f64) v = {}
    int i = 0
    while i < n { v.push(r.unit()); i = i + 1 }
    return v
}

// n values of standard-normal f64 (common for weight init).
def normals(&!Rng r, int n) -> Vec(f64) {
    Vec(f64) v = {}
    int i = 0
    while i < n { v.push(r.normal()); i = i + 1 }
    return v
}
