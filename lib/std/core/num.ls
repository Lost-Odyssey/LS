// std/num.ls — numeric building-block traits.
//
// `Zero` provides the additive identity of a type as a STATIC trait method, so a
// generic body can name a type parameter's zero without `0 as T` (which only works
// for numeric scalars, not structs like Complex). Inside a monomorphized generic
// method, `T.zero()` dispatches on the concrete T:
//
//     T acc = T.zero()          // T=int  -> int.zero()  -> 0
//                               // T=f64  -> f64.zero()  -> 0.0
//                               // T=Complex(f64) -> Complex(f64).zero() -> {0,0}
//
// Scalar impls live here. Complex provides a call-compatible `static def zero()` in
// its own inherent impl (generic trait impls are not yet supported), so `T.zero()`
// dispatches there too — duck-typed, no `where T: Zero` bound required (matching
// std.sci.tensor's bound-free generic arithmetic).

interface Zero { static def zero() -> Self }

methods int: Zero { static def zero() -> Self { return 0 } }
methods i64: Zero { static def zero() -> Self { return 0 as i64 } }
methods f64: Zero { static def zero() -> Self { return 0.0 } }

// `One` is the multiplicative identity, the dual of Zero — same duck-typed
// static dispatch (`T.one()` inside a monomorphized generic body), used to seed
// a product fold (e.g. Vec(T).product()). Scalar impls here; a struct numeric
// type can provide a call-compatible `static def one()` in its own impl.
interface One { static def one() -> Self }

methods int: One { static def one() -> Self { return 1 } }
methods i64: One { static def one() -> Self { return 1 as i64 } }
methods f64: One { static def one() -> Self { return 1.0 } }
