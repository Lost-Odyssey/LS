// tensor_phase3c_test.ls — std.tensor 阶段 3c：std.rand + 随机初始化 + tanh + 按轴 mean/max
//
// 覆盖 std.rand（seed/unit/between/normal 决定性 + 范围）、随机张量初始化
// （uniforms/normals -> init_from）、tanh 激活、mean_axis/max_axis。
// 注意：tanh 内部用 math.exp/math.tanh 但调用方无需 import math（编译器把
// math 作 ambient builtin 模块解析）。JIT+AOT+memcheck 0/0/0。

import std.str
import std.vec
import std.tensor
import std.rand as rand

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // ---- Rng 决定性 + 范围 ----
    Rng a = rand.seed(42)
    Rng b = rand.seed(42)
    f64 ua = a.unit()
    f64 ub = b.unit()
    check(ua == ub, "same seed -> same unit() (deterministic)")
    check(ua >= 0.0 && ua < 1.0, "unit() in [0,1)")

    Rng c = rand.seed(7)
    bool inrange = true
    int trials = 0
    while trials < 200 {
        int x = c.between(10, 20)
        if x < 10 || x >= 20 { inrange = false }
        trials = trials + 1
    }
    check(inrange, "between(10,20) stays in [10,20)")

    // normal() 大样本均值应接近 0（宽松界，决定性 seed 下稳定）
    Rng d = rand.seed(123)
    f64 acc = 0.0
    int ns = 0
    while ns < 2000 { acc = acc + d.normal(); ns = ns + 1 }
    f64 m = acc / 2000.0
    check(m > -0.2 && m < 0.2, "normal() sample mean near 0")

    // ---- 随机张量初始化（uniforms/normals -> init_from）----
    Rng e = rand.seed(99)
    Vec(f64) udata = rand.uniforms(&!e, 6)
    Vec(int) sh = [2, 3]
    Tensor(f64) U = {}
    U.init_from(sh, udata)
    check(U.rank() == 2 && U.size() == 6, "uniform tensor shape [2,3]")
    bool allunit = true
    int ui = 0
    while ui < 6 {
        f64 v = U.get!(ui)
        if v < 0.0 || v >= 1.0 { allunit = false }
        ui = ui + 1
    }
    check(allunit, "uniform tensor values in [0,1)")

    Vec(f64) ndata = rand.normals(&!e, 4)
    Vec(int) sh2 = [4]
    Tensor(f64) N = {}
    N.init_from(sh2, ndata)
    check(N.size() == 4, "normal tensor size 4")

    // ---- tanh（math.tanh）----
    Vec(int) sh22 = [2, 2]
    Vec(f64) za = [0.0, 0.0, 0.0, 0.0]
    Tensor(f64) Z = {}
    Z.init_from(sh22, za)
    Tensor(f64) Th = Z.tanh()
    check(Th.at2(0, 0) == 0.0 && Th.at2(1, 1) == 0.0, "tanh(0)=0")

    // ---- mean_axis / max_axis on A = [[1,2,3],[4,5,6]] ----
    Vec(int) sha = [2, 3]
    Vec(int) aa = [1, 2, 3, 4, 5, 6]
    Tensor(int) A = {}
    A.init_from(sha, aa)

    Tensor(int) cm = A.mean_axis(0)                 // col means [2,3,4] (int div)
    check(cm.dim(0) == 3 && cm.get!(0) == 2 && cm.get!(2) == 4, "mean_axis(0) col means [2,3,4]")
    Tensor(int) rm = A.mean_axis(1)                 // row means [2,5]
    check(rm.dim(0) == 2 && rm.get!(0) == 2 && rm.get!(1) == 5, "mean_axis(1) row means [2,5]")

    Tensor(int) cx = A.max_axis(0)                  // col max [4,5,6]
    check(cx.dim(0) == 3 && cx.get!(0) == 4 && cx.get!(2) == 6, "max_axis(0) col max [4,5,6]")
    Tensor(int) rx = A.max_axis(1)                  // row max [3,6]
    check(rx.dim(0) == 2 && rx.get!(0) == 3 && rx.get!(1) == 6, "max_axis(1) row max [3,6]")

    print("TENSOR_P3C PASS")
}
