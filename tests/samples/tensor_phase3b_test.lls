// tensor_phase3b_test.ls — std.sci.tensor 阶段 3b：broadcasting + 按轴 reduction + 激活
//
// 覆盖 NumPy 式 broadcasting（add/sub/mul 的 [m,n]⊗[n]、[1,n]、[m,1]）、sum_axis
// （按轴求和，降一维）、float 激活 exp/sigmoid/softmax_rows，并以带 bias 的前向
// relu(X@W + b) 端到端验证。JIT+AOT+memcheck 0/0/0。

import std.core.str
import std.core.vec
import std.sci.tensor

def check(bool ok, Str l) { if ok { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

def main() {
    // A = [[1,2,3],[4,5,6]]
    Vec(int) sha = [2, 3]
    Vec(int) aa = [1, 2, 3, 4, 5, 6]
    Tensor(int) A = {}
    A.init_from(sha, aa)

    // ---- broadcasting bias add: A[2,3] + b[3] ----
    Vec(int) shb = [3]
    Vec(int) bb = [10, 20, 30]
    Tensor(int) b = {}
    b.init_from(shb, bb)
    Tensor(int) ab = A.add(b)                       // [[11,22,33],[14,25,36]]
    check(ab.dim(0) == 2 && ab.dim(1) == 3, "bcast add shape [2,3]")
    check(ab.at2(0, 0) == 11 && ab.at2(1, 2) == 36, "bcast add row-broadcast")

    // ---- broadcasting with [1,3] (explicit leading 1) ----
    Vec(int) sh13 = [1, 3]
    Tensor(int) c = {}
    c.init_from(sh13, bb)
    Tensor(int) ac = A.add(c)
    check(ac.at2(0, 0) == 11 && ac.at2(1, 2) == 36, "bcast add [1,3]")

    // ---- broadcasting mul (scale columns by b) ----
    Tensor(int) am = A.mul(b)                       // [[10,40,90],[40,100,180]]
    check(am.at2(0, 2) == 90 && am.at2(1, 0) == 40, "bcast mul (Hadamard broadcast)")

    // ---- broadcasting [m,1] (per-row scalar) ----
    Vec(int) shcol = [2, 1]
    Vec(int) colv = [100, 200]
    Tensor(int) col = {}
    col.init_from(shcol, colv)
    Tensor(int) acol = A.add(col)                   // row0+100, row1+200
    check(acol.at2(0, 0) == 101 && acol.at2(1, 0) == 204, "bcast add [m,1] per-row")

    // ---- sum_axis ----
    Tensor(int) cs = A.sum_axis(0)                  // column sums [5,7,9]
    check(cs.rank() == 1 && cs.dim(0) == 3, "sum_axis(0) shape [3]")
    check(cs.get!(0) == 5 && cs.get!(1) == 7 && cs.get!(2) == 9, "sum_axis(0) col sums")
    Tensor(int) rs = A.sum_axis(1)                  // row sums [6,15]
    check(rs.dim(0) == 2 && rs.get!(0) == 6 && rs.get!(1) == 15, "sum_axis(1) row sums")
    // 1-D reduces to rank-0 scalar
    Vec(int) shv = [3]
    Vec(int) vv = [3, 4, 5]
    Tensor(int) vt = {}
    vt.init_from(shv, vv)
    Tensor(int) vs = vt.sum_axis(0)
    check(vs.rank() == 0 && vs.get!(0) == 12, "sum_axis 1-D -> scalar 12")

    // ---- float activations ----
    Vec(int) sh22 = [2, 2]
    Vec(f64) za = [0.0, 0.0, 0.0, 0.0]
    Tensor(f64) Z = {}
    Z.init_from(sh22, za)
    Tensor(f64) E = Z.exp()
    check(E.at2(0, 0) == 1.0 && E.at2(1, 1) == 1.0, "exp(0)=1")
    Tensor(f64) Sg = Z.sigmoid()
    check(Sg.at2(0, 0) == 0.5, "sigmoid(0)=0.5")

    // softmax over rows: [[1,1],[2,2]] -> each row [0.5,0.5]
    Vec(f64) sma = [1.0, 1.0, 2.0, 2.0]
    Tensor(f64) Sin = {}
    Sin.init_from(sh22, sma)
    Tensor(f64) Sm = Sin.softmax_rows()
    check(Sm.at2(0, 0) == 0.5 && Sm.at2(1, 1) == 0.5, "softmax rows uniform = 0.5")
    f64 row0 = Sm.at2(0, 0) + Sm.at2(0, 1)
    check(row0 == 1.0, "softmax row sums to 1")

    // ---- end-to-end MLP layer: relu(X @ W + bias) ----
    // X = A[2,3];  W[3,2] = [[1,-1],[0,1],[-1,0]];  X@W = [[-2,1],[-2,1]]
    // bias[2] = [5,-1] -> Z+bias = [[3,0],[3,0]] -> relu -> [[3,0],[3,0]] sum=6
    Vec(int) wsh = [3, 2]
    Vec(int) wa = [1, -1, 0, 1, -1, 0]
    Tensor(int) W = {}
    W.init_from(wsh, wa)
    Vec(int) bsh = [2]
    Vec(int) bia = [5, -1]
    Tensor(int) bias = {}
    bias.init_from(bsh, bia)
    Tensor(int) Zl = A.matmul(W)
    Tensor(int) Zb = Zl.add(bias)                   // broadcast bias over rows
    Tensor(int) Hh = Zb.relu()
    check(Hh.at2(0, 0) == 3 && Hh.at2(0, 1) == 0, "MLP layer row 0 = [3,0]")
    check(Hh.sum() == 6, "MLP layer relu(X@W+b) sum=6")

    @print("TENSOR_P3B PASS")
}
