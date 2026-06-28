// std.core.math — the pure-LS layer, MERGED with (not shadowing) the built-in
// math primitives.
//
// The built-in math module (src/builtins_math.c) carries only what must be
// compiler-emitted: LLVM intrinsics (sqrt/sin/floor/...), direct libm calls
// (tan/atan2/sinh/...), int-polymorphic abs/min/max, and comptime folding.
// Derived helpers — pure arithmetic (angle conversion) or simple compositions
// of primitives (decibels) — live here, in LS.
//
// Merge mechanism: on `import std.core.math` the compiler folds the built-in
// primitives and this file's exports (loaded under the registry name
// "std.core.math", symbols std_core_math__<fn>) into one namespace, so
// math.radians and math.sqrt mix freely (`import std.core.math as math`).
// (A user-supplied math.ls in the importer's own directory is user-priority
//  shadowing — a full replacement, not a merge; that is a separate path.)
//
// This file imports std.core.math to reach primitives (log10/pow/...). During
// the merge the compiler resolves that self-import to primitives-ONLY (a
// re-entrancy guard, see ModuleRegistry.merging_math), so there is no infinite
// recursion — and so derived helpers here can only call primitives, not each
// other. Helpers that need no primitive (radians/degrees) ignore the import.
//
// Add new pure-arithmetic / composite helpers (clamp/lerp/sign/hypot/...) here,
// not to the built-in C table.

import std.core.math as math

// --- angle conversion (pure arithmetic) ---

// degrees -> radians (deg * PI/180)
def radians(f64 deg) -> f64 {
    return deg * 0.017453292519943295
}

// radians -> degrees (rad * 180/PI)
def degrees(f64 rad) -> f64 {
    return rad * 57.29577951308232
}

// --- decibels (power convention: 10*log10) ---

// linear power ratio -> decibels (10 * log10(x))
def to_db(f64 x) -> f64 {
    return 10.0 * math.log10(x)
}

// decibels -> linear power ratio (10^(db/10))
def to_linear(f64 db) -> f64 {
    return math.pow(10.0, db / 10.0)
}
