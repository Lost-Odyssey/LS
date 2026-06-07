// Named functions can be used where Block(...) values are expected.

import std.vec
import fn_as_block_util as util

type IntOp = Block(int) -> int
type IntPair = Block(int, int) -> int

fn inc(int x) -> int {
    return x + 1
}

fn desc(int a, int b) -> int {
    return b - a
}

fn even(int x) -> bool {
    return x % 2 == 0
}

fn add(int a, int b) -> int {
    return a + b
}

fn apply1(int x, IntOp f) -> int {
    return f(x)
}

fn apply2(int x, int y, IntPair f) -> int {
    return f(x, y)
}

fn make_inc() -> IntOp {
    return inc
}

fn main() {
    int a = apply1(41, inc)
    if a != 42 {
        print("FAIL: fn arg as Block")
        return
    }

    IntPair plus = add
    int b = plus(20, 22)
    if b != 42 {
        print("FAIL: fn init as Block")
        return
    }

    IntOp f = make_inc()
    int c = f(41)
    if c != 42 {
        print("FAIL: fn return as Block")
        return
    }

    Vec(int) nums = [3, 1, 4, 2]
    nums.sort_by(desc)
    if nums[0] != 4 || nums[3] != 1 {
        print("FAIL: sort_by named comparator")
        return
    }

    Vec(int) evens = nums.filter(even)
    if evens.len() != 2 || evens[0] != 4 || evens[1] != 2 {
        print("FAIL: filter named predicate")
        return
    }

    int d = apply2(19, 23, add)
    if d != 42 {
        print("FAIL: two-arg fn arg as Block")
        return
    }

    int e = apply2(20, 22, util.add)
    if e != 42 {
        print("FAIL: module fn arg as Block")
        return
    }

    IntOp mod_inc = util.inc
    int f2 = mod_inc(41)
    if f2 != 42 {
        print("FAIL: module fn init as Block")
        return
    }

    print("ALL PASS")
}
