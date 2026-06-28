// 合并式 std.core.math:内建原语(sqrt/sin/...)+ 纯 LS 派生(radians/degrees,
// lib/std/core/math.ls)并入同一命名空间。验证派生函数走 LS 符号
// (std_core_math__radians)、与原语混用、int 实参扩展;自检打印 "MATHEXT PASS"。
// JIT + AOT + memcheck 三重。
import std.core.math as math

// 浮点近似相等(派生/原语都有舍入)
def near(f64 a, f64 b) -> bool {
    f64 d = a - b
    if d < 0.0 { d = 0.0 - d }
    return d < 0.000001
}

def main() -> int {
    bool ok = true

    // 派生函数(LS 层)
    if !near(math.radians(180.0), math.PI) { @print("FAIL: radians(180)") ok = false }
    if !near(math.degrees(math.PI), 180.0) { @print("FAIL: degrees(PI)") ok = false }
    if !near(math.radians(90.0), math.PI / 2.0) { @print("FAIL: radians(90)") ok = false }
    if !near(math.degrees(math.PI / 2.0), 90.0) { @print("FAIL: degrees(PI/2)") ok = false }

    // round-trip:degrees(radians(x)) == x
    if !near(math.degrees(math.radians(45.0)), 45.0) { @print("FAIL: round-trip") ok = false }

    // 派生与内建原语混用(sin(radians(90)) == 1)
    if !near(math.sin(math.radians(90.0)), 1.0) { @print("FAIL: sin(rad(90))") ok = false }

    // int 实参隐式扩展到 f64
    if !near(math.radians(360), 2.0 * math.PI) { @print("FAIL: radians(360 int)") ok = false }

    // 分贝(功率约定 10*log10)—— 派生函数调内建原语 log10/pow
    if !near(math.to_db(100.0), 20.0) { @print("FAIL: to_db(100)") ok = false }
    if !near(math.to_linear(20.0), 100.0) { @print("FAIL: to_linear(20)") ok = false }
    if !near(math.to_linear(0.0), 1.0) { @print("FAIL: to_linear(0)") ok = false }
    // round-trip:to_linear(to_db(x)) == x
    if !near(math.to_linear(math.to_db(7.0)), 7.0) { @print("FAIL: db round-trip") ok = false }

    if ok { @print("MATHEXT PASS") }
    return 0
}
