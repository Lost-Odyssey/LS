// Named functions can be used where Block(...) values are expected.

import std.core.vec
import std.core.str
import fn_as_block_util as util

type IntOp = Block(int) -> int
type IntPair = Block(int, int) -> int

def inc(int x) -> int {
    return x + 1
}

def desc(int a, int b) -> int {
    return b - a
}

def even(int x) -> bool {
    return x % 2 == 0
}

// Element-consuming vec methods (sort_by/filter/...) now lend elements by
// read-only borrow `&T`. A NAMED def used as their Block must take `&T` params —
// which works when T is a struct/enum (e.g. `&Str`). POD `&int` def params are
// not supported, so POD-element vec methods take a closure instead (its `&T`
// param is inferred). These named `&Str` comparators/predicates exercise the
// named-def-as-Block(&T) path for has_drop elements.
def str_longer(&Str a, &Str b) -> int { return a.len() - b.len() }
def str_nonempty(&Str s) -> bool { return s.len() > 0 }

def add(int a, int b) -> int {
    return a + b
}

def apply1(int x, IntOp f) -> int {
    return f(x)
}

def apply2(int x, int y, IntPair f) -> int {
    return f(x, y)
}

def make_inc() -> IntOp {
    return inc
}

def main() {
    int a = apply1(41, inc)
    if a != 42 {
        @print("FAIL: fn arg as Block")
        return
    }

    IntPair plus = add
    int b = plus(20, 22)
    if b != 42 {
        @print("FAIL: fn init as Block")
        return
    }

    IntOp f = make_inc()
    int c = f(41)
    if c != 42 {
        @print("FAIL: fn return as Block")
        return
    }

    // desc / even as by-value Block values (named def → Block, no vec).
    IntPair dcmp = desc
    if dcmp(2, 5) != 3 {
        @print("FAIL: named fn as IntPair value")
        return
    }

    // POD-element vec methods take a closure (its `&int` param is inferred).
    Vec(int) nums = [3, 1, 4, 2]
    nums.sort_by(|a, b| { return b - a })
    if nums[0] != 4 || nums[3] != 1 {
        @print("FAIL: sort_by closure comparator")
        return
    }
    Vec(int) evens = nums.filter(|x| { return x % 2 == 0 })
    if evens.len() != 2 || evens[0] != 4 || evens[1] != 2 {
        @print("FAIL: filter closure predicate")
        return
    }

    // NAMED def as Block(&T) for has_drop element type (&Str): sort + filter.
    Vec(Str) ws = ["bb", "a", "cccc", "ddd"]
    ws.sort_by(str_longer)
    if ws.get(0)!.len() != 1 || ws.get(3)!.len() != 4 {
        @print("FAIL: sort_by named &Str comparator")
        return
    }
    Vec(Str) ne = ws.filter(str_nonempty)
    if ne.len() != 4 {
        @print("FAIL: filter named &Str predicate")
        return
    }

    int d = apply2(19, 23, add)
    if d != 42 {
        @print("FAIL: two-arg fn arg as Block")
        return
    }

    int e = apply2(20, 22, util.add)
    if e != 42 {
        @print("FAIL: module fn arg as Block")
        return
    }

    IntOp mod_inc = util.inc
    int f2 = mod_inc(41)
    if f2 != 42 {
        @print("FAIL: module fn init as Block")
        return
    }

    @print("ALL PASS")
}
