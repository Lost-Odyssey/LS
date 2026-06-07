// std/fs.ls — Filesystem utilities: directory listing, path operations.
// Pure LS — platform differences handled by std.os (os_win32.c / os_posix.c).

import std.vec
import std.os as _os

// ---- Directory listing ----

// Returns names of all entries in path (files and subdirectories),
// excluding "." and "..".  Returns an empty Vec on error.
fn list_dir(string path) -> Vec(string) {
    _os.raw_listdir_prepare(path)
    int n = _os.raw_listdir_count()
    Vec(string) result = {}
    int i = 0
    while i < n {
        result.push(from_cstr(_os.raw_listdir_entry(i)))
        i = i + 1
    }
    return result
}

// ---- Path predicates ----

fn exists(string path) -> bool {
    return _os.raw_path_exists(path) != 0
}

fn is_dir(string path) -> bool {
    return _os.raw_path_is_dir(path) != 0
}

fn is_file(string path) -> bool {
    return _os.raw_path_is_file(path) != 0
}

// ---- Directory creation / removal ----

// Create a single directory level.  Fails if the directory already exists
// or a parent component is missing.
fn mkdir(string path) -> Result(int, string) {
    int rc = _os.raw_mkdir(path)
    if rc != 0 {
        return Err(from_cstr(_os.raw_last_error()))
    }
    return Ok(0)
}

// Create directory and all missing parents (like `mkdir -p`).
// Succeeds if the directory already exists.
fn mkdir_all(string path) -> Result(int, string) {
    int rc = _os.raw_mkdir_all(path)
    if rc != 0 {
        return Err(from_cstr(_os.raw_last_error()))
    }
    return Ok(0)
}

// Remove an empty directory.
fn rmdir(string path) -> Result(int, string) {
    int rc = _os.raw_rmdir(path)
    if rc != 0 {
        return Err(from_cstr(_os.raw_last_error()))
    }
    return Ok(0)
}

// ---- Rename / move ----

// Rename or move a file or directory.  On most platforms the destination
// is overwritten atomically if it already exists and is the same type.
fn rename(string from_path, string to_path) -> Result(int, string) {
    int rc = _os.raw_rename_path(from_path, to_path)
    if rc != 0 {
        return Err(from_cstr(_os.raw_last_error()))
    }
    return Ok(0)
}

// ---- Working directory ----

// Return the current working directory as a string.
// Returns an empty string on failure (very unlikely in practice).
fn cwd() -> string {
    return from_cstr(_os.raw_getcwd())
}

// Change the current working directory.
fn chdir(string path) -> Result(int, string) {
    int rc = _os.raw_chdir(path)
    if rc != 0 {
        return Err(from_cstr(_os.raw_last_error()))
    }
    return Ok(0)
}
