// rawvec_map_reduce_test.ls -- method-level generic map/reduce on Vec(T).
// Tests: int->int, int->string, string->string, string->int, struct->string,
//        reduce with POD accumulator, string accumulator, and struct accumulator.
// All three paths: JIT + AOT + memcheck must pass with 0 leaks.

import std.vec

struct Person { string name; int age }

fn check(bool c, string l) { if c { print(f"ok {l}") } else { print(f"FAIL {l}") } }

fn main() {

    // ── map: int → int ──────────────────────────────────────────────────────
    Vec(int) nums = [1, 2, 3, 4, 5]
    Vec(int) doubled = nums.map(int)(|x| x * 2)
    check(doubled.len() == 5, "map int->int len")
    check(doubled[0] == 2 && doubled[2] == 6 && doubled[4] == 10, "map int->int vals")
    check(nums.len() == 5, "map src untouched")

    // ── map: int → string ───────────────────────────────────────────────────
    Vec(string) strs = nums.map(string)(|x| f"n{x}")
    check(strs.len() == 5, "map int->string len")
    check(strs[0] == "n1" && strs[2] == "n3" && strs[4] == "n5", "map int->string vals")

    // ── map: string → string ────────────────────────────────────────────────
    Vec(string) words = [f"apple", f"banana", f"cherry"]
    Vec(string) upper_words = words.map(string)(|s| s.upper())
    check(upper_words.len() == 3, "map str->str len")
    check(upper_words[0] == "APPLE" && upper_words[2] == "CHERRY", "map str->str vals")
    check(words[0] == "apple", "map str src untouched")

    // ── map: string → int (length) ──────────────────────────────────────────
    Vec(int) lens = words.map(int)(|s| s.length)
    check(lens.len() == 3, "map str->int len")
    check(lens[0] == 5 && lens[1] == 6 && lens[2] == 6, "map str->int vals")

    // ── map: int → struct ───────────────────────────────────────────────────
    Vec(int) ages = [20, 30, 40]
    Vec(Person) people = ages.map(Person)(|a| Person { name: f"p{a}", age: a })
    check(people.len() == 3, "map int->struct len")
    check(people[0].name == "p20" && people[0].age == 20, "map int->struct first")
    check(people[2].name == "p40" && people[2].age == 40, "map int->struct last")

    // ── map: struct → string ────────────────────────────────────────────────
    Vec(string) names = people.map(string)(|p| p.name)
    check(names.len() == 3, "map struct->string len")
    check(names[1] == "p30", "map struct->string val")

    // ── map on empty vec ────────────────────────────────────────────────────
    Vec(int) empty = {}
    Vec(string) empty_mapped = empty.map(string)(|x| f"x{x}")
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

    // ── reduce: string join (concat with separator) ──────────────────────────
    Vec(string) parts = [f"hello", f"world", f"ls"]
    string joined = parts.reduce(string)(f"", |acc, s| {
        if acc.length == 0 { return f"" + s }
        return acc + f", " + s
    })
    check(joined == "hello, world, ls", "reduce string join")

    // ── reduce: string collect int-to-string ─────────────────────────────────
    Vec(int) digits = [1, 2, 3]
    string digit_str = digits.reduce(string)(f"", |acc, d| acc + f"{d}")
    check(digit_str == "123", "reduce int->string concat")

    // ── reduce: int accumulator from struct vec ──────────────────────────────
    Vec(Person) group = [
        Person { name: f"alice", age: 30 },
        Person { name: f"bob",   age: 25 },
        Person { name: f"cara",  age: 35 }
    ]
    int total_age = group.reduce(int)(0, |acc, p| acc + p.age)
    check(total_age == 90, "reduce struct->int sum")

    // ── reduce: string from struct vec ──────────────────────────────────────
    string name_list = group.reduce(string)(f"", |acc, p| {
        if acc.length == 0 { return p.name }
        return acc + f"," + p.name
    })
    check(name_list == "alice,bob,cara", "reduce struct->string")

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
