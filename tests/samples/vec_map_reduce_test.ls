// vec_map_reduce_test.ls -- method-level generic map/reduce on Vec(T).
// Tests: int->int, int->Str, Str->Str, Str->int, struct->Str,
//        reduce with POD accumulator, Str accumulator, and struct accumulator.
// All three paths: JIT + AOT + memcheck must pass with 0 leaks.

import std.vec
import std.str

struct Person { Str name; int age }

fn check(bool c, Str l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {

    // ── map: int → int ──────────────────────────────────────────────────────
    Vec(int) nums = [1, 2, 3, 4, 5]
    Vec(int) doubled = nums.map(int)(|x| x * 2)
    check(doubled.len() == 5, "map int->int len")
    check(doubled[0] == 2 && doubled[2] == 6 && doubled[4] == 10, "map int->int vals")
    check(nums.len() == 5, "map src untouched")

    // ── map: int → Str ───────────────────────────────────────────────────────
    Vec(Str) strs = nums.map(Str)(|x| f"n{x}")
    check(strs.len() == 5, "map int->string len")
    check(strs[0].eq?("n1") && strs[2].eq?("n3") && strs[4].eq?("n5"), "map int->string vals")

    // ── map: Str → Str ───────────────────────────────────────────────────────
    Vec(Str) words = [f"apple", f"banana", f"cherry"]
    Vec(Str) upper_words = words.map(Str)(|s| s.upper())
    check(upper_words.len() == 3, "map str->str len")
    check(upper_words[0].eq?("APPLE") && upper_words[2].eq?("CHERRY"), "map str->str vals")
    check(words[0].eq?("apple"), "map str src untouched")

    // ── map: Str → int (length) ──────────────────────────────────────────────
    Vec(int) lens = words.map(int)(|s| s.len())
    check(lens.len() == 3, "map str->int len")
    check(lens[0] == 5 && lens[1] == 6 && lens[2] == 6, "map str->int vals")

    // ── map: int → struct ───────────────────────────────────────────────────
    Vec(int) ages = [20, 30, 40]
    Vec(Person) people = ages.map(Person)(|a| Person { name: f"p{a}", age: a })
    check(people.len() == 3, "map int->struct len")
    check(people[0].name.eq?("p20") && people[0].age == 20, "map int->struct first")
    check(people[2].name.eq?("p40") && people[2].age == 40, "map int->struct last")

    // ── map: struct → Str ────────────────────────────────────────────────────
    Vec(Str) names = people.map(Str)(|p| p.name)
    check(names.len() == 3, "map struct->string len")
    check(names[1].eq?("p30"), "map struct->string val")

    // ── map on empty vec ────────────────────────────────────────────────────
    Vec(int) empty = {}
    Vec(Str) empty_mapped = empty.map(Str)(|x| f"x{x}")
    check(empty_mapped.len() == 0, "map empty")

    // ── reduce: int sum ─────────────────────────────────────────────────────
    Vec(int) r_nums = [1, 2, 3, 4, 5]
    int sum = r_nums.reduce(int)(0, |acc, x| acc + x)
    check(sum == 15, "reduce int sum")

    // ── reduce: int product ─────────────────────────────────────────────────
    int prod = r_nums.reduce(int)(1, |acc, x| acc * x)
    check(prod == 120, "reduce int product")

    // ── reduce: int max ─────────────────────────────────────────────────────
    Vec(int) unordered = [3, 1, 4, 1, 5, 9, 2, 6]
    int mx = unordered.reduce(int)(unordered[0], |acc, x| {
        if x > acc { return x }
        return acc
    })
    check(mx == 9, "reduce int max")

    // ── reduce: Str join (concat with separator) ─────────────────────────────
    Vec(Str) parts = [f"hello", f"world", f"ls"]
    Str joined = parts.reduce(Str)(f"", |acc, s| {
        if acc.len() == 0 { return f"{s}" }
        return acc + f", " + s
    })
    check(joined.eq?("hello, world, ls"), "reduce string join")

    // ── reduce: Str collect int-to-string ────────────────────────────────────
    Vec(int) digits = [1, 2, 3]
    Str digit_str = digits.reduce(Str)(f"", |acc, d| acc + f"{d}")
    check(digit_str.eq?("123"), "reduce int->string concat")

    // ── reduce: int accumulator from struct vec ──────────────────────────────
    Vec(Person) group = [
        Person { name: f"alice", age: 30 },
        Person { name: f"bob",   age: 25 },
        Person { name: f"cara",  age: 35 }
    ]
    int total_age = group.reduce(int)(0, |acc, p| acc + p.age)
    check(total_age == 90, "reduce struct->int sum")

    // ── reduce: Str from struct vec ──────────────────────────────────────────
    Str name_list = group.reduce(Str)(f"", |acc, p| {
        if acc.len() == 0 { return p.name }
        return acc + f"," + p.name
    })
    check(name_list.eq?("alice,bob,cara"), "reduce struct->string")

    // ── reduce on empty vec (returns init) ───────────────────────────────────
    Vec(int) empty2 = {}
    int empty_sum = empty2.reduce(int)(42, |acc, x| acc + x)
    check(empty_sum == 42, "reduce empty returns init")

    // ── chained map + reduce ────────────────────────────────────────────────
    Vec(int) base = [1, 2, 3, 4, 5]
    int chain_result = base.map(int)(|x| x * x).reduce(int)(0, |acc, x| acc + x)
    check(chain_result == 55, "chained map+reduce (1^2+...+5^2=55)")

    print("RAWVEC MAP REDUCE PASS")
}
