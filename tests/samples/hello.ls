// Hello World — basic LS program (C-style type-first syntax)
module main

// 定义判断质数的函数
// 使用 fn 关键字，输入 int，返回 int（1 表示 true，0 表示 false）
fn is_prime(int n) -> int {
    // 基础边界检查
    if (n <= 1) {
        return 0
    }

    // 变量声明采用 C 风格，类型前置
    int i = 2
    
    // 使用 while 循环进行整除测试 [2]
    // 注意：LS 的分号是可选的 [1]
    while (i * i <= n) {
        if (n % i == 0) {
            return 0
        }
        i += 1
    }

    return 1
}

fn main() -> int {

    int n = 317
    print(f"{n} is prime? = {is_prime(n)}")

    return 0
}
