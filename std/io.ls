// std/io.ls — File I/O module.
// Pure LS — no platform conditionals, no direct extern fn (all via std.c / std.os).
//
// FFI rule (string->Str migration): paths handed to the DIRECT extern fn
// c.fopen go through a builtin-string local first — the Str->string var-decl
// bridge copies + NUL-terminates (Str.data itself has NO NUL and the call-arg
// bridge does not cover direct extern calls). Write paths use Str.as_ptr+len
// (fwrite is length-based, no NUL needed); read paths wrap the malloc'd buffer
// into an owned Str zero-copy.


import std.os as _os
import std.c as c
import std.str

// ---- Public types ----

enum OpenMode {
    Read
    Write
    Append
    ReadBinary
    WriteBinary
    AppendBinary
}

enum SeekFrom {
    Start    // SEEK_SET
    Current  // SEEK_CUR
    End      // SEEK_END
}

struct File {
    object handle
    bool is_binary
}

// ---- Internal helpers ----

// Returns the fopen mode; c.fopen takes *u8, so callers pass `_mode_str(m).c_str()`.
fn _mode_str(OpenMode m) -> Str {
    Str r = "r"
    match m {
        Read         => { r = "r" }
        Write        => { r = "w" }
        Append       => { r = "a" }
        ReadBinary   => { r = "rb" }
        WriteBinary  => { r = "wb" }
        AppendBinary => { r = "ab" }
    }
    return r
}

fn _is_binary(OpenMode m) -> bool {
    bool b = false
    match m {
        ReadBinary   => { b = true }
        WriteBinary  => { b = true }
        AppendBinary => { b = true }
        Read         => { b = false }
        Write        => { b = false }
        Append       => { b = false }
    }
    return b
}

fn _seek_origin(SeekFrom o) -> int {
    int r = 0
    match o {
        Start   => { r = 0 }
        Current => { r = 1 }
        End     => { r = 2 }
    }
    return r
}

fn _fseek64(object fp, i64 off, int origin) -> int { return _os.raw_fseek64(fp, off, origin) }
fn _ftell64(object fp) -> i64 { return _os.raw_ftell64(fp) }

fn _err(Str msg) -> Str {
    Str e = "io: "
    e.push_str(msg)
    return e
}

// Wrap a tracked (c.malloc'd) buffer into an owned Str, zero-copy.
// cap must be the malloc'd size (> len). The Str drop frees the buffer.
fn _own_buf(*u8 buf, i64 len, i64 cap) -> Str {
    return Str { data: buf, len: len as int, cap: cap as int }
}

// ---- Public API ----

fn read_file(Str path) -> Result(Str, Str) {
    Str mode = "rb"
    object fp = c.fopen(path.c_str(), mode.c_str())
    if fp == nil {
        return Err(_err("read_file: open failed"))
    }
    _fseek64(fp, 0, 2)
    i64 sz = _ftell64(fp)
    _fseek64(fp, 0, 0)
    if sz < 0 {
        c.fclose(fp)
        return Err(_err("read_file: tell failed"))
    }
    *u8 buf = c.malloc(sz + 1)
    i64 nread = c.fread(buf, 1, sz, fp)
    c.fclose(fp)
    Str s = _own_buf(buf, nread, sz + 1)
    if nread != sz {
        return Err(_err("read_file: read incomplete"))
    }
    return Ok(s)
}

fn write_file(Str path, Str content) -> Result(int, Str) {
    Str mode = "wb"
    object fp = c.fopen(path.c_str(), mode.c_str())
    if fp == nil {
        return Err(_err("write_file: open failed"))
    }
    i64 len = content.len()
    i64 wrote = c.fwrite(content.as_ptr() as *u8, 1, len, fp)
    c.fclose(fp)
    if wrote != len {
        return Err(_err("write_file: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn append_file(Str path, Str content) -> Result(int, Str) {
    Str mode = "ab"
    object fp = c.fopen(path.c_str(), mode.c_str())
    if fp == nil {
        return Err(_err("append_file: open failed"))
    }
    i64 len = content.len()
    i64 wrote = c.fwrite(content.as_ptr() as *u8, 1, len, fp)
    c.fclose(fp)
    if wrote != len {
        return Err(_err("append_file: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn exists(Str path) -> bool {
    Str mode = "rb"
    object fp = c.fopen(path.c_str(), mode.c_str())
    if fp == nil {
        return false
    }
    c.fclose(fp)
    return true
}

fn open(Str path, OpenMode m) -> Result(File, Str) {
    Str ms = _mode_str(m)
    object fp = c.fopen(path.c_str(), ms.c_str())
    if fp == nil {
        return Err(_err("open failed"))
    }
    bool b = _is_binary(m)
    File f = File { handle: fp, is_binary: b }
    return Ok(f)
}

fn close(File f) -> int {
    return c.fclose(f.handle)
}

fn read_all(File f) -> Result(Str, Str) {
    if f.handle == nil {
        return Err(_err("read_all: file is closed"))
    }
    i64 saved = _ftell64(f.handle)
    _fseek64(f.handle, 0, 2)
    i64 endp = _ftell64(f.handle)
    i64 sz = endp - saved
    _fseek64(f.handle, saved, 0)
    if sz <= 0 {
        Str empty = ""
        return Ok(empty)
    }
    *u8 buf = c.malloc(sz + 1)
    i64 nread = c.fread(buf, 1, sz, f.handle)
    Str s = _own_buf(buf, nread, sz + 1)
    if nread != sz {
        return Err(_err("read_all: read incomplete"))
    }
    return Ok(s)
}

fn write(File f, Str content) -> Result(int, Str) {
    if f.handle == nil {
        return Err(_err("write: file is closed"))
    }
    i64 len = content.len()
    i64 wrote = c.fwrite(content.as_ptr() as *u8, 1, len, f.handle)
    if wrote != len {
        return Err(_err("write: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn seek(File f, i64 offset, SeekFrom origin) -> Result(i64, Str) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    int o = _seek_origin(origin)
    int r = _fseek64(f.handle, offset, o)
    if r != 0 {
        return Err(_err("seek failed"))
    }
    i64 pos = _ftell64(f.handle)
    return Ok(pos)
}

fn tell(File f) -> Result(i64, Str) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    i64 pos = _ftell64(f.handle)
    if pos < 0 {
        return Err(_err("tell failed"))
    }
    return Ok(pos)
}

fn size(File f) -> Result(i64, Str) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    i64 saved = _ftell64(f.handle)
    _fseek64(f.handle, 0, 2)
    i64 sz = _ftell64(f.handle)
    _fseek64(f.handle, saved, 0)
    return Ok(sz)
}

fn rewind(File f) -> Result(int, Str) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    int r = _fseek64(f.handle, 0, 0)
    if r != 0 {
        return Err(_err("rewind failed"))
    }
    int z = 0
    return Ok(z)
}

fn remove(Str path) -> Result(int, Str) {
    int r = _os.raw_unlink(path)
    if r != 0 {
        return Err(_err("remove failed"))
    }
    int z = 0
    return Ok(z)
}

// Reads one line from stdin, stripping the trailing newline.
// Returns Ok(line) on success, Err("io: read_line: EOF") at end of input.
fn read_line() -> Result(Str, Str) {
    c.__ls_readline_exec()
    if c.__ls_readline_ok() == 0 {
        return Err(_err("read_line: EOF"))
    }
    /* Copy out of the runtime-owned buffer (freed by the next _exec). Taking
       ownership of a runtime-malloc'd pointer would make the eventual string
       drop an INVALID FREE under --memcheck (untracked allocation). */
    object ptr = c.__ls_readline_ptr()
    Str s = from_cstr(ptr)
    return Ok(s)
}
