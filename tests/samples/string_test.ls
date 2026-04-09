// string_test.ls — End-to-end test for LsString struct representation
// Tests: literals, .length, concatenation (+), value comparison (== !=),
//        print, f-string with string vars, match on strings

fn main() -> int {
    // 1. String literal and .length (O(1) from struct field)
    string s = "hello"
    print(s.length)             // expect: 5

    // 2. Empty string
    string empty = ""
    print(empty.length)         // expect: 0

    // 3. String concatenation via +
    string a = "hello"
    string b = " world"
    string c = a + b
    print(c)                    // expect: hello world
    print(c.length)             // expect: 11

    // 4. Value comparison == and !=
    string x = "abc"
    string y = "abc"
    string z = "def"
    if (x == y) { print("eq1: pass") }     // expect: eq1: pass
    if (x != z) { print("ne1: pass") }     // expect: ne1: pass
    if (x == z) { print("FAIL") }          // should NOT print

    // 5. Literal comparison
    if ("same" == "same") { print("eq2: pass") }   // expect: eq2: pass
    if ("aaa" != "bbb") { print("ne2: pass") }     // expect: ne2: pass

    // 6. Print string directly
    print("direct print works")     // expect: direct print works

    // 7. Multiple string args in print
    print("one", "two", "three")    // expect: one two three

    // 8. f-string with string variable interpolation
    string name = "LS"
    print(f"Hello, {name}!")        // expect: Hello, LS!

    // 9. f-string assigned to variable
    string greeting = f"lang={name}, len={name.length}"
    print(greeting)                 // expect: lang=LS, len=2

    // 10. Concat result used in comparison
    string ab = "ab"
    string cd = "cd"
    string abcd = ab + cd
    if (abcd == "abcd") { print("concat eq: pass") }   // expect: concat eq: pass

    // 11. Chained concatenation
    string chain = "a" + "b" + "c"
    print(chain)                    // expect: abc
    print(chain.length)             // expect: 3

    // === Batch 1: Query methods ===

    // 12. empty()
    string nonempty = "hello"
    string mt = ""
    if (nonempty.empty() == false) { print("empty1: pass") }   // expect: empty1: pass
    if (mt.empty()) { print("empty2: pass") }                  // expect: empty2: pass

    // 13. at() — returns byte value at index
    string attest = "ABC"
    int ch = attest.at(0)
    print(ch)                       // expect: 65 (ASCII 'A')
    int ch2 = attest.at(2)
    print(ch2)                      // expect: 67 (ASCII 'C')

    // 14. find() — returns index of substring, -1 if not found
    string haystack = "hello world"
    int pos1 = haystack.find("world")
    print(pos1)                     // expect: 6
    int pos2 = haystack.find("xyz")
    print(pos2)                     // expect: -1
    int pos3 = haystack.find("hello")
    print(pos3)                     // expect: 0

    // 15. contains()
    if (haystack.contains("llo")) { print("contains1: pass") }     // expect: contains1: pass
    if (haystack.contains("xyz") == false) { print("contains2: pass") }  // expect: contains2: pass

    // 16. starts_with()
    if (haystack.starts_with("hello")) { print("sw1: pass") }      // expect: sw1: pass
    if (haystack.starts_with("world") == false) { print("sw2: pass") }  // expect: sw2: pass

    // 17. ends_with()
    if (haystack.ends_with("world")) { print("ew1: pass") }        // expect: ew1: pass
    if (haystack.ends_with("hello") == false) { print("ew2: pass") }  // expect: ew2: pass

    // 18. compare()
    string ca = "abc"
    string cb = "def"
    string cc = "abc"
    int cmp1 = ca.compare(cb)
    if (cmp1 < 0) { print("cmp1: pass") }      // expect: cmp1: pass  ("abc" < "def")
    int cmp2 = cb.compare(ca)
    if (cmp2 > 0) { print("cmp2: pass") }      // expect: cmp2: pass  ("def" > "abc")
    int cmp3 = ca.compare(cc)
    if (cmp3 == 0) { print("cmp3: pass") }     // expect: cmp3: pass  ("abc" == "abc")

    // === Batch 2: Methods that allocate new strings ===

    // 19. upper()
    string lo = "hello World 123"
    string up = lo.upper()
    print(up)                       // expect: HELLO WORLD 123

    // 20. lower()
    string hi = "HELLO World 123"
    string lw = hi.lower()
    print(lw)                       // expect: hello world 123

    // 21. substr()
    string hw = "hello world"
    string sub1 = hw.substr(0, 5)
    print(sub1)                     // expect: hello
    string sub2 = hw.substr(6, 5)
    print(sub2)                     // expect: world

    // 22. trim()
    string padded = "  hello  "
    string trimmed = padded.trim()
    print(trimmed)                  // expect: hello
    // trim with tabs and newlines
    string padded2 = "\t\n  hi \r\n"
    string trimmed2 = padded2.trim()
    print(trimmed2)                 // expect: hi

    // 23. replace()
    string orig = "hello world"
    string rep1 = orig.replace("world", "LS")
    print(rep1)                     // expect: hello LS
    // replace multiple occurrences
    string rep2 = "aaa".replace("a", "bb")
    print(rep2)                     // expect: bbbbbb
    // replace with empty string (deletion)
    string rep3 = "hello".replace("l", "")
    print(rep3)                     // expect: heo

    // 24. chained operations
    string chain2 = "  HELLO  ".trim().lower()
    print(chain2)                   // expect: hello

    // 25. copy() — dynamic heap-owned duplicate
    string src = "hello"
    string dup = src.copy()
    print(dup)                      // expect: hello
    print(dup.length)               // expect: 5
    if (src == dup) { print("copy eq: pass") }  // expect: copy eq: pass

    return 0
}
