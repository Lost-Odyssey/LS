/* fs_test.ls — Batch-1 filesystem operations smoke test.
   Runs in a temp subdirectory to avoid side effects. */

import std.vec
import std.fs as fs
import std.str
import std.io as io

fn main() {
    string base = "fs_test_tmp"

    /* ---- mkdir / exists / is_dir / is_file ---- */
    match fs.mkdir(base) {
        Ok(v) => { print("mkdir ok") }
        Err(e) => { print("mkdir fail") }
    }

    bool d = fs.is_dir(base)
    print(d)                              /* true */
    bool f = fs.is_file(base)
    print(f)                              /* false */
    bool e = fs.exists(base)
    print(e)                              /* true */

    /* ---- mkdir_all (nested) ---- */
    string nested = "fs_test_tmp/a/b/c"
    match fs.mkdir_all(nested) {
        Ok(v) => { print("mkdir_all ok") }
        Err(e) => { print("mkdir_all fail") }
    }
    bool nd = fs.is_dir(nested)
    print(nd)                             /* true */

    /* ---- cwd / chdir ---- */
    string w = fs.cwd()
    bool wok = w.length > 0
    print(wok)                            /* true */

    match fs.chdir(base) {
        Ok(v) => { print("chdir ok") }
        Err(e) => { print("chdir fail") }
    }
    /* chdir back */
    match fs.chdir("..") {
        Ok(v) => { print("chdir back ok") }
        Err(e) => { print("chdir back fail") }
    }

    /* ---- rename ---- */
    string src_path = "fs_test_tmp/hello.txt"
    string dst_path = "fs_test_tmp/world.txt"
    match io.write_file(src_path, "hi") {
        Ok(v) => { }
        Err(e) => { }
    }
    match fs.rename(src_path, dst_path) {
        Ok(v) => { print("rename ok") }
        Err(e) => { print("rename fail") }
    }
    bool dst_exists = fs.exists(dst_path)
    print(dst_exists)                     /* true */
    bool src_gone   = fs.exists(src_path)
    print(src_gone)                       /* false */

    /* ---- list_dir ---- */
    Vec(Str) entries = fs.list_dir(base)
    bool has_entries = entries.len() > 0
    print(has_entries)                    /* true */

    /* ---- rmdir (clean up leaves first) ---- */
    match io.remove(dst_path) {
        Ok(v) => { }
        Err(e) => { }
    }
    /* rmdir nested/a/b/c → a/b → a → base */
    fs.rmdir("fs_test_tmp/a/b/c")
    fs.rmdir("fs_test_tmp/a/b")
    fs.rmdir("fs_test_tmp/a")
    match fs.rmdir(base) {
        Ok(v) => { print("rmdir ok") }
        Err(e) => { print("rmdir fail") }
    }
    bool gone = fs.exists(base)
    print(gone)                           /* false */

    print("ALL PASS")
}
