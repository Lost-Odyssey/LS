// M-4.5: vec[i] 索引访问返回 has_drop struct/enum 的深拷贝。
// 该深拷贝是语句级临时值，若不转移给命名变量必须在语句结束时 drop，
// 否则其拥有的堆资源（string 字段 / enum payload）泄漏。
// 目标：memcheck OK clean（0 leak / 0 dfree / 0 ifree），JIT + AOT 双通道。

struct Item {
    string name
    int qty
}

enum Box {
    Empty
    One(string)
    Pair(string, int)
}

fn main() -> int {
    vec(Item) vit = []
    vit.push(Item{name: "n1".upper(), qty: 1})
    vit.push(Item{name: "n2".upper(), qty: 2})

    // ===== 1. 临时使用：vit[i].field —— 之前的 leak 点 =====
    print(vit[0].name)              // 临时 struct clone，取字段后丢弃 → 必须 drop
    print(vit[1].qty)               // 临时 struct clone，取 POD 字段后丢弃

    // ===== 2. 所有权转移：命名变量接管 =====
    Item it0 = vit[0]               // it0 接管临时 clone，作用域结束 drop
    print(it0.name)
    Item it1 = vit[1]
    print(it1.name)

    // ===== 3. 循环内临时使用 =====
    for i in 0..2 {
        print(vit[i].name)          // 每次迭代产生临时 clone，迭代结束 drop
    }

    // ===== 4. has_drop enum：vec(enum) 索引 =====
    vec(Box) vb = []
    vb.push(One("b0".upper()))
    vb.push(One("b1".upper()))

    // 临时使用：match 直接消费 vb[i]
    match vb[0] {
        Empty => { print("empty") }
        One(x) => { print(x) }
        Pair(x, n) => { print(x) }
    }

    // 所有权转移：命名 enum 变量接管
    Box bx = vb[1]
    match bx {
        Empty => { print("empty") }
        One(x) => { print(x) }
        Pair(x, n) => { print(x) }
    }

    print("done")
    return 0
}
