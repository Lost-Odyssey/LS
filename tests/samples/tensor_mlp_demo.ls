// tensor_mlp_demo.ls — std.tensor 端到端 demo：一个两层 MLP 分类器的前向。
//
// 把整套栈串起来跑真东西：std.rand 随机初始化权重 → 一行构造 zeros/full/from_vec →
// 广播 bias 的 matmul → relu 隐藏层 → softmax 输出 → argmax_rows 取预测类别。
// 既是 showcase，也是集成测试：seed 决定性，断言结构性质（形状、概率分布合法、
// 预测在类别范围内）。JIT+AOT+memcheck 0/0/0。
//
// 注意：float 激活（relu/softmax）内部用 math.*，但调用方无需 import math
// （编译器把 math 作 ambient builtin 模块解析）。生成式构造 zeros/from_vec 等
// 是泛型自由函数，按 LS 约定以裸名调用：`from_vec(f64)(sh, data)`。

import std.str
import std.vec
import std.tensor
import std.rand as rand

fn check(bool ok, Str l) { if ok { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {
    // 4 个样本、3 维特征的固定输入 X[4,3]（确定性）。
    Vec(int) xsh = [4, 3]
    Vec(f64) xd = [0.5, -1.0, 2.0,
                   1.5,  0.0, -0.5,
                  -2.0,  1.0, 0.5,
                   0.3,  0.7, -1.2]
    Tensor(f64) X = from_vec(f64)(xsh, xd)

    // 随机初始化两层权重（seed 决定性）：3 -> 5 (hidden) -> 2 (classes)。
    Rng r = rand.seed(1234)
    Vec(int) w1sh = [3, 5]
    Vec(f64) w1d = rand.normals(&!r, 15)
    Tensor(f64) W1 = from_vec(f64)(w1sh, w1d)
    Vec(int) b1sh = [5]
    Vec(f64) b1d = rand.normals(&!r, 5)
    Tensor(f64) b1 = from_vec(f64)(b1sh, b1d)

    Vec(int) w2sh = [5, 2]
    Vec(f64) w2d = rand.normals(&!r, 10)
    Tensor(f64) W2 = from_vec(f64)(w2sh, w2d)
    Vec(int) b2sh = [2]
    Vec(f64) b2d = rand.normals(&!r, 2)
    Tensor(f64) b2 = from_vec(f64)(b2sh, b2d)

    // ---- 前向：H = relu(X @ W1 + b1);  logits = H @ W2 + b2;  P = softmax(logits) ----
    Tensor(f64) Z1 = X.matmul(W1)               // [4,5]
    Tensor(f64) Z1b = Z1.add(b1)                // 广播 bias 到每行
    Tensor(f64) H = Z1b.relu()                  // [4,5] 隐藏激活
    check(H.dim(0) == 4 && H.dim(1) == 5, "hidden H shape [4,5]")

    Tensor(f64) Z2 = H.matmul(W2)               // [4,2]
    Tensor(f64) logits = Z2.add(b2)             // 广播 bias
    check(logits.dim(0) == 4 && logits.dim(1) == 2, "logits shape [4,2]")

    Tensor(f64) P = logits.softmax_rows()       // [4,2] 每行概率分布
    check(P.dim(0) == 4 && P.dim(1) == 2, "probs shape [4,2]")

    // 概率合法：每行和≈1，每个元素∈[0,1]。
    Tensor(f64) rowsum = P.sum_axis(1)          // [4]
    bool sums_ok = true
    int i = 0
    while i < 4 {
        f64 s = rowsum.get!(i)
        if s < 0.999999 || s > 1.000001 { sums_ok = false }
        i = i + 1
    }
    check(sums_ok, "every softmax row sums to 1")

    bool probs_ok = true
    int k = 0
    while k < P.size() {
        f64 v = P.get!(k)
        if v < 0.0 || v > 1.0 { probs_ok = false }
        k = k + 1
    }
    check(probs_ok, "every prob in [0,1]")

    // 预测：每个样本取最大概率类别。
    Vec(int) preds = P.argmax_rows()            // 4 个类别索引
    check(preds.len() == 4, "4 predictions")
    bool preds_ok = true
    int j = 0
    while j < 4 {
        int c = preds.get!(j)
        if c < 0 || c >= 2 { preds_ok = false }
        j = j + 1
    }
    check(preds_ok, "every prediction in [0,2)")

    // 打印 showcase 摘要
    print(f"MLP forward: X[4,3] -> relu(H[4,5]) -> softmax(P[4,2])")
    print(f"  sample0 probs: [{P.at2(0,0)}, {P.at2(0,1)}] -> class {preds.get!(0)}")
    print(f"  sample1 probs: [{P.at2(1,0)}, {P.at2(1,1)}] -> class {preds.get!(1)}")

    print("TENSOR_MLP PASS")
}
