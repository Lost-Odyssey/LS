// struct_field_defaults_test.ls — struct field defaults + partial init (v1).
// Prints "SFDEF PASS" / "SFDEF FAIL: ...".
import std.vec
import std.str

// POD + Str + bool + f64 + negative-literal defaults
struct Cfg {
    int w = 1000
    int h = 400
    Str theme = "rainbow"
    bool grid = true
    f64 margin = 0.05
    int decimals = -1
}

// has_drop struct: Str field has a default, vec field has none (must be explicit)
struct Box {
    Str label = "box"
    Vec(f64) data
}

// field separators: comma / semicolon / newline / mixed / trailing all compile
struct Sa { int a = 1, int b = 2, int c = 3 }      // commas, same line
struct Sb { int a = 4; int b = 5 }                 // semicolons, same line
struct Sc { int a = 6; int b = 7
            int d = 8 }                            // mixed: ';' then newline
struct Sd { int a = 9, int b = 10, }               // trailing comma

fn check(Str got, Str want, Str name) -> bool {
    if got.eq?(want) { return true }
    print(f"SFDEF FAIL: {name} got=[{got}] want=[{want}]")
    return false
}

fn cfg_str(Cfg c) -> Str {
    return f"{c.w},{c.h},{c.theme},{c.grid},{c.margin:.2f},{c.decimals}"
}

// consume a Box by value (move); reads its fields
fn box_sum(Box b) -> Str {
    f64 s = 0.0
    int i = 0
    while i < b.data.len() {
        s = s + b.data[i]
        i = i + 1
    }
    return f"{b.label}:{b.data.len()}:{s:.1f}"
}

fn main() {
    bool ok = true

    // all defaults
    ok = check(cfg_str(Cfg{}), "1000,400,rainbow,true,0.05,-1", "all_defaults") && ok
    // override one (skip middle fields)
    ok = check(cfg_str(Cfg{theme: "viridis"}), "1000,400,viridis,true,0.05,-1", "skip_middle") && ok
    // override several, declaration-order-independent
    ok = check(cfg_str(Cfg{grid: false, w: 1600}), "1600,400,rainbow,false,0.05,-1", "multi_override") && ok
    // all explicit
    ok = check(cfg_str(Cfg{w: 1, h: 2, theme: "x", grid: false, margin: 1.5, decimals: 3}),
               "1,2,x,false,1.50,3", "all_explicit") && ok

    // has_drop struct: label defaults, data explicit (moved in)
    Vec(f64) d1 = [1.0, 2.0, 3.0]
    Box b1 = Box{data: d1}
    ok = check(box_sum(b1), "box:3:6.0", "box_default_label") && ok

    Vec(f64) d2 = [10.0, 20.0]
    Box b2 = Box{label: "custom", data: d2}
    ok = check(box_sum(b2), "custom:2:30.0", "box_explicit_label") && ok

    // separator styles compile + defaults apply
    Sa sa = Sa{}
    Sb sb = Sb{}
    Sc sc = Sc{}
    Sd sd = Sd{}
    ok = check(f"{sa.a}{sa.b}{sa.c}", "123", "sep_comma") && ok
    ok = check(f"{sb.a}{sb.b}", "45", "sep_semicolon") && ok
    ok = check(f"{sc.a}{sc.b}{sc.d}", "678", "sep_mixed") && ok
    ok = check(f"{sd.a}{sd.b}", "910", "sep_trailing") && ok

    if ok { print("SFDEF PASS") }
}
