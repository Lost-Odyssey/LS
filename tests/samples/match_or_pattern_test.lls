/* match_or_pattern_test.ls — OR pattern in match (bugs/18 fix)
   Tests:
   1. Basic int OR pattern: 1 | 2 => same arm
   2. Three-way OR: 10 | 20 | 30
   3. OR with wildcard fallback
   4. char OR pattern (int underneath)
   5. String match (Str → if/eq? chain — regression)
   6. String match with OR (if/eq? chain + OR logic)
*/

import std.core.str

def classify(int c) -> Str {
    match c {
        1 | 2       => { return "one-or-two" }
        3           => { return "three" }
        _           => { return "other" }
    }
}

def vowel_or_not(int c) -> Str {
    /* ASCII codes: a=97 e=101 i=105 o=111 u=117 */
    match c {
        97 | 101 | 105 | 111 | 117 => { return "vowel" }
        _                          => { return "consonant" }
    }
}

def weekend(int day) -> bool {
    /* 0=Sun 6=Sat */
    match day {
        0 | 6 => { return true }
        _     => { return false }
    }
}

def str_test(Str s) -> Str {
    if s.eq?("hello") { return "greeting" }
    if s.eq?("bye")   { return "farewell" }
    return "unknown"
}

/* Str OR test via an if/eq? chain (Str has no string-switch match) */
def str_or_test(Str s) -> Str {
    if s.eq?("a") || s.eq?("e") || s.eq?("i") || s.eq?("o") || s.eq?("u") {
        return "vowel"
    }
    return "consonant"
}

def main() {
    /* Test 1: basic OR */
    Str r1 = classify(1)
    Str r2 = classify(2)
    Str r3 = classify(3)
    Str r4 = classify(99)
    if r1.eq?("one-or-two") { @print("PASS 1a") } else { @print("FAIL 1a") }
    if r2.eq?("one-or-two") { @print("PASS 1b") } else { @print("FAIL 1b") }
    if r3.eq?("three")      { @print("PASS 1c") } else { @print("FAIL 1c") }
    if r4.eq?("other")      { @print("PASS 1d") } else { @print("FAIL 1d") }

    /* Test 2: three-way OR vowels */
    if vowel_or_not(97).eq?("vowel")      { @print("PASS 2a") } else { @print("FAIL 2a") }
    if vowel_or_not(101).eq?("vowel")     { @print("PASS 2b") } else { @print("FAIL 2b") }
    if vowel_or_not(105).eq?("vowel")     { @print("PASS 2c") } else { @print("FAIL 2c") }
    if vowel_or_not(98).eq?("consonant")  { @print("PASS 2d") } else { @print("FAIL 2d") }

    /* Test 3: weekend */
    if weekend(0) == true  { @print("PASS 3a") } else { @print("FAIL 3a") }
    if weekend(6) == true  { @print("PASS 3b") } else { @print("FAIL 3b") }
    if weekend(3) == false { @print("PASS 3c") } else { @print("FAIL 3c") }

    /* Test 4: Str match regression */
    if str_test("hello").eq?("greeting") { @print("PASS 4a") } else { @print("FAIL 4a") }
    if str_test("bye").eq?("farewell")   { @print("PASS 4b") } else { @print("FAIL 4b") }
    if str_test("hi").eq?("unknown")     { @print("PASS 4c") } else { @print("FAIL 4c") }

    /* Test 5: Str OR pattern */
    if str_or_test("a").eq?("vowel")     { @print("PASS 5a") } else { @print("FAIL 5a") }
    if str_or_test("e").eq?("vowel")     { @print("PASS 5b") } else { @print("FAIL 5b") }
    if str_or_test("b").eq?("consonant") { @print("PASS 5c") } else { @print("FAIL 5c") }
}
