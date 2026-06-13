// std/rand.ls — small, fast pseudo-random generator (xorshift64*) for LS.
//
// 设计走 LS 自己的风格（不照搬 Python random / Rust rand 的命名）：一个可显式
// 播种的生成器 Rng（持 u64 状态、无全局状态），方法名简约而有表现力——
//   seed(s) -> Rng     播种一个生成器（自由函数）
//   r.unit()           下一个 [0,1) 的 f64（"单位区间"）
//   r.between(lo, hi)  下一个 [lo, hi) 的 int
//   r.normal()         下一个标准正态 N(0,1)（Box–Muller）
//   r.next_u64()       底层一步：原始 u64（高级用途）
// 批量喂给张量初始化：
//   uniforms(&!r, n) -> Vec(f64)   n 个 [0,1)
//   normals(&!r, n)  -> Vec(f64)   n 个 N(0,1)
//   —— 配 Tensor.init_from(shape, vec) 即得随机张量。
//
// 常量均控制在 i64 可表示范围内（高位为 0），避免 u64 字面量溢出。

import std.vec
import math

struct Rng { u64 state }

// xorshift64* 的乘子（< 2^63，i64 安全）。
fn rng_mul() -> u64 { return 0x2545F4914F6CDD1D as u64 }

// 播种：把 seed 打散成非零状态（生成器自身会进一步混合）。
fn seed(int s) -> Rng {
    u64 z = (s as u64) * rng_mul()
    z = z + (0x123456789ABCDEF as u64)
    z = z ^ (z >> (33 as u64))
    z = z * rng_mul()
    z = z ^ (z >> (29 as u64))
    if z == (0 as u64) { z = rng_mul() }
    return Rng{ state: z }
}

impl Rng {
    // 底层一步：xorshift64*，推进状态并返回打散后的 u64。
    fn next_u64(&!self) -> u64 {
        u64 x = self.state
        x = x ^ (x >> (12 as u64))
        x = x ^ (x << (25 as u64))
        x = x ^ (x >> (27 as u64))
        self.state = x
        return x * rng_mul()
    }

    // [0,1) 的 f64：取高 53 位 / 2^53。
    fn unit(&!self) -> f64 {
        u64 bits = self.next_u64() >> (11 as u64)
        return (bits as f64) / 9007199254740992.0
    }

    // [lo, hi) 的 int（要求 hi > lo）。模偏置在 v1 可忽略。
    fn between(&!self, int lo, int hi) -> int {
        int span = hi - lo
        if span <= 0 {
            print(f"Rng.between empty range [{lo},{hi})")
            std.c.abort()
        }
        u64 m = self.next_u64() % (span as u64)
        return lo + (m as int)
    }

    // 标准正态 N(0,1)，Box–Muller。
    fn normal(&!self) -> f64 {
        f64 u1 = self.unit()
        if u1 < 0.000000000001 { u1 = 0.000000000001 }
        f64 u2 = self.unit()
        f64 mag = math.sqrt(0.0 - 2.0 * math.log(u1))
        return mag * math.cos(6.283185307179586 * u2)
    }
}

// n 个 [0,1) 的 f64（均匀随机初始化）。
fn uniforms(&!Rng r, int n) -> Vec(f64) {
    Vec(f64) v = {}
    int i = 0
    while i < n { v.push(r.unit()); i = i + 1 }
    return v
}

// n 个标准正态 f64（权重初始化常用）。
fn normals(&!Rng r, int n) -> Vec(f64) {
    Vec(f64) v = {}
    int i = 0
    while i < n { v.push(r.normal()); i = i + 1 }
    return v
}
