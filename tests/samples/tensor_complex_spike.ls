// tensor_complex_spike.ls — the head-risk integration: Tensor(Complex(f64)).
// Exercises nested generics (Tensor over Complex over f64), `T.zero()` dispatch to
// the static Complex(f64).zero() inside tensor's generic methods (zeros free fn +
// sum accumulator), and Complex operator overloads inside tensor elementwise add.

import std.str
import std.vec
import std.complex
import std.tensor

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }
fn near(f64 a, f64 b) -> bool { f64 d = a - b; if d < 0.0 { d = 0.0 - d } return d < 0.0000001 }

fn main() {
    Vec(int) sh = [2]

    // init_zeros — T.zero() dispatch inside a generic METHOD (the real tensor path)
    Tensor(Complex(f64)) z = {}
    z.init_zeros(sh)
    Complex(f64) z0 = z.get!(0)
    check(near(z0.re, 0.0) && near(z0.im, 0.0), "init_zeros -> Complex(0,0)")

    // two complex tensors from flat Vec(Complex) via init_from method
    Vec(Complex(f64)) av = []
    av.push(c(f64)(1.0, 2.0))
    av.push(c(f64)(3.0, 4.0))
    Vec(Complex(f64)) bv = []
    bv.push(c(f64)(5.0, 6.0))
    bv.push(c(f64)(7.0, 8.0))
    Tensor(Complex(f64)) a = {}
    a.init_from(sh, av)
    Tensor(Complex(f64)) b = {}
    b.init_from(sh, bv)

    // elementwise add — Complex `+` operator inside tensor's generic method
    Tensor(Complex(f64)) s = a.add(b)
    Complex(f64) s0 = s.get!(0)        // (1+2i)+(5+6i) = 6+8i
    check(near(s0.re, 6.0) && near(s0.im, 8.0), "add[0] = 6+8i")
    Complex(f64) s1 = s.get!(1)        // (3+4i)+(7+8i) = 10+12i
    check(near(s1.re, 10.0) && near(s1.im, 12.0), "add[1] = 10+12i")

    // sum — T.zero() accumulator + Complex `+`
    Complex(f64) tot = a.sum()          // (1+2i)+(3+4i) = 4+6i
    check(near(tot.re, 4.0) && near(tot.im, 6.0), "sum = 4+6i")

    print("TENSOR_COMPLEX PASS")
}
