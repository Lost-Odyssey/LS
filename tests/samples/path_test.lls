/* path_test.ls — Batch-2 std.sys.path utility smoke test. */

import std.sys.path as path

def main() {
    /* basename */
    @print(path.basename("a/b/c"))       /* c */
    @print(path.basename("foo"))         /* foo */
    @print(path.basename("/root/"))      /* (empty) */

    /* dirname */
    @print(path.dirname("a/b/c"))        /* a/b */
    @print(path.dirname("foo"))          /* . */
    @print(path.dirname("/foo"))         /* / */

    /* ext */
    @print(path.ext("file.txt"))         /* .txt */
    @print(path.ext("archive.tar.gz"))   /* .gz */
    @print(path.ext("Makefile"))         /* (empty) */

    /* stem */
    @print(path.stem("file.txt"))        /* file */
    @print(path.stem("archive.tar.gz"))  /* archive.tar */
    @print(path.stem("Makefile"))        /* Makefile */

    /* join */
    @print(path.join("a/b", "c"))        /* a/b/c */
    @print(path.join("a/b/", "c"))       /* a/b/c */
    @print(path.join("a/b", "/c"))       /* a/b/c */
    @print(path.join("", "foo"))         /* foo */

    /* is_absolute */
    bool abs1 = path.is_absolute("/usr/bin")
    @print(abs1)                         /* true */
    bool abs2 = path.is_absolute("rel/path")
    @print(abs2)                         /* false */
    bool abs3 = path.is_absolute("C:/Windows")
    @print(abs3)                         /* true */

    @print("ALL PASS")
}
