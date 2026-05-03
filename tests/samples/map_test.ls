// map_test.ls — end-to-end tests for map(K,V) built-in

fn test_int_int() {
    map(int, int) m
    m.set(1, 100)
    m.set(2, 200)
    m.set(3, 300)

    print(m.get(1))        // 100
    print(m.get(2))        // 200
    print(m.get(3))        // 300
    print(m.get(99))       // 0 (not found)

    print(m.contains_key(2))   // true
    print(m.contains_key(99))  // false
    print(m.length)            // 3

    m.set(2, 999)
    print(m.get(2))        // 999 (updated)

    m.remove(1)
    print(m.contains_key(1))  // false
    print(m.length)            // 2
}

fn test_string_int() {
    map(string, int) freq
    freq.set("hello", 1)
    freq.set("world", 2)
    freq.set("hello", 3)  // update

    print(freq.get("hello"))   // 3
    print(freq.get("world"))   // 2
    print(freq.contains_key("hello"))  // true
    print(freq.contains_key("foo"))    // false
    print(freq.length)                 // 2
}

fn test_string_string() {
    map(string, string) dict
    dict.set("key1", "value1")
    dict.set("key2", "value2")
    dict.set("key3", "value3")

    string v1 = dict.get("key1")
    string v2 = dict.get("key2")
    print(v1)   // value1
    print(v2)   // value2
    print(dict.length)   // 3

    dict.remove("key2")
    print(dict.contains_key("key2"))  // false
    print(dict.length)   // 2
}

fn test_clear() {
    map(int, int) m
    m.set(10, 1)
    m.set(20, 2)
    m.set(30, 3)
    print(m.length)    // 3
    m.clear()
    print(m.length)    // 0
    print(m.is_empty())  // true

    // Can add again after clear
    m.set(10, 99)
    print(m.get(10))   // 99
}

fn test_is_empty() {
    map(string, int) m
    print(m.is_empty())  // true
    m.set("x", 1)
    print(m.is_empty())  // false
}

fn test_index_syntax() {
    map(string, int) m
    // m["key"] = val  →  set
    // m["key"]        →  get
    m.set("a", 10)
    m.set("b", 20)
    print(m.get("a"))  // 10
    print(m.get("b"))  // 20
}

fn test_rehash() {
    // Insert many items to trigger rehash (load factor > 75%)
    map(int, int) m
    for i in 0..20 {
        m.set(i, i * i)
    }
    print(m.length)     // 20
    print(m.get(5))     // 25
    print(m.get(10))    // 100
    print(m.get(19))    // 361
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
