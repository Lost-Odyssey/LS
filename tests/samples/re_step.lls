import std.text.regex as re

def main() {
    @print("step1")
    bool ok1 = re.matches("hello world", "\\w+")
    @print("step2")
    bool ok2 = re.matches("hello", "\\d+")
    @print("step3")
    if ok1 { @print("matches pos: ok") } else { @print("FAIL matches pos") }
    @print("step4")
    if !ok2 { @print("matches neg: ok") } else { @print("FAIL matches neg") }
    @print("step5")
    bool f1 = re.full_match("2024-01-15", "\\d{4}-\\d{2}-\\d{2}")
    @print("step6")
    if f1 { @print("full_match: ok") } else { @print("FAIL full_match") }
    @print("done")
}
