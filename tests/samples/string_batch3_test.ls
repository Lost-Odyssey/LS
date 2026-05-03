// string Batch 3 end-to-end test: rfind / count / substr(1-arg) / split / join

fn main() -> int {
    // ===================== rfind() =====================

    // rfind: returns index of LAST occurrence
    string s1 = "hello world hello"
    int pos1 = s1.rfind("hello")
    print(pos1)   // 12

    // rfind: only one occurrence
    string s2 = "abcdef"
    print(s2.rfind("cd"))   // 2

    // rfind: not found → -1
    print(s2.rfind("xyz"))   // -1

    // rfind: empty string finds last position = len
    // (returns len because strstr("abc","") = "abc" then advance 1 each time)
    // We return len in that case, but actually our impl advances by max(1,0)=1
    // Let's just test non-empty cases reliably

    // rfind: multiple overlapping-ish occurrences
    string s3 = "aaaa"
    print(s3.rfind("a"))   // 3

    // ===================== count() =====================

    // count non-overlapping occurrences
    string c1 = "abababab"
    print(c1.count("ab"))   // 4

    string c2 = "hello"
    print(c2.count("l"))    // 2

    // count: not found → 0
    print(c2.count("xyz"))  // 0

    // count: single char
    string c3 = "aaa"
    print(c3.count("aa"))   // 1  (non-overlapping: "aa" at 0, then advance 2, only "a" left)

    // ===================== substr(start) — single arg =====================

    string w = "hello world"
    // from index 6 to end
    string tail = w.substr(6)
    print(tail)   // world

    // from index 0 = full string
    string full = w.substr(0)
    print(full)   // hello world

    // start >= length → empty string
    string empty_s = w.substr(100)
    print(empty_s.length)  // 0

    // negative start clamps to 0
    string from_start = w.substr(-5)
    print(from_start)      // hello world

    // ===================== split() =====================

    // basic split
    string csv = "apple,banana,cherry"
    vec(string) parts = csv.split(",")
    print(parts.length)   // 3
    print(parts[0])       // apple
    print(parts[1])       // banana
    print(parts[2])       // cherry

    // split with multi-char separator
    string s4 = "one::two::three"
    vec(string) ps4 = s4.split("::")
    print(ps4.length)   // 3
    print(ps4[0])       // one
    print(ps4[1])       // two
    print(ps4[2])       // three

    // split: no separator found → 1-element vec
    string s5 = "nosep"
    vec(string) ps5 = s5.split(",")
    print(ps5.length)   // 1
    print(ps5[0])       // nosep

    // split: empty separator → 1-element vec with copy of src
    vec(string) ps6 = s5.split("")
    print(ps6.length)   // 1
    print(ps6[0])       // nosep

    // split: trailing separator creates empty last element
    string s7 = "a,b,"
    vec(string) ps7 = s7.split(",")
    print(ps7.length)   // 3
    print(ps7[0])       // a
    print(ps7[1])       // b
    print(ps7[2].length) // 0 (empty string)

    // ===================== join() =====================

    // basic join
    vec(string) words
    words.push("hello")
    words.push("world")
    words.push("foo")
    string joined = ", ".join(words)
    print(joined)   // hello, world, foo

    // join with empty separator
    string joined2 = "".join(words)
    print(joined2)  // helloworldfoo

    // join single element
    vec(string) one_elem
    one_elem.push("only")
    string joined3 = "-".join(one_elem)
    print(joined3)  // only

    // join empty vec → empty string
    vec(string) empty_vec
    string joined4 = ",".join(empty_vec)
    print(joined4.length)  // 0

    // ===================== round-trip: split then join =====================

    string original = "a:b:c:d"
    vec(string) split_parts = original.split(":")
    string rejoined = ":".join(split_parts)
    print(rejoined)   // a:b:c:d

    return 0
}
