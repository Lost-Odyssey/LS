// s13_borrow_gaps_test.ls — plan_std_map §13 两挂账回归：
//  ① borrow-match 绑定出的（导入模块实例化）Vec 用 [i] 下标
//     （旧报「cannot index non-array type 'Vec(std_str__Str)'」——
//      消费方 checker 未注册导入泛型实例的 __index，门口缺 VR-LIM-018 回退）
//  ② 显式 `&局部` 实参传 `&T` 参数（旧报「expected '&T', got '*T'」）
import std.core.vec
import std.core.map
import std.text.json as json
import std.core.str

enum Holder {
    Keys(Vec(int)),
    Pair(Vec(Str), Map(Str, int)),
    Empty
}

// ② 显式 & 实参的目标函数们
def sum_vec(&Vec(int) v) -> int {
    int s = 0
    for x in v { s = s + x }
    return s
}

def str_len(&Str s) -> int {
    return s.len()
}

def peek_holder(&Holder h) -> int {
    match h {
        Keys(ks) => { return ks[0] }
        Pair(names, m) => { return m.len() }
        Empty => { return -1 }
    }
    return -2
}

def check(bool cond, Str label) {
    if (cond) { @print(f"PASS {label}") } else { @print(f"FAIL {label}") }
}

def main() -> int {
    // ---- ① 本地枚举 borrow-match binder Vec 下标（单/双载荷） ----
    Vec(int) v1 = [10, 20, 30]
    Holder h1 = Keys(v1)
    match h1 {
        Keys(ks) => {
            check(ks[0] == 10 && ks[2] == 30, "local-binder-vec-index")
        }
        _ => { @print("FAIL local-binder-arm") }
    }

    Vec(Str) names = ["alpha", "beta"]
    Map(Str, int) m = {"alpha": 1, "beta": 2}
    Holder h2 = Pair(names, m)
    match h2 {
        Pair(ns, mm) => {
            check(ns[1].eq?("beta") && mm.len() == 2, "dual-payload-binder-index")
        }
        _ => { @print("FAIL dual-payload-arm") }
    }

    // ---- ① 导入模块实例化的 Vec（std.text.json 的 Object(Vec(Str), Map)）----
    Result(JsonValue, Str) r = json.parse("{\"x\":\"hello\"}")
    JsonValue root
    match r {
        Ok(jv) => { root = jv }
        Err(e) => { @print("FAIL json-parse"); return 1 }
    }
    match root {
        Object(keys, entries) => {
            Str k = keys[0]    // 旧版在此报 type error
            check(k.eq?("x"), "imported-instantiation-vec-index")
            match entries.get(k) {
                Some(val) => { @print("PASS imported-map-get") }
                None => { @print("FAIL imported-map-get") }
            }
        }
        _ => { @print("FAIL json-not-object") }
    }

    // ---- ② 显式 &局部 实参 ----
    Vec(int) v2 = [1, 2, 3]
    int s_explicit = sum_vec(&v2)
    int s_auto = sum_vec(v2)          // auto-borrow 对照
    check(s_explicit == 6 && s_auto == 6, "explicit-amp-vec-arg")
    check(v2.len() == 3, "explicit-amp-source-alive")

    Str txt = "hello"
    check(str_len(&txt) == 5, "explicit-amp-str-arg")
    check(txt.eq?("hello"), "explicit-amp-str-alive")

    Vec(int) v3 = [7]
    Holder h3 = Keys(v3)
    check(peek_holder(&h3) == 7, "explicit-amp-enum-arg")
    check(peek_holder(h3) == 7, "auto-borrow-enum-arg")

    @print("S13_BORROW_GAPS PASS")
    return 0
}
