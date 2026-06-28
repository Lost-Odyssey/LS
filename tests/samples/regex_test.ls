// tests/samples/regex_test.ls — Integration tests for std.text.regex
import std.core.vec
import std.text.regex as re
import std.core.str

def test_basic() {
    // matches
    if !re.matches("hello world", "\\w+") { @print("FAIL: matches pos"); return }
    if re.matches("hello", "\\d+") { @print("FAIL: matches neg"); return }
    // full_match
    if !re.full_match("2024-01-15", "\\d{4}-\\d{2}-\\d{2}") { @print("FAIL: full_match pos"); return }
    if re.full_match("2024-01-15 extra", "\\d{4}-\\d{2}-\\d{2}") { @print("FAIL: full_match neg"); return }
    @print("PASS: basic")
}

def test_quantifiers() {
    // greedy *
    Vec(Str) v = re.find_all("aabbb", "a*")
    if v.len() == 0 { @print("FAIL: greedy star"); return }
    // + requires at least one
    if re.matches("b", "a+") { @print("FAIL: plus neg"); return }
    if !re.matches("aa", "a+") { @print("FAIL: plus pos"); return }
    // {n,m}
    if !re.matches("aaa", "a{3}") { @print("FAIL: {n}"); return }
    if re.matches("aa", "a{3}") { @print("FAIL: {n} neg"); return }
    if !re.matches("aaaa", "a{2,4}") { @print("FAIL: {n,m}"); return }
    // lazy — capture returns Vec(Str), empty = no match
    Vec(Str) c = re.capture("aXbXc", "(a.*?c)")
    if c.len() == 0 { @print("FAIL: lazy none"); return }
    if !c[0].eq?("aXbXc") { @print(f"FAIL: lazy wrong: {c[0]}"); return }
    @print("PASS: quantifiers")
}

def test_charclass() {
    if !re.matches("abc123", "[a-z]+") { @print("FAIL: range"); return }
    if re.matches("ABC", "[a-z]+") { @print("FAIL: range neg"); return }
    if !re.matches("123", "[^a-z]+") { @print("FAIL: negate"); return }
    if !re.matches("42", "\\d+") { @print("FAIL: digit"); return }
    if !re.matches("hello_world", "\\w+") { @print("FAIL: word"); return }
    if !re.matches("  \t", "\\s+") { @print("FAIL: space"); return }
    if !re.matches("hello world", "\\bhello\\b") { @print("FAIL: wordbnd"); return }
    if re.matches("helloworld", "\\bhello\\b") { @print("FAIL: wordbnd neg"); return }
    @print("PASS: charclass")
}

def test_alternation() {
    if !re.matches("cat", "cat|dog") { @print("FAIL: alt cat"); return }
    if !re.matches("dog", "cat|dog") { @print("FAIL: alt dog"); return }
    if re.matches("bird", "cat|dog") { @print("FAIL: alt neg"); return }
    if !re.matches("red sky", "(red|blue) sky") { @print("FAIL: group alt"); return }
    if !re.matches("blue sky", "(red|blue) sky") { @print("FAIL: group alt 2"); return }
    @print("PASS: alternation")
}

def test_find() {
    Option(Str) m = re.find("price: 42.5 USD", "\\d+\\.\\d+")
    match m {
        Some(s) => { if !s.eq?("42.5") { @print(f"FAIL: find got {s}"); return } }
        None    => { @print("FAIL: find none"); return }
    }
    Vec(Str) all = re.find_all("a1 b2 c3", "\\d+")
    if all.len() != 3 { @print("FAIL: find_all count"); return }
    if !all[0].eq?("1") { @print("FAIL: find_all[0]"); return }
    if !all[1].eq?("2") { @print("FAIL: find_all[1]"); return }
    if !all[2].eq?("3") { @print("FAIL: find_all[2]"); return }
    @print("PASS: find")
}

def test_capture() {
    // capture returns Vec(Str): [full, g1, g2, ...]  empty = no match
    Vec(Str) caps = re.capture("2024-01-15", "(\\d{4})-(\\d{2})-(\\d{2})")
    if caps.len() == 0 { @print("FAIL: capture none"); return }
    if caps.len() != 4 { @print(f"FAIL: capture length {caps.len()}"); return }
    if !caps[0].eq?("2024-01-15") { @print("FAIL: cap[0]"); return }
    if !caps[1].eq?("2024") { @print("FAIL: cap[1]"); return }
    if !caps[2].eq?("01") { @print("FAIL: cap[2]"); return }
    if !caps[3].eq?("15") { @print("FAIL: cap[3]"); return }

    // capture_all returns flat vec; stride = group_count + 1
    // pattern "(\\w+)=(\\d+)" has 2 groups → stride = 3
    // "a=1 b=2 c=3" → 3 matches → 9 elements
    Str cap_all_pat = "(\\w+)=(\\d+)"
    int stride = re.group_count(cap_all_pat) + 1
    if stride != 3 { @print(f"FAIL: group_count {stride}"); return }
    Vec(Str) flat = re.capture_all("a=1 b=2 c=3", cap_all_pat)
    if flat.len() != 9 { @print(f"FAIL: capture_all count {flat.len()}"); return }
    // match 0: flat[0]="a=1"  flat[1]="a"  flat[2]="1"
    // match 1: flat[3]="b=2"  flat[4]="b"  flat[5]="2"
    // match 2: flat[6]="c=3"  flat[7]="c"  flat[8]="3"
    if !flat[1].eq?("a") { @print("FAIL: capture_all[0][1]"); return }
    if !flat[5].eq?("2") { @print("FAIL: capture_all[1][2]"); return }
    if !flat[7].eq?("c") { @print("FAIL: capture_all[2][1]"); return }
    @print("PASS: capture")
}

def test_named_capture() {
    // capture_named returns Map(Str,Str); empty map = no match
    Map(Str, Str) mp = re.capture_named(
        "2024-01-15",
        "(?<year>\\d{4})-(?<month>\\d{2})-(?<day>\\d{2})"
    )
    if mp.len() == 0 { @print("FAIL: capture_named none"); return }
    if !mp.has?("year")  { @print("FAIL: named year missing"); return }
    if !mp.has?("month") { @print("FAIL: named month missing"); return }
    if !mp.has?("day")   { @print("FAIL: named day missing"); return }
    Str y = ""
    Str mo = ""
    Str d = ""
    match mp.get("year") { Some(vy) => { Str sy = vy
                                         y = sy } None => {} }
    match mp.get("month") { Some(vm) => { Str sm = vm
                                          mo = sm } None => {} }
    match mp.get("day") { Some(vd) => { Str sd = vd
                                        d = sd } None => {} }
    if !y.eq?("2024") { @print(f"FAIL: named year={y}"); return }
    if !mo.eq?("01")  { @print(f"FAIL: named month={mo}"); return }
    if !d.eq?("15")   { @print(f"FAIL: named day={d}"); return }
    @print("PASS: named_capture")
}

def test_lookahead() {
    // positive lookahead: foo only when followed by bar
    Vec(Str) v = re.find_all("foobar foobaz", "foo(?=bar)")
    if v.len() != 1 { @print(f"FAIL: pos lookahead count {v.len()}"); return }
    if !v[0].eq?("foo")  { @print("FAIL: pos lookahead val"); return }

    // negative lookahead: foo when NOT followed by bar
    Vec(Str) v2 = re.find_all("foobar foobaz", "foo(?!bar)")
    if v2.len() != 1 { @print(f"FAIL: neg lookahead count {v2.len()}"); return }
    if !v2[0].eq?("foo")  { @print("FAIL: neg lookahead val"); return }
    @print("PASS: lookahead")
}

def test_flags() {
    // case insensitive via inline flag
    if !re.matches("Hello World", "(?i)hello") { @print("FAIL: (?i)"); return }
    if !re.matches("HELLO", "(?i)hello") { @print("FAIL: (?i) upper"); return }
    // multiline ^ $
    if !re.matches("line1\nline2", "(?m)^line2") { @print("FAIL: (?m)"); return }
    // dotall . matches newline
    if !re.matches("foo\nbar", "(?s)foo.bar") { @print("FAIL: (?s)"); return }
    if re.matches("foo\nbar", "foo.bar") { @print("FAIL: dotall default"); return }
    @print("PASS: flags")
}

def test_replace() {
    Str r1 = re.replace("hello world", "\\bworld\\b", "LS")
    if !r1.eq?("hello LS") { @print(f"FAIL: replace: {r1}"); return }

    Str r2 = re.replace_all("2024/01/15", "/", "-")
    if !r2.eq?("2024-01-15") { @print(f"FAIL: replace_all: {r2}"); return }

    // replace_all with digits
    Str r3 = re.replace_all("a1 b2 c3", "\\d+", "X")
    if !r3.eq?("aX bX cX") { @print(f"FAIL: replace_all digits: {r3}"); return }
    @print("PASS: replace")
}

def test_split() {
    Vec(Str) parts = re.split("one,,two,,,three", ",+")
    if parts.len() != 3 { @print(f"FAIL: split count {parts.len()}"); return }
    if !parts[0].eq?("one")   { @print("FAIL: split[0]"); return }
    if !parts[1].eq?("two")   { @print("FAIL: split[1]"); return }
    if !parts[2].eq?("three") { @print("FAIL: split[2]"); return }

    Vec(Str) ws = re.split("  hello   world  ", "\\s+")
    // leading/trailing empty strings dropped
    bool found_hello = false
    bool found_world = false
    int i = 0
    while i < ws.len() {
        if ws[i].eq?("hello") { found_hello = true }
        if ws[i].eq?("world") { found_world = true }
        i = i + 1
    }
    if !found_hello { @print("FAIL: split ws hello"); return }
    if !found_world { @print("FAIL: split ws world"); return }
    @print("PASS: split")
}

def test_error_handling() {
    // Invalid pattern — unclosed group — should return safe defaults
    bool m = re.matches("hello", "(unclosed")
    if m { @print("FAIL: error handling matched"); return }
    Option(Str) f = re.find("hello", "[invalid")
    match f {
        Some(_) => { @print("FAIL: error find matched"); return }
        None    => { }
    }
    @print("PASS: error_handling")
}

def main() {
    test_basic()
    test_quantifiers()
    test_charclass()
    test_alternation()
    test_find()
    test_capture()
    test_named_capture()
    test_lookahead()
    test_flags()
    test_replace()
    test_split()
    test_error_handling()
    @print("ALL PASS")
}
