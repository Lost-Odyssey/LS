// M-4 审计矩阵：覆盖 M-3 未涵盖的所有权转移站点
// 目标：memcheck OK clean（0 leak / 0 dfree / 0 ifree）
//
// 已由 M-3 cg_store_owned 处理（不在本测试范围）：
//   Vec.push / Vec.insert / Vec[i]= / enum ctor payload / struct ctor field
//
// 本测试覆盖：
//   1. AST_VAR_DECL × {string, struct, enum, Vec, map}
//   2. AST_ASSIGN var × {string, struct, enum, Vec, map}
//   3. AST_ASSIGN field.x × {string, struct}
//   4. AST_RETURN × {string, struct, enum, Vec, map}
//   5. Vec 字面量 + map 字面量 + map.set / map[k]=
//   6. 函数实参 → 形参 (by-value move)
//   7. match arm binder × {string, struct, enum}

import std.vec
import std.map

struct Item {
    string name
    int qty
}

enum Box {
    Empty
    One(string)
    Pair(string, int)
}

enum Holder {
    Some(Item)
    Nil
}

fn mk_str() -> string { return "rvalue".upper() }
fn mk_item() -> Item { return Item{name: "from-fn".upper(), qty: 7} }
fn mk_box() -> Box  { return One("box-rval".upper()) }
fn mk_vec() -> Vec(string) {
    Vec(string) v = ["a".upper(), "b".upper()]
    return v
}
fn mk_map() -> Map(string, int) {
    Map(string, int) m = {}
    m.set("k".upper(), 1)
    return m
}

// 接收 string by-value（move 语义）
fn take_str(string s) -> int {
    print(s)
    return 1
}

// 接收 struct by-value
fn take_item(Item it) -> int {
    print(it.name)
    return it.qty
}

fn main() -> int {
    // ========== 1. AST_VAR_DECL ==========
    string s_var = mk_str()
    print(s_var)

    Item it_var = mk_item()
    print(it_var.name)

    Box bx_var = mk_box()
    match bx_var {
        Empty => { print("e") }
        One(x) => { print(x) }
        Pair(x, n) => { print(x) }
    }

    Vec(string) v_var = mk_vec()
    print(v_var[0])

    Map(string, int) m_var = mk_map()
    print("m_var ok")

    // ========== 2. AST_ASSIGN var (=) ==========
    string s_asn = "init".copy()
    s_asn = mk_str()           // string rvalue 赋给已存在变量
    print(s_asn)

    // ========== 3. AST_ASSIGN field.x ==========
    Item it_asn = Item{name: "old".copy(), qty: 1}
    it_asn.name = mk_str()      // string rvalue 赋给 struct.field
    print(it_asn.name)

    string fresh = "FRESH".copy()
    it_asn.name = fresh         // string IDENT move 赋给 struct.field
    print(it_asn.name)

    // ========== 4. AST_RETURN ========== (覆盖通过 mk_* 已隐式测试)

    // ========== 5. Vec / map 字面量 + map.set / map[k]= ==========
    Vec(string) v_lit = ["x".upper(), "y".upper(), "z".upper()]
    print(v_lit[2])

    Map(string, int) m_lit = { "hello": 100, "world": 200 }
    print("m_lit ok")

    Map(string, int) m2 = {}
    m2.set("alpha".upper(), 1)         // map.set with string rvalue key
    string mk = "beta".upper()
    m2.set(mk, 2)                       // map.set with string IDENT key (move)
    print("m2 ok")

    Map(string, string) m3 = {}
    m3.set("k1".copy(), "v1".upper())   // string rvalue value
    string mv = "v2".upper()
    m3.set("k2".copy(), mv)             // string IDENT value (move)
    print("m3 ok")

    // ========== 6. 函数实参 → 形参 (by-value) ==========
    string s_pass = "pass".upper()
    int n1 = take_str(s_pass)           // string IDENT move 给形参
    print(n1)

    int n2 = take_str(mk_str())         // rvalue 直接传形参
    print(n2)

    Item it_pass = Item{name: "P".upper(), qty: 9}
    int n3 = take_item(it_pass)         // struct IDENT move 给形参
    print(n3)

    // ========== 7. match arm binder ==========
    Box bx_m = One("inner".upper())
    match bx_m {
        Empty => { print("empty") }
        One(x) => { print(x) }                  // bind string from enum
        Pair(x, n) => { print(x) }
    }

    Box bx_p = Pair("pkey".upper(), 42)
    match bx_p {
        Empty => { print("empty") }
        One(x) => { print(x) }
        Pair(x, n) => { print(x); print(n) }    // bind string + int
    }

    // ========== 8. 嵌套容器 ==========
    // Vec(struct has_drop)
    Vec(Item) vit = {}
    vit.push(Item{name: "n1".upper(), qty: 1})
    vit.push(Item{name: "n2".upper(), qty: 2})
    // M-4.5 已修复 "Vec(has_drop T) index field-access clone leak"：
    // Vec[i].field 的临时 struct 深拷贝在语句结束被 drop。
    print(vit[0].name)              // 直接字段访问（M-4.5 修复点）
    Item it0 = vit[0]               // 变量绑定（所有权转移，基线已正确）
    print(it0.name)

    // Vec(Vec(string)) — 暂不测：同样涉及嵌套 vec 借用 bug
    // 已派生独立任务跟踪

    // map(string, Vec(string)) — 暂不测：runtime symbol mismatch bug
    // 已派生独立任务跟踪

    // ========== 9. has_drop struct/enum 整体赋值 ==========
    Item ia = Item{name: "ia".upper(), qty: 1}
    Item ib = Item{name: "ib".upper(), qty: 2}
    ib = ia                              // struct IDENT 赋给 struct IDENT
    print(ib.name)

    Box ba = One("ba".upper())
    Box bb = One("bb".upper())
    bb = ba                              // enum IDENT 赋给 enum IDENT
    match bb {
        Empty => { print("e") }
        One(x) => { print(x) }
        Pair(x, n) => { print(x) }
    }

    // ========== 10. 嵌套 enum payload struct ==========
    Holder h = Some(Item{name: "h".upper(), qty: 99})
    match h {
        Some(it) => { print(it.name) }
        Nil      => { print("nil") }
    }

    print("done")
    return 0
}
