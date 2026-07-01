// tensor_phase0_test.ls — std.sci.tensor 堆 Tensor 阶段 0 地基探针（plan_ndarray_stdlib.md §-1）
//
// 用硬编码 2D int 矩阵 IMat（具体 int 元素 + 堆 *int buffer，验证 NumPy 式堆
// 存储地基；泛型 Tensor(T) + 运行时 rank 留阶段 1）验证：
//   1. 堆 *int data（c.malloc，∏shape 个 int）+ row-major 偏移 at/set
//   2. as_ptr() -> *int 直接返回 buffer 基址（C 互操作核心，堆比内联自然）
//   3. row_ptr(i) -> *int 子数组（行）基址 = &data[i*cols]（任意子块取址）
//   4. move 语义：has_drop struct，`b = a` 移动；.copy() 显式深拷独立
//   5. as_ptr 传真 C 函数（CRT memcpy = c.__ls_bytecopy）整块读写
//
// 全 POD int 元素，data 由 IMat 自有并在 __drop 释放。打印 "ok"/"FAIL"，
// 收尾 "TENSOR_P0 PASS"。memcheck 必须 0/0/0。

import std.core.str
import std.core.vec
import std.sys.c as c

// 堆后端 2D int 矩阵。data 是 rows*cols 个 int 的 malloc buffer，row-major：
// offset = i*cols + j。has_drop（拥有 buffer，__drop 释放）→ move 类型。
struct IMat {
    *int data
    int rows
    int cols
    int size
}

// 构造：分配并零初始化 rows*cols 个 int。
def imat_make(int rows, int cols) -> IMat {
    int n = rows * cols
    *int p = c.malloc(n * sizeof(int)) as *int
    for (int k = 0; k < n; k = k + 1) { p[k] = 0 }
    return IMat{ data: p, rows: rows, cols: cols, size: n }
}

methods IMat {
    def rows(&self) -> int { return self.rows }
    def cols(&self) -> int { return self.cols }
    def size(&self) -> int { return self.size }

    // checked 读（三层安全模型的 checked 层；at!/set! unchecked 留阶段 1）
    def at(&self, int i, int j) -> int {
        if i < 0 || i >= self.rows || j < 0 || j >= self.cols {
            @print(f"IMat index out of bounds: rows={self.rows} cols={self.cols} i={i} j={j}")
            c.abort()
        }
        return self.data[i * self.cols + j]
    }
    def set(&!self, int i, int j, int v) {
        if i < 0 || i >= self.rows || j < 0 || j >= self.cols {
            @print(f"IMat index out of bounds: rows={self.rows} cols={self.cols} i={i} j={j}")
            c.abort()
        }
        self.data[i * self.cols + j] = v
    }

    // 整块 buffer 基址（C 互操作）。data 已是 *int，直接返回，无需取址。
    def as_ptr(&self) -> *int { return self.data }

    // 子数组（第 i 行）基址 = &data[i*cols]，一个真正的 *int 指入 buffer。
    // 这是 NumPy 子块/视图 base 指针的取法（LS 无指针算术，靠 &data[off]）。
    def row_ptr(&self, int i) -> *int {
        return &self.data[i * self.cols]
    }

    // 显式深拷（move 类型 → copy 是 opt-in）。经 CRT memcpy 整块拷贝。
    def copy(&self) -> IMat {
        IMat out = imat_make(self.rows, self.cols)
        c.__ls_bytecopy(out.data as *u8, 0, self.data as *u8, 0,
                        (self.size * sizeof(int)) as int)
        return out
    }
    // 按值读时的深拷钩子（同 Vec.__clone）。

    // 析构：释放自有 buffer。
}

methods IMat: Clone {
    def clone(&self) -> IMat { return self.copy() }
}

methods IMat: Destroy {
    def ~(&!self) { c.free(self.data as *u8) }
}

def check(bool ok, Str l) {
    if ok { @print(f"ok {l}") } else { @print(f"FAIL {l}") }
}

// 纯 LS 函数收 *int + n：模拟 C 端按 int* 接收基址求和（同 ABI）。
def psum(*int p, int n) -> int {
    int s = 0
    for (int i = 0; i < n; i = i + 1) { s = s + p[i] }
    return s
}

def main() {
    // ---- 1. 堆存储 make/at/set（row-major）----
    IMat m = imat_make(3, 2)
    check(m.rows() == 3 && m.cols() == 2 && m.size() == 6, "make 3x2 size 6")
    check(m.at(0, 0) == 0 && m.at(2, 1) == 0, "zero-init")
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) { m.set(i, j, i * 10 + j) }
    }
    // 存储顺序 row-major: [0,1,10,11,20,21]
    check(m.at(0, 1) == 1 && m.at(1, 0) == 10 && m.at(2, 1) == 21, "set/at row-major")

    // ---- 2. as_ptr 基址 + 同 ABI 读回（sum=63）----
    check(psum(m.as_ptr(), m.size()) == 63, "as_ptr psum=63")
    check(m.as_ptr()[2] == 10, "as_ptr[2]=10 (row-major)")

    // ---- 3. 子数组（行）指针 ----
    *int r1 = m.row_ptr(1)              // 第 1 行 [10, 11]
    check(r1[0] == 10 && r1[1] == 11, "row_ptr(1)=[10,11]")
    r1[0] = 100                        // 写穿透回 buffer
    check(m.at(1, 0) == 100, "row_ptr write-through")
    m.set(1, 0, 10)                    // 复原

    // ---- 4. move 语义 + 显式 copy 独立 ----
    IMat b = m.copy()                  // 独立深拷（m、b 都活）
    check(b.at(2, 1) == 21, "copy b.at(2,1)=21")
    b.set(2, 1, 999)
    check(b.at(2, 1) == 999 && m.at(2, 1) == 21, "copy independent")

    IMat src = imat_make(2, 2)
    src.set(0, 0, 7)
    IMat moved = src                   // move: src 之后失效（不可再用）
    check(moved.at(0, 0) == 7, "move target usable (src moved-from)")

    // ---- 5. as_ptr 传真 C 函数（CRT memcpy）整块拷贝 ----
    IMat dst = imat_make(3, 2)
    c.__ls_bytecopy(dst.as_ptr() as *u8, 0, m.as_ptr() as *u8, 0,
                    (m.size() * sizeof(int)) as int)
    check(dst.at(0, 1) == 1 && dst.at(2, 1) == 21, "C memcpy field round-trip")
    check(psum(dst.as_ptr(), 6) == 63, "C memcpy full sum=63")

    @print("TENSOR_P0 PASS")
}
