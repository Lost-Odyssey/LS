// std/io.ls — File I/O module.
// Pure LS — no platform conditionals, no direct extern def (all via std.sys.c / std.sys.os).
//
// FFI rule (string->Str migration): paths handed to the DIRECT extern def
// c.fopen go through a builtin-string local first — the Str->string var-decl
// bridge copies + NUL-terminates (Str.data itself has NO NUL and the call-arg
// bridge does not cover direct extern calls). Write paths use Str.as_ptr+len
// (fwrite is length-based, no NUL needed); read paths wrap the malloc'd buffer
// into an owned Str zero-copy.


import std.sys.os as _os
import std.sys.c as c
import std.core.str
import std.core.sink as sink

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

// File is an RAII resource handle: it auto-closes at scope exit (no leak on a
// forgotten close), and nils the handle so an explicit `close` followed by the
// destructor — or two closes — is a safe no-op (no double fclose). Owning an
// `object` handle with a destructor and no Clone makes File MOVE-ONLY: passing
// it by value is a compile error (use `&File` / `&!File`); the only owner is
// whoever holds it, and ownership transfers on move (mirrors Rust's File).
methods File: Destroy {
    def ~(&!self) {
        if self.handle != nil {
            c.fclose(self.handle)
            self.handle = nil
        }
    }
}

// ---- Internal helpers ----

// Returns the fopen mode; c.fopen takes *u8, so callers pass `_mode_str(m).c_str()`.
def _mode_str(OpenMode m) -> Str {
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

def _is_binary(OpenMode m) -> bool {
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

def _seek_origin(SeekFrom o) -> int {
    int r = 0
    match o {
        Start   => { r = 0 }
        Current => { r = 1 }
        End     => { r = 2 }
    }
    return r
}

def _fseek64(object fp, i64 off, int origin) -> int { return _os.raw_fseek64(fp, off, origin) }
def _ftell64(object fp) -> i64 { return _os.raw_ftell64(fp) }

def _err(Str msg) -> Str {
    Str e = "io: "
    e.push_str(msg)
    return e
}

// Wrap a tracked (c.malloc'd) buffer into an owned Str, zero-copy.
// cap must be the malloc'd size (> len). The Str drop frees the buffer.
def _own_buf(*u8 buf, i64 len, i64 cap) -> Str {
    return Str { data: buf, len: len as int, cap: cap as int }
}

// ---- Public API ----

def read_file(Str path) -> Result(Str, Str) {
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

def write_file(Str path, Str content) -> Result(int, Str) {
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

def append_file(Str path, Str content) -> Result(int, Str) {
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

def exists(Str path) -> bool {
    Str mode = "rb"
    object fp = c.fopen(path.c_str(), mode.c_str())
    if fp == nil {
        return false
    }
    c.fclose(fp)
    return true
}

def open(Str path, OpenMode m) -> Result(File, Str) {
    Str ms = _mode_str(m)
    object fp = c.fopen(path.c_str(), ms.c_str())
    if fp == nil {
        return Err(_err("open failed"))
    }
    bool b = _is_binary(m)
    File f = File { handle: fp, is_binary: b }
    return Ok(f)
}

// Make this File the @print destination: steal its handle into a SinkDest and
// nil the source so the File's destructor becomes a no-op (ownership transfers
// to the sink, which closes it on the next set_sink / at exit). The source File
// is left an inert husk (handle=nil) — later io.write returns "file is closed".
// Use: `set_sink(io.file(&!log))`. See docs/plan_print_sink.md §2.4.
def file(&!File f) -> sink.SinkDest {
    object h = f.handle
    f.handle = nil
    return Fp(h)                    // bare variant; expected-type hint = sink.SinkDest
}

// Explicit early close. Optional — File auto-closes at scope exit — but useful
// to release the handle before the end of scope. Nils the handle so the later
// destructor no-ops (no double fclose). Idempotent.
def close(&!File f) -> int {
    if f.handle == nil {
        return 0
    }
    int r = c.fclose(f.handle)
    f.handle = nil
    return r
}

def read_all(&File f) -> Result(Str, Str) {
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

def write(&File f, Str content) -> Result(int, Str) {
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

// Flush buffered writes to the OS without closing the file. Useful for streaming
// writers that must make data durable mid-stream (the close/auto-close also
// flushes).
def flush(&File f) -> Result(int, Str) {
    if f.handle == nil {
        return Err(_err("flush: file is closed"))
    }
    int r = c.fflush(f.handle)
    if r != 0 {
        return Err(_err("flush failed"))
    }
    int z = 0
    return Ok(z)
}

def seek(&File f, i64 offset, SeekFrom origin) -> Result(i64, Str) {
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

def tell(&File f) -> Result(i64, Str) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    i64 pos = _ftell64(f.handle)
    if pos < 0 {
        return Err(_err("tell failed"))
    }
    return Ok(pos)
}

def size(&File f) -> Result(i64, Str) {
    if !f.is_binary || f.handle == nil {
        return Err(_err("file is text-mode or closed (positioning requires binary)"))
    }
    i64 saved = _ftell64(f.handle)
    _fseek64(f.handle, 0, 2)
    i64 sz = _ftell64(f.handle)
    _fseek64(f.handle, saved, 0)
    return Ok(sz)
}

def rewind(&File f) -> Result(int, Str) {
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

def remove(Str path) -> Result(int, Str) {
    int r = _os.raw_unlink(path)
    if r != 0 {
        return Err(_err("remove failed"))
    }
    int z = 0
    return Ok(z)
}

// Reads one line from stdin, stripping the trailing newline.
// Returns Ok(line) on success, Err("io: read_line: EOF") at end of input.
def read_line() -> Result(Str, Str) {
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
