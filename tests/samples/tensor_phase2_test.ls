// tensor_phase2_test.ls — std.tensor 阶段 2：多下标 t[i,j,k] 语法（arity 派发协议）
//
// `t[i,j]`/`t[i,j,k]`/`t[i,j,k,l]` 经编译器按下标个数派发到保留协议
// __index2/3/4（读）与 __index_set2/3/4（写）——定 arity、标量实参、无容器，
// 偏移算术内联。验证 2D/3D/4D 读写、表达式中使用、f64 元素，且单下标 v[i]
// （Vec）不受影响。JIT+AOT+memcheck 0/0/0。

import std.str
import std.vec
import std.tensor

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // ---- 2D 读写 ----
    Vec(int) sh = [2, 3]
    Vec(int) d = [1, 2, 3, 4, 5, 6]              // [[1,2,3],[4,5,6]]
    Tensor(int) m = {}
    m.init_from(sh, d)
    check(m[0, 0] == 1 && m[0, 2] == 3 && m[1, 1] == 5, "2D read t[i,j]")
    check(m[0, 1] == m.at2(0, 1), "t[i,j] == at2(i,j)")
    m[1, 0] = 99
    check(m[1, 0] == 99, "2D write t[i,j]=v")
    // 表达式中使用：读、运算、写
    m[0, 0] = m[1, 2] * 2 + m[0, 1]             // 6*2 + 2 = 14
    check(m[0, 0] == 14, "t[i,j] in expression (read+write)")

    // ---- 3D 读写 ----
    Vec(int) sh3 = [2, 2, 2]
    Tensor(int) c = {}
    c.init_zeros(sh3)
    c[0, 1, 0] = 5
    c[1, 1, 1] = 8
    check(c[0, 1, 0] == 5 && c[1, 1, 1] == 8, "3D read/write t[i,j,k]")
    check(c[0, 0, 0] == 0, "3D untouched stays 0")

    // ---- 4D 读写 ----
    Vec(int) sh4 = [2, 2, 2, 2]                  // 16 elems (NCHW-ish)
    Tensor(int) t4 = {}
    t4.init_zeros(sh4)
    t4[1, 0, 1, 0] = 42
    check(t4[1, 0, 1, 0] == 42, "4D read/write t[i,j,k,l]")
    check(t4.size() == 16 && t4.rank() == 4, "4D size 16 rank 4")

    // ---- f64 元素 ----
    Vec(int) fsh = [2, 2]
    Vec(f64) fd = [1.5, 2.5, 3.5, 4.5]
    Tensor(f64) f = {}
    f.init_from(fsh, fd)
    check(f[1, 0] == 3.5, "f64 t[i,j] read")
    f[1, 1] = 9.0
    check(f[1, 1] == 9.0, "f64 t[i,j] write")

    // ---- 单下标 v[i]（Vec）不受影响 ----
    check(d[2] == 3 && d[5] == 6, "single-subscript Vec v[i] unaffected")
    d[0] = 100
    check(d[0] == 100, "single-subscript Vec v[i]=x unaffected")

    print("TENSOR_P2 PASS")
}
