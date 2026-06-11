// map_test.ls - end-to-end tests for std.map Map(K,V)

import std.map
import std.str

fn get_ii(&Map(int, int) m, int key) -> int {
    match m.get(key) {
        Some(v) => { return v }
        None => { return 0 }
    }
}

fn get_si(&Map(Str, int) m, Str key) -> int {
    match m.get(key) {
        Some(v) => { return v }
        None => { return 0 }
    }
}

fn get_ss(&Map(Str, Str) m, Str key) -> Str {
    match m.get(key) {
        Some(v) => { return v }
        None => { return "" }
    }
}

fn test_int_int() {
    Map(int, int) m = {}
    m.set(1, 100)
    m.set(2, 200)
    m.set(3, 300)

    print(get_ii(m, 1))        // 100
    print(get_ii(m, 2))        // 200
    print(get_ii(m, 3))        // 300
    print(get_ii(m, 99))       // 0 (not found)

    print(m.has?(2))   // true
    print(m.has?(99))  // false
    print(m.len())     // 3

    m.set(2, 999)
    print(get_ii(m, 2))        // 999 (updated)

    m.remove(1)
    print(m.has?(1))  // false
    print(m.len())    // 2
}

fn test_string_int() {
    Map(Str, int) freq = {}
    freq.set("hello", 1)
    freq.set("world", 2)
    freq.set("hello", 3)  // update

    print(get_si(freq, "hello"))   // 3
    print(get_si(freq, "world"))   // 2
    print(freq.has?("hello"))      // true
    print(freq.has?("foo"))        // false
    print(freq.len())              // 2
}

fn test_string_string() {
    Map(Str, Str) dict = {}
    dict.set("key1", "value1")
    dict.set("key2", "value2")
    dict.set("key3", "value3")

    Str v1 = get_ss(dict, "key1")
    Str v2 = get_ss(dict, "key2")
    print(v1)   // value1
    print(v2)   // value2
    print(dict.len())   // 3

    dict.remove("key2")
    print(dict.has?("key2"))  // false
    print(dict.len())         // 2
}

fn test_clear() {
    Map(int, int) m = {}
    m.set(10, 1)
    m.set(20, 2)
    m.set(30, 3)
    print(m.len())     // 3
    m.clear()
    print(m.len())     // 0
    print(m.empty?())  // true

    // Can add again after clear
    m.set(10, 99)
    print(get_ii(m, 10))   // 99
}

fn test_is_empty() {
    Map(Str, int) m = {}
    print(m.empty?())  // true
    m.set("x", 1)
    print(m.empty?())  // false
}

fn test_index_syntax() {
    Map(Str, int) m = {}
    m.set("a", 10)
    m.set("b", 20)
    print(get_si(m, "a"))  // 10
    print(get_si(m, "b"))  // 20
}

fn test_rehash() {
    // Insert many items to trigger rehash (load factor > 70%).
    Map(int, int) m = {}
    for i in 0..20 {
        m.set(i, i * i)
    }
    print(m.len())           // 20
    print(get_ii(m, 5))      // 25
    print(get_ii(m, 10))     // 100
    print(get_ii(m, 19))     // 361
}

fn main() {
    test_int_int()
    test_string_int()
    test_string_string()
    test_clear()
    test_is_empty()
    test_index_syntax()
    test_rehash()
    print("map tests passed")
}