// tensor_phase1_test.ls — std.sci.tensor 阶段 1：泛型 Tensor(T) + 运行时 shape/strides
//
// 覆盖：init/init_zeros/init_from 构造（{} + 方法 idiom）、rank/size/dim 元数据、
// 三层 flat 访问（get/set/get!/set!）、多下标 at2/set2/at3/set3、reshape（不移数据
// 改 strides）、for-in 迭代、move + 显式 copy 独立、as_ptr -> *T。JIT+AOT+memcheck。

import std.core.str
import std.core.vec
import std.sci.tensor

def check(bool ok, Str l) { if ok { @print(f"ok {l}") } else { @print(f"FAIL {l}") } }

def psum(*int p, int n) -> int {
    int s = 0
    for (int i = 0; i < n; i = i + 1) { s = s + p[i] }
    return s
}

def main() {
    // ---- 构造 + 元数据（2D 3x2 zeros）----
    Vec(int) sh = [3, 2]
    Tensor(int) m = {}
    m.init_zeros(sh)
    check(m.rank() == 2, "rank=2")
    check(m.size() == 6, "size=6")
    check(m.dim(0) == 3 && m.dim(1) == 2, "dim 3,2")
    check(m.get!(0) == 0 && m.get!(5) == 0, "zeros init")

    // 填 m[i][j] = i*10 + j（多下标 set2）
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) { m.set2(i, j, i * 10 + j) }
    }
    check(m.at2(0, 1) == 1 && m.at2(1, 0) == 10 && m.at2(2, 1) == 21, "at2/set2 row-major")
    // flat 存储序 = [0,1,10,11,20,21]
    check(m.get!(2) == 10 && m.get!(5) == 21, "flat storage order")

    // ---- flat 三层访问 ----
    check(m.get(2).unwrap_or(-1) == 10, "get(2)=Some(10)")
    check(m.get(99).is_none?(), "get(99)=None (out of range)")
    m.set(0, 100)
    check(m.get!(0) == 100, "set(0,100)")
    m.set(0, 0)                               // 复原

    // ---- as_ptr 基址 ----
    check(psum(m.as_ptr(), m.size()) == 63, "as_ptr psum=63")

    // ---- init / fill ----
    Vec(int) sh2 = [2, 2]
    Tensor(f64) f = {}
    f.init(sh2, 1.5)
    check(f.size() == 4, "init fill size=4")
    check(f.at2(0, 0) == 1.5 && f.at2(1, 1) == 1.5, "init fill 1.5")

    // ---- init_from（1D 长度 6）----
    Vec(int) flat = [10, 11, 12, 13, 14, 15]
    Vec(int) sh3 = [6]
    Tensor(int) v = {}
    v.init_from(sh3, flat)
    check(v.rank() == 1 && v.size() == 6, "init_from 1D size 6")
    check(v.get!(0) == 10 && v.get!(5) == 15, "init_from contents")

    // ---- reshape 6 -> 2x3（不移数据，改 strides）----
    Vec(int) rs = [2, 3]
    v.reshape(rs)
    check(v.rank() == 2 && v.dim(0) == 2 && v.dim(1) == 3, "reshape 2x3 meta")
    check(v.at2(0, 0) == 10 && v.at2(1, 2) == 15, "reshape at2 (data preserved)")
    check(v.get!(3) == 13, "reshape flat unchanged")

    // ---- 3D（2x2x2）----
    Vec(int) sh4 = [2, 2, 2]
    Tensor(int) cube = {}
    cube.init_zeros(sh4)
    cube.set3(1, 0, 1, 77)
    check(cube.at3(1, 0, 1) == 77, "3D set3/at3")
    check(cube.size() == 8 && cube.rank() == 3, "3D size 8 rank 3")

    // ---- for-in 迭代（flat 存储序）----
    int total = 0
    for x in m { total = total + x }
    check(total == 63, "for-in flat sum=63")

    // ---- move + 显式 copy 独立 ----
    Tensor(int) b = m.copy()
    b.set2(2, 1, 999)
    check(b.at2(2, 1) == 999 && m.at2(2, 1) == 21, "copy independent")

    Tensor(int) src = {}
    src.init_zeros(sh2)
    src.set!(0, 5)
    Tensor(int) moved = src                    // move src (dead hereafter)
    check(moved.get!(0) == 5, "move target usable")

    @print("TENSOR_P1 PASS")
}
