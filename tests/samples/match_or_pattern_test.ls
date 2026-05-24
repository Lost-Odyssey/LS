/* match_or_pattern_test.ls — OR pattern in match (bugs/18 fix)
   Tests:
   1. Basic int OR pattern: 1 | 2 => same arm
   2. Three-way OR: 10 | 20 | 30
   3. OR with wildcard fallback
   4. char OR pattern (int underneath)
   5. String match (CondBr path, no OR — regression)
   6. String match with OR (CondBr + OR logic)
*/

fn classify(int c) -> string {
    match c {
        1 | 2       => { return "one-or-two" }
        3           => { return "three" }
        _           => { return "other" }
    }
}

fn vowel_or_not(int c) -> string {
    /* ASCII codes: a=97 e=101 i=105 o=111 u=117 */
    match c {
        97 | 101 | 105 | 111 | 117 => { return "vowel" }
        _                          => { return "consonant" }
    }
}

fn weekend(int day) -> bool {
    /* 0=Sun 6=Sat */
    match day {
        0 | 6 => { return true }
        _     => { return false }
    }
}

fn str_test(string s) -> string {
    match s {
        "hello" => { return "greeting" }
        "bye"   => { return "farewell" }
        _       => { return "unknown" }
    }
}

/* Test OR pattern in string match (CondBr path) */
fn str_or_test(string s) -> string {
    match s {
        "a" | "e" | "i" | "o" | "u" => { return "vowel" }
        _                            => { return "consonant" }
    }
}

fn main() {
    /* Test 1: basic OR */
    string r1 = classify(1)
    string r2 = classify(2)
    string r3 = classify(3)
    string r4 = classify(99)
    if r1 == "one-or-two" { print("PASS 1a") } else { print("FAIL 1a") }
    if r2 == "one-or-two" { print("PASS 1b") } else { print("FAIL 1b") }
    if r3 == "three"      { print("PASS 1c") } else { print("FAIL 1c") }
    if r4 == "other"      { print("PASS 1d") } else { print("FAIL 1d") }

    /* Test 2: three-way OR vowels */
    if vowel_or_not(97)  == "vowel"     { print("PASS 2a") } else { print("FAIL 2a") }
    if vowel_or_not(101) == "vowel"     { print("PASS 2b") } else { print("FAIL 2b") }
    if vowel_or_not(105) == "vowel"     { print("PASS 2c") } else { print("FAIL 2c") }
    if vowel_or_not(98)  == "consonant" { print("PASS 2d") } else { print("FAIL 2d") }

    /* Test 3: weekend */
    if weekend(0) == true  { print("PASS 3a") } else { print("FAIL 3a") }
    if weekend(6) == true  { print("PASS 3b") } else { print("FAIL 3b") }
    if weekend(3) == false { print("PASS 3c") } else { print("FAIL 3c") }

    /* Test 4: string match regression */
    if str_test("hello") == "greeting" { print("PASS 4a") } else { print("FAIL 4a") }
    if str_test("bye")   == "farewell" { print("PASS 4b") } else { print("FAIL 4b") }
    if str_test("hi")    == "unknown"  { print("PASS 4c") } else { print("FAIL 4c") }

    /* Test 5: string OR pattern */
    if str_or_test("a") == "vowel"     { print("PASS 5a") } else { print("FAIL 5a") }
    if str_or_test("e") == "vowel"     { print("PASS 5b") } else { print("FAIL 5b") }
    if str_or_test("b") == "consonant" { print("PASS 5c") } else { print("FAIL 5c") }
}
