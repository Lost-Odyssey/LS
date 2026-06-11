// std/fs.ls — Filesystem utilities: directory listing, path operations.
// Pure LS — platform differences handled by std.os (os_win32.c / os_posix.c).

import std.vec
import std.os as _os
import std.str

// ---- Directory listing ----

// Returns names of all entries in path (files and subdirectories),
// excluding "." and "..".  Returns an empty Vec on error.
fn list_dir(Str path) -> Vec(Str) {
    _os.raw_listdir_prepare(path)
    int n = _os.raw_listdir_count()
    Vec(Str) result = {}
    int i = 0
    while i < n {
        Str e = from_cstr(_os.raw_listdir_entry(i))
        result.push(e)
        i = i + 1
    }
    return result
}

// ---- Path predicates ----

fn exists(Str path) -> bool {
    return _os.raw_path_exists(path) != 0
}

fn is_dir(Str path) -> bool {
    return _os.raw_path_is_dir(path) != 0
}

fn is_file(Str path) -> bool {
    return _os.raw_path_is_file(path) != 0
}

// ---- Directory creation / removal ----

// Create a single directory level.  Fails if the directory already exists
// or a parent component is missing.
fn mkdir(Str path) -> Result(int, Str) {
    int rc = _os.raw_mkdir(path)
    if rc != 0 {
        Str msg = from_cstr(_os.raw_last_error())
        return Err(msg)
    }
    return Ok(0)
}

// Create directory and all missing parents (like `mkdir -p`).
// Succeeds if the directory already exists.
fn mkdir_all(Str path) -> Result(int, Str) {
    int rc = _os.raw_mkdir_all(path)
    if rc != 0 {
        Str msg = from_cstr(_os.raw_last_error())
        return Err(msg)
    }
    return Ok(0)
}

// Remove an empty directory.
fn rmdir(Str path) -> Result(int, Str) {
    int rc = _os.raw_rmdir(path)
    if rc != 0 {
        Str msg = from_cstr(_os.raw_last_error())
        return Err(msg)
    }
    return Ok(0)
}

// ---- Rename / move ----

// Rename or move a file or directory.  On most platforms the destination
// is overwritten atomically if it already exists and is the same type.
fn rename(Str from_path, Str to_path) -> Result(int, Str) {
    int rc = _os.raw_rename_path(from_path, to_path)
    if rc != 0 {
        Str msg = from_cstr(_os.raw_last_error())
        return Err(msg)
    }
    return Ok(0)
}

// ---- Working directory ----

// Return the current working directory as a Str.
// Returns an empty Str on failure (very unlikely in practice).
fn cwd() -> Str {
    Str v = from_cstr(_os.raw_getcwd())
    return v
}

// Change the current working directory.
fn chdir(Str path) -> Result(int, Str) {
    int rc = _os.raw_chdir(path)
    if rc != 0 {
        Str msg = from_cstr(_os.raw_last_error())
        return Err(msg)
    }
    return Ok(0)
}
