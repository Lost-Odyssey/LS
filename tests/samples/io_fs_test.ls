// tests/samples/io_fs_test.ls — Integration test for io.read_line + fs.list_dir

import std.vec
import std.io as io
import std.fs as fs

// ---- Part 1: read_line from stdin ----

fn test_readline() {
    // Read 3 lines; test runner pipes them via stdin.
    match io.read_line() {
        Ok(s) => {
            if s != "hello" {
                print("FAIL: read_line line1 expected 'hello' got '" + s + "'")
                return
            }
        }
        Err(e) => { print("FAIL: read_line EOF on line1: " + e); return }
    }

    match io.read_line() {
        Ok(s) => {
            if s != "world" {
                print("FAIL: read_line line2 expected 'world' got '" + s + "'")
                return
            }
        }
        Err(e) => { print("FAIL: read_line EOF on line2: " + e); return }
    }

    match io.read_line() {
        Ok(s) => {
            if s != "42" {
                print("FAIL: read_line line3 expected '42' got '" + s + "'")
                return
            }
        }
        Err(e) => { print("FAIL: read_line EOF on line3: " + e); return }
    }

    // Next read should be EOF
    match io.read_line() {
        Ok(_)  => { print("FAIL: read_line expected EOF but got Ok") ; return }
        Err(_) => { }
    }

    print("PASS: read_line")
}

// ---- Part 2: fs.list_dir ----

fn test_list_dir() {
    // List the std/ directory — we know it contains at least io.ls, fs.ls, os.ls
    // The LS_HOME env var points to the project root; build the path from there.
    Vec(string) entries = fs.list_dir("std")
    if entries.len() == 0 {
        print("FAIL: list_dir returned empty vec for 'std'")
        return
    }

    bool found_io = false
    bool found_fs = false
    int i = 0
    while i < entries.len() {
        string name = entries[i]
        if name == "io.ls" { found_io = true }
        if name == "fs.ls" { found_fs = true }
        i = i + 1
    }

    if !found_io {
        print("FAIL: list_dir did not find 'io.ls' in std/")
        return
    }
    if !found_fs {
        print("FAIL: list_dir did not find 'fs.ls' in std/")
        return
    }
    print("PASS: list_dir")
}

fn main() {
    test_readline()
    test_list_dir()
    print("ALL PASS")
}
