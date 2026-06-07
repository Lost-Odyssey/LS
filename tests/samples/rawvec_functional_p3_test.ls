// rawvec_functional_p3_test.ls -- Vec functional parity over Block(T)
// with POD, string, and has_drop struct elements.

import std.vec

struct Person { string name; int age }

fn check(bool c, string l) {
    if c { print(f"ok {l}") } else { print(f"FAIL {l}") }
}

fn main() {
    Vec(int) nums = [3, 1, 4, 1, 5, 9]
    check(nums.any(|x| x > 8), "int any")
    check(!nums.any(|x| x > 99), "int any false")
    check(nums.all(|x| x > 0), "int all")
    check(nums.count(|x| x == 1) == 2, "int count")
    check(nums.pos(|x| x == 5) == 4, "int find_index")
    match nums.find(|x| x > 4) {
        Some(x) => { check(x == 5, "int find") }
        None => { check(false, "int find none") }
    }
    Vec(int) big = nums.filter(|x| x > 3)
    check(big.len() == 3 && big[0] == 4 && big[2] == 9, "int filter")
    nums.sort_by(|a, b| a - b)
    check(nums[0] == 1 && nums[1] == 1 && nums[5] == 9, "int sort_by")

    Vec(string) words = [f"pear", f"kiwi", f"banana", f"plum"]
    check(words.any(|s| s.length > 5), "string any")
    check(words.all(|s| s.length >= 4), "string all")
    check(words.count(|s| s.length == 4) == 3, "string count")
    check(words.pos(|s| s == "banana") == 2, "string find_index")
    match words.find(|s| s.length == 4) {
        Some(w) => { check(w == "pear", "string find") }
        None => { check(false, "string find none") }
    }
    Vec(string) short = words.filter(|s| s.length == 4)
    check(short.len() == 3 && short[1] == "kiwi", "string filter")
    words.each(|s| { string t = s + "!"; if t.length < 2 { print("FAIL each") } })
    words.sort_by(|a, b| a.length - b.length)
    check(words[0] == "pear" && words[3] == "banana", "string sort_by")

    Vec(Person) people = [
        Person { name: f"amy", age: 31 },
        Person { name: f"bo", age: 22 },
        Person { name: f"carla", age: 40 }
    ]
    check(people.any(|p| p.age > 35), "struct any")
    check(people.count(|p| p.name.length > 2) == 2, "struct count")
    Vec(Person) older = people.filter(|p| p.age >= 30)
    check(older.len() == 2 && older[0].name == "amy", "struct filter")
    match people.find(|p| p.name == "bo") {
        Some(p) => { check(p.age == 22, "struct find") }
        None => { check(false, "struct find none") }
    }
    people.sort_by(|a, b| a.age - b.age)
    check(people[0].name == "bo" && people[2].name == "carla", "struct sort_by")

    print("RAWVEC FUNCTIONAL P3 PASS")
}
