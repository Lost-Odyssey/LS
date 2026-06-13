// tensor_phase3_test.ls — std.tensor 阶段 3a：数值核心
//
// 覆盖 elementwise（add/sub/mul/div）、scalar（add_scalar/scale）、reduction
// （sum/mean/max/argmax）、matmul（2D）、transpose、relu，并以一个两层前向 demo
// （x @ W -> relu -> sum）端到端验证设计。JIT+AOT+memcheck 0/0/0。

import std.str
import std.vec
import std.tensor

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }

// helper: build a Tensor(int) from a flat list + shape (shape literal doesn't
// coerce to Vec(int) in an arg position, so build the Vecs as locals).
fn main() {
    // ---- elementwise (2x2 int) ----
    Vec(int) sh = [2, 2]
    Vec(int) xa = [1, 2, 3, 4]
    Vec(int) ya = [10, 20, 30, 40]
    Tensor(int) x = {}
    x.init_from(sh, xa)
    Tensor(int) y = {}
    y.init_from(sh, ya)

    Tensor(int) s = x.add(y)
    check(s.at2(0, 0) == 11 && s.at2(1, 1) == 44, "add")
    Tensor(int) d = y.sub(x)
    check(d.at2(0, 0) == 9 && d.at2(1, 1) == 36, "sub")
    Tensor(int) h = x.mul(y)                       // Hadamard
    check(h.at2(0, 1) == 40 && h.at2(1, 0) == 90, "mul (elementwise)")
    Tensor(int) q = y.div(x)
    check(q.at2(0, 0) == 10 && q.at2(1, 1) == 10, "div")

    // ---- scalar ----
    Tensor(int) sc = x.scale(2)
    check(sc.at2(0, 0) == 2 && sc.at2(1, 1) == 8, "scale by 2")
    Tensor(int) ad = x.add_scalar(10)
    check(ad.at2(0, 0) == 11 && ad.at2(1, 1) == 14, "add_scalar 10")

    // ---- reductions (flat over x = [1,2,3,4]) ----
    check(x.sum() == 10, "sum=10")
    check(x.mean() == 2, "mean=2 (int div 10/4)")
    check(x.max() == 4, "max=4")
    check(x.argmax() == 3, "argmax=3")

    // ---- matmul: A[2,3] @ B[3,2] = [2,2] ----
    // A = [[1,2,3],[4,5,6]]   B = [[1,0],[0,1],[1,1]]
    // A@B = [[4,5],[10,11]]
    Vec(int) sha = [2, 3]
    Vec(int) aa = [1, 2, 3, 4, 5, 6]
    Tensor(int) A = {}
    A.init_from(sha, aa)
    Vec(int) shb = [3, 2]
    Vec(int) bb = [1, 0, 0, 1, 1, 1]
    Tensor(int) B = {}
    B.init_from(shb, bb)
    Tensor(int) C = A.matmul(B)
    check(C.rank() == 2 && C.dim(0) == 2 && C.dim(1) == 2, "matmul shape [2,2]")
    check(C.at2(0, 0) == 4 && C.at2(0, 1) == 5, "matmul row 0 = [4,5]")
    check(C.at2(1, 0) == 10 && C.at2(1, 1) == 11, "matmul row 1 = [10,11]")
    check(C.sum() == 30, "matmul sum=30")

    // ---- transpose: A[2,3] -> AT[3,2] = [[1,4],[2,5],[3,6]] ----
    Tensor(int) AT = A.transpose()
    check(AT.dim(0) == 3 && AT.dim(1) == 2, "transpose shape [3,2]")
    check(AT.at2(0, 1) == 4 && AT.at2(2, 0) == 3, "transpose values")

    // ---- relu: [-2,3,-1,5] -> [0,3,0,5], sum=8 ----
    Vec(int) sh4 = [4]
    Vec(int) ra = [-2, 3, -1, 5]
    Tensor(int) rin = {}
    rin.init_from(sh4, ra)
    Tensor(int) ro = rin.relu()
    check(ro.get!(0) == 0 && ro.get!(1) == 3 && ro.get!(2) == 0 && ro.get!(3) == 5, "relu clamps negatives")
    check(ro.sum() == 8, "relu sum=8")

    // ---- end-to-end demo: 2-layer-ish forward  h = relu(X @ W);  out = h.sum() ----
    // X[2,3] = [[1,2,3],[4,5,6]]   W[3,2] = [[1,-1],[0,1],[-1,0]]
    // X@W = [[1*1+2*0+3*-1, 1*-1+2*1+3*0],[4*1+5*0+6*-1, 4*-1+5*1+6*0]]
    //     = [[1+0-3, -1+2+0],[4+0-6, -4+5+0]] = [[-2,1],[-2,1]]
    // relu -> [[0,1],[0,1]]   sum = 2
    Vec(int) wsh = [3, 2]
    Vec(int) wa = [1, -1, 0, 1, -1, 0]
    Tensor(int) W = {}
    W.init_from(wsh, wa)
    Tensor(int) Z = A.matmul(W)                    // A is X[2,3]
    Tensor(int) Hh = Z.relu()
    check(Hh.at2(0, 0) == 0 && Hh.at2(0, 1) == 1, "forward relu row 0 = [0,1]")
    check(Hh.sum() == 2, "forward output sum=2")

    // ---- f64 sanity (generic element type) ----
    Vec(int) fsh = [2, 2]
    Vec(f64) fa = [1.5, 2.5, 3.5, 4.5]
    Tensor(f64) F = {}
    F.init_from(fsh, fa)
    check(F.sum() == 12.0, "f64 sum=12.0")
    Tensor(f64) F2 = F.scale(2.0)
    check(F2.at2(1, 1) == 9.0, "f64 scale 4.5*2=9.0")

    print("TENSOR_P3 PASS")
}
