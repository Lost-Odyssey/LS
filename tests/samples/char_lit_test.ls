// char_lit_test.ls — C-style char literal tests
// Tests: char vs int comparison, match with char patterns, escape chars

fn main() {
    // --- Basic char literal equality ---
    int c = 65
    if c == 'A' { print("PASS 1a") }   // int == char
    if 'A' == c { print("PASS 1b") }   // char == int

    // --- Inequality ---
    if c != 'B' { print("PASS 1c") }
    if 'Z' != 'A' { print("PASS 1d") }  // char != char (same type)

    // --- Escape sequences ---
    int nl = 10
    if nl == '\n' { print("PASS 2a") }
    int cr = 13
    if cr == '\r' { print("PASS 2b") }
    int tab = 9
    if tab == '\t' { print("PASS 2c") }
    int bs = 92
    if bs == '\\' { print("PASS 2d") }
    int sq = 39
    if sq == '\'' { print("PASS 2e") }
    int nul = 0
    if nul == '\0' { print("PASS 2f") }

    // --- match with char patterns ---
    int ch = 65   // 'A'
    match ch {
        'A' => { print("PASS 3a") }
        'B' => { print("FAIL 3a") }
        _   => { print("FAIL 3a wildcard") }
    }

    // --- match with char OR-pattern ---
    int vowel = 101   // 'e'
    match vowel {
        'a' | 'e' | 'i' | 'o' | 'u' => { print("PASS 3b") }
        _ => { print("FAIL 3b") }
    }

    // --- match with escape char pattern ---
    int esc = 10
    match esc {
        '\n' => { print("PASS 3c") }
        '\r' => { print("FAIL 3c") }
        _    => { print("FAIL 3c wildcard") }
    }

    // --- Use char literal in expression ---
    int lo = 'a'
    int hi = 'z'
    int x = 109   // 'm'
    if x >= lo && x <= hi { print("PASS 4a") }

    // --- char literal arithmetic ---
    int digit = '5'
    int val = digit - '0'
    if val == 5 { print("PASS 4b") }

    // --- char literal in variable declaration ---
    int newline = '\n'
    if newline == 10 { print("PASS 4c") }

    // --- match successive chars using OR pattern ---
    int letter = 98   // 'b'
    match letter {
        'a' | 'b' | 'c' => { print("PASS 5a") }
        'd' | 'e' | 'f' => { print("FAIL 5a") }
        _ => { print("FAIL 5a wildcard") }
    }

    // --- json-style escape dispatch (the original use case) ---
    int next = 110   // 'n'
    match next {
        '"'  => { print("PASS 5b: quote") }
        'n'  => { print("PASS 5b: newline") }
        'r'  => { print("PASS 5b: cr") }
        't'  => { print("PASS 5b: tab") }
        '\\' => { print("PASS 5b: backslash") }
        _    => { print("FAIL 5b") }
    }

    // --- 'u' dispatch (the concrete question from the user) ---
    int escape_char = 117   // 'u'
    if escape_char == 'u' { print("PASS 5c") }

    match escape_char {
        'b' | 'f' => { print("FAIL 5c or") }
        'u'       => { print("PASS 5c match") }
        _         => { print("FAIL 5c wildcard") }
    }
}
