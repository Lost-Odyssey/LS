// std/io.ls — File I/O module.
// Pure LS — no platform conditionals, no direct extern fn (all via std.c / std.os).


import std.os as _os
import std.c as c

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

fn _mode_str(OpenMode m) -> string {
    string r = "r"
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

fn _err(string msg) -> string {
    return "io: " + msg
}

// ---- Public API ----

fn read_file(string path) -> Result(string, string) {
    object fp = c.fopen(path, "rb")
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
    *u8 buf = malloc(sz + 1)
    i64 nread = c.fread(buf, 1, sz, fp)
    c.fclose(fp)
    string s = __string_take_buffer(buf, nread)
    if nread != sz {
        return Err(_err("read_file: read incomplete"))
    }
    return Ok(s)
}

fn write_file(string path, string content) -> Result(int, string) {
    object fp = c.fopen(path, "wb")
    if fp == nil {
        return Err(_err("write_file: open failed"))
    }
    i64 len = c.strlen(content)
    *u8 cstr = content.to_cstr() as *u8
    i64 wrote = c.fwrite(cstr, 1, len, fp)
    c.fclose(fp)
    if wrote != len {
        return Err(_err("write_file: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn append_file(string path, string content) -> Result(int, string) {
    object fp = c.fopen(path, "ab")
    if fp == nil {
        return Err(_err("append_file: open failed"))
    }
    i64 len = c.strlen(content)
    *u8 cstr = content.to_cstr() as *u8
    i64 wrote = c.fwrite(cstr, 1, len, fp)
    c.fclose(fp)
    if wrote != len {
        return Err(_err("append_file: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn exists(string path) -> bool {
    object fp = c.fopen(path, "rb")
    if fp == nil {
        return false
    }
    c.fclose(fp)
    return true
}

fn open(string path, OpenMode m) -> Result(File, string) {
    string ms = _mode_str(m)
    object fp = c.fopen(path, ms)
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

fn read_all(File f) -> Result(string, string) {
    if f.handle == nil {
        return Err(_err("read_all: file is closed"))
    }
    i64 saved = _ftell64(f.handle)
    _fseek64(f.handle, 0, 2)
    i64 endp = _ftell64(f.handle)
    i64 sz = endp - saved
    _fseek64(f.handle, saved, 0)
    if sz <= 0 {
        string empty = ""
        return Ok(empty)
    }
    *u8 buf = malloc(sz + 1)
    i64 nread = c.fread(buf, 1, sz, f.handle)
    string s = __string_take_buffer(buf, nread)
    if nread != sz {
        return Err(_err("read_all: read incomplete"))
    }
    return Ok(s)
}

fn write(File f, string content) -> Result(int, string) {
    if f.handle == nil {
        return Err(_err("write: file is closed"))
    }
    i64 len = c.strlen(content)
    *u8 cstr = content.to_cstr() as *u8
    i64 wrote = c.fwrite(cstr, 1, len, f.handle)
    if wrote != len {
        return Err(_err("write: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn seek(File f, i64 offset, SeekFrom origin) -> Result(i64, string) {
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

fn tell(File f) -> Result(i64, string) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    i64 pos = _ftell64(f.handle)
    if pos < 0 {
        return Err(_err("tell failed"))
    }
    return Ok(pos)
}

fn size(File f) -> Result(i64, string) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    i64 saved = _ftell64(f.handle)
    _fseek64(f.handle, 0, 2)
    i64 sz = _ftell64(f.handle)
    _fseek64(f.handle, saved, 0)
    return Ok(sz)
}

fn rewind(File f) -> Result(int, string) {
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

fn remove(string path) -> Result(int, string) {
    int r = _os.raw_unlink(path)
    if r != 0 {
        return Err(_err("remove failed"))
    }
    int z = 0
    return Ok(z)
}

// Reads one line from stdin, stripping the trailing newline.
// Returns Ok(line) on success, Err("io: read_line: EOF") at end of input.
fn read_line() -> Result(string, string) {
    c.__ls_readline_exec()
    if c.__ls_readline_ok() == 0 {
        return Err(_err("read_line: EOF"))
    }
    object ptr = c.__ls_readline_take()
    i64 len = c.__ls_readline_len()
    string s = __string_take_buffer(ptr as *u8, len)
    return Ok(s)
}
