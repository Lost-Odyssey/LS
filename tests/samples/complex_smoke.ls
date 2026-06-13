// complex_smoke.ls — std.complex operators + methods (Phase 1). Static zero()/one()
// are exercised via Tensor(Complex) (T.zero()) in the integration spike.

import std.str
import std.complex
import math

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }
fn near(f64 a, f64 b) -> bool { f64 d = a - b; if d < 0.0 { d = 0.0 - d } return d < 0.0000001 }

fn main() {
    Complex(f64) a = {re: 1.0, im: 2.0}
    Complex(f64) b = {re: 3.0, im: -1.0}

    Complex(f64) s = a + b
    check(near(s.re, 4.0) && near(s.im, 1.0), "add (4,1)")

    Complex(f64) d = a - b
    check(near(d.re, -2.0) && near(d.im, 3.0), "sub (-2,3)")

    // (1+2i)(3-i) = 3 - i + 6i - 2i^2 = (3+2) + 5i = 5+5i
    Complex(f64) m = a * b
    check(near(m.re, 5.0) && near(m.im, 5.0), "mul (5,5)")

    // (5+5i)/(3-i) == a == 1+2i
    Complex(f64) q = m / b
    check(near(q.re, 1.0) && near(q.im, 2.0), "div roundtrip (1,2)")

    Complex(f64) cj = a.conj()
    check(near(cj.re, 1.0) && near(cj.im, -2.0), "conj (1,-2)")

    check(near(a.norm(), 5.0), "norm 5")
    check(near(a.abs(), math.sqrt(5.0)), "abs sqrt5")

    // e^(i*pi) = -1  (Euler)
    Complex(f64) ipi = {re: 0.0, im: math.PI}
    Complex(f64) e = ipi.exp()
    check(near(e.re, -1.0) && near(e.im, 0.0), "exp euler -1")

    Complex(f64) cc = c(f64)(7.0, 8.0)
    check(near(cc.re, 7.0) && near(cc.im, 8.0), "c() ctor (7,8)")

    // from_polar(1, pi/2) = i  (free-fn constructor)
    Complex(f64) p = from_polar(f64)(1.0, math.PI / 2.0)
    check(near(p.re, 0.0) && near(p.im, 1.0), "from_polar -> i")

    Complex(f64) r5 = real(f64)(5.0)
    check(near(r5.re, 5.0) && near(r5.im, 0.0), "real(5)")

    print("COMPLEX PASS")
}
