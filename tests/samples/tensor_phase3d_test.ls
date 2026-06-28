// tensor_phase3d_test.ls — std.sci.tensor 阶段 3d：安全拷贝切片 + 数值广度
//
// 拷贝切片 row/col/slice（own 结果，无视图/生命期 footgun）、argmax_rows（分类）、
// min、逐元素 neg/abs/sqrt/log/clamp、mse 损失。JIT+AOT+memcheck 0/0/0。

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

    // ---- 拷贝切片 ----
    Tensor(int) r1 = A.row(1)                    // [4,5,6]
    check(r1.rank() == 1 && r1.size() == 3, "row(1) shape [3]")
    check(r1.get!(0) == 4 && r1.get!(2) == 6, "row(1) = [4,5,6]")
    Tensor(int) c1 = A.col(1)                    // [2,5]
    check(c1.size() == 2 && c1.get!(0) == 2 && c1.get!(1) == 5, "col(1) = [2,5]")
    // slice 保持 rank
    Tensor(int) sc = A.slice(1, 1, 3)            // cols [1,3) -> [[2,3],[5,6]]
    check(sc.dim(0) == 2 && sc.dim(1) == 2, "slice(axis1,1,3) shape [2,2]")
    check(sc.at2(0, 0) == 2 && sc.at2(1, 1) == 6, "slice values")
    Tensor(int) sr = A.slice(0, 1, 2)            // row [1,2) -> [[4,5,6]]
    check(sr.dim(0) == 1 && sr.at2(0, 2) == 6, "slice(axis0,1,2) = [[4,5,6]]")
    // 拷贝独立：改切片不动源
    sc.set2(0, 0, 999)
    check(A.at2(0, 1) == 2, "slice is an independent copy (source unchanged)")

    // ---- argmax_rows（分类）----
    Vec(int) shb = [2, 3]
    Vec(int) bb = [1, 5, 2, 8, 3, 9]             // [[1,5,2],[8,3,9]]
    Tensor(int) B = {}
    B.init_from(shb, bb)
    Vec(int) am = B.argmax_rows()                // [1, 2]
    check(am.len() == 2 && am.get!(0) == 1 && am.get!(1) == 2, "argmax_rows = [1,2]")

    // ---- min ----
    check(A.min() == 1, "min = 1")

    // ---- 逐元素 ----
    Tensor(int) ng = A.neg()
    check(ng.get!(0) == -1 && ng.get!(5) == -6, "neg")
    Vec(int) sh4 = [4]
    Vec(int) cc = [-2, 3, -1, 5]
    Tensor(int) C = {}
    C.init_from(sh4, cc)
    Tensor(int) ab = C.abs()
    check(ab.get!(0) == 2 && ab.get!(2) == 1 && ab.get!(3) == 5, "abs")
    Vec(int) shd = [3]
    Vec(int) dd = [1, 5, 10]
    Tensor(int) D = {}
    D.init_from(shd, dd)
    Tensor(int) cl = D.clamp(2, 8)               // [2,5,8]
    check(cl.get!(0) == 2 && cl.get!(1) == 5 && cl.get!(2) == 8, "clamp [2,8]")

    // ---- float sqrt/log ----
    Vec(int) shf = [3]
    Vec(f64) fd = [4.0, 9.0, 16.0]
    Tensor(f64) F = {}
    F.init_from(shf, fd)
    Tensor(f64) sq = F.sqrt()
    check(sq.get!(0) == 2.0 && sq.get!(2) == 4.0, "sqrt [2,3,4]")
    Vec(int) sh1 = [1]
    Vec(f64) ld = [1.0]
    Tensor(f64) L = {}
    L.init_from(sh1, ld)
    Tensor(f64) lg = L.log()
    check(lg.get!(0) == 0.0, "log(1)=0")

    // ---- mse 损失 ----
    Vec(int) shm = [2]
    Vec(int) pa = [2, 4]
    Vec(int) pb = [0, 0]
    Tensor(int) P = {}
    P.init_from(shm, pa)
    Tensor(int) Q = {}
    Q.init_from(shm, pb)
    check(P.mse(Q) == 10, "mse([2,4],[0,0]) = (4+16)/2 = 10")

    @print("TENSOR_P3D PASS")
}
