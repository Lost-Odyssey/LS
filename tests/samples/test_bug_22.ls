// test_bug_22.ls
// Tests: global struct with all-empty string fields, vec(struct) field access,
// f-string formatting in multi-branch logic, string length checks.

struct Theme {
    string blue
    string text
    string nc
    string cyan
    string cyan2
    string violet
    string magenta
    string green
    string green2
}

Theme T = Theme {}

struct Entry {
    int id
    string path
    string git_state
    string file_state
}

// Build a display string from a vec of entries.
// Entries whose git_state == "to_be_committed" are silently skipped.
// "committed" entries show a suffix: "*<file_state>" when file_state is
// non-empty, or "x" otherwise.
// Any other git_state is shown in brackets.
fn show(vec(Entry) entries) -> string {
    string out = ""
    for i in 0..entries.length {
        string gs = entries[i].git_state
        if gs != "to_be_committed" {
            string sid = f"{entries[i].id}"
            string fp  = entries[i].path
            string fs  = entries[i].file_state
            if gs == "committed" {
                string suffix = "x"
                if fs.length > 0 { suffix = f"*{fs}" }
                out.append(sid)
                out.append(" ")
                out.append(fp)
                out.append(" ")
                out.append(suffix)
                out.append("\n")
            } else {
                out.append(sid)
                out.append(" ")
                out.append(fp)
                out.append(" [")
                out.append(gs)
                out.append("]\n")
            }
        }
    }
    return out
}

fn main() -> int {
    int fails = 0

    // ---- Test 1: empty vec → empty output ----
    vec(Entry) e0 = []
    string r0 = show(e0)
    if r0 == "" {
        print("PASS 1")
    } else {
        print("FAIL 1")
        fails = fails + 1
    }

    // ---- Test 2: committed, no file_state → suffix "x" ----
    vec(Entry) e1 = []
    Entry a = Entry { id: 1, path: "src/main.c", git_state: "committed", file_state: "" }
    e1.push(a)
    string r1 = show(e1)
    if r1 == "1 src/main.c x\n" {
        print("PASS 2")
    } else {
        print(f"FAIL 2: got '{r1}'")
        fails = fails + 1
    }

    // ---- Test 3: committed with file_state → suffix "*M" ----
    vec(Entry) e2 = []
    Entry b = Entry { id: 2, path: "src/parser.c", git_state: "committed", file_state: "M" }
    e2.push(b)
    string r2 = show(e2)
    if r2 == "2 src/parser.c *M\n" {
        print("PASS 3")
    } else {
        print(f"FAIL 3: got '{r2}'")
        fails = fails + 1
    }

    // ---- Test 4: to_be_committed is entirely skipped ----
    vec(Entry) e3 = []
    Entry c = Entry { id: 3, path: "src/skip.c", git_state: "to_be_committed", file_state: "A" }
    e3.push(c)
    string r3 = show(e3)
    if r3 == "" {
        print("PASS 4")
    } else {
        print(f"FAIL 4: got '{r3}'")
        fails = fails + 1
    }

    // ---- Test 5: non-committed state shows brackets ----
    vec(Entry) e4 = []
    Entry d = Entry { id: 4, path: "src/new.c", git_state: "untracked", file_state: "" }
    e4.push(d)
    string r4 = show(e4)
    if r4 == "4 src/new.c [untracked]\n" {
        print("PASS 5")
    } else {
        print(f"FAIL 5: got '{r4}'")
        fails = fails + 1
    }

    // ---- Test 6: mixed — skips to_be_committed, preserves order ----
    vec(Entry) e5 = []
    Entry m1 = Entry { id: 1, path: "a.c", git_state: "committed",       file_state: "A" }
    Entry m2 = Entry { id: 2, path: "b.c", git_state: "to_be_committed", file_state: "" }
    Entry m3 = Entry { id: 3, path: "c.c", git_state: "modified",        file_state: "" }
    Entry m4 = Entry { id: 4, path: "d.c", git_state: "committed",       file_state: "" }
    e5.push(m1)
    e5.push(m2)
    e5.push(m3)
    e5.push(m4)
    string r5 = show(e5)
    string want5 = "1 a.c *A\n3 c.c [modified]\n4 d.c x\n"
    if r5 == want5 {
        print("PASS 6")
    } else {
        print(f"FAIL 6: got '{r5}'")
        fails = fails + 1
    }

    // ---- Test 7: global Theme — all string fields initialised to "" ----
    bool theme_ok = T.blue.length    == 0 &&
                    T.text.length    == 0 &&
                    T.nc.length      == 0 &&
                    T.cyan.length    == 0 &&
                    T.cyan2.length   == 0 &&
                    T.violet.length  == 0 &&
                    T.magenta.length == 0 &&
                    T.green.length   == 0 &&
                    T.green2.length  == 0
    if theme_ok {
        print("PASS 7")
    } else {
        print("FAIL 7")
        fails = fails + 1
    }

    // ---- Test 8: assign Theme fields and read back ----
    T.blue  = "\x1b[34m".copy()
    T.green = "\x1b[32m".copy()
    T.nc    = "\x1b[0m".copy()
    if T.blue.length > 0 && T.green.length > 0 && T.nc.length > 0 {
        print("PASS 8")
    } else {
        print("FAIL 8")
        fails = fails + 1
    }

    if fails == 0 {
        print("ALL PASS")
    }
    return fails
}
