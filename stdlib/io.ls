// stdlib/io.ls — Pure LS reimplementation of the built-in `io` module.
// Replaces src/builtins_io.c + src/builtins_io_cg.c.
//
// API parity with the previous built-in:
//   read_file / write_file / append_file / exists / remove
//   open / close / read_all / write
//   seek / tell / size / rewind
//   OpenMode + SeekFrom + File
//
// Implementation notes:
//   - Uses libc directly via FFI.
//   - i64 file positioning via _fseeki64/_ftelli64 (Windows) or fseeko/ftello.
//   - read_file does calloc(size+1, 1) + fread + from_cstr + free.
//     Files containing embedded NUL truncate at the first NUL.

module io

extern {
    fn fopen(string path, string mode) -> object
    fn fclose(object fp) -> int
    fn fread(*u8 buf, i64 sz, i64 n, object fp) -> i64
    fn fwrite(*u8 buf, i64 sz, i64 n, object fp) -> i64
    fn calloc(i64 n, i64 sz) -> *u8
    fn strlen(string s) -> i64
}

// Unlink-style file deletion (avoids `remove` symbol clash with our public API).
#if WINDOWS
extern fn _unlink(string path) -> int
fn _libc_unlink(string path) -> int { return _unlink(path) }
#else
extern fn unlink(string path) -> int
fn _libc_unlink(string path) -> int { return unlink(path) }
#end

#if WINDOWS
extern fn _fseeki64(object fp, i64 off, int origin) -> int
extern fn _ftelli64(object fp) -> i64
#else
extern fn fseeko(object fp, i64 off, int origin) -> int
extern fn ftello(object fp) -> i64
#end

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

#if WINDOWS
fn _fseek64(object fp, i64 off, int origin) -> int { return _fseeki64(fp, off, origin) }
fn _ftell64(object fp) -> i64 { return _ftelli64(fp) }
#else
fn _fseek64(object fp, i64 off, int origin) -> int { return fseeko(fp, off, origin) }
fn _ftell64(object fp) -> i64 { return ftello(fp) }
#end

fn _err(string msg) -> string {
    return "io: " + msg
}

// ---- Public API ----

fn read_file(string path) -> Result(string, string) {
    object fp = fopen(path, "rb")
    if fp == nil {
        return Err(_err("read_file: open failed"))
    }
    _fseek64(fp, 0, 2)
    i64 sz = _ftell64(fp)
    _fseek64(fp, 0, 0)
    if sz < 0 {
        fclose(fp)
        return Err(_err("read_file: tell failed"))
    }
    i64 cap = sz + 1
    *u8 buf = calloc(cap, 1)
    i64 nread = fread(buf, 1, sz, fp)
    fclose(fp)
    string s = from_cstr(buf)
    free(buf)
    if nread != sz {
        return Err(_err("read_file: read incomplete"))
    }
    return Ok(s)
}

fn write_file(string path, string content) -> Result(int, string) {
    object fp = fopen(path, "wb")
    if fp == nil {
        return Err(_err("write_file: open failed"))
    }
    i64 len = strlen(content)
    *u8 cstr = content.to_cstr() as *u8
    i64 wrote = fwrite(cstr, 1, len, fp)
    fclose(fp)
    if wrote != len {
        return Err(_err("write_file: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn append_file(string path, string content) -> Result(int, string) {
    object fp = fopen(path, "ab")
    if fp == nil {
        return Err(_err("append_file: open failed"))
    }
    i64 len = strlen(content)
    *u8 cstr = content.to_cstr() as *u8
    i64 wrote = fwrite(cstr, 1, len, fp)
    fclose(fp)
    if wrote != len {
        return Err(_err("append_file: write incomplete"))
    }
    int n = wrote as int
    return Ok(n)
}

fn exists(string path) -> bool {
    object fp = fopen(path, "rb")
    if fp == nil {
        return false
    }
    fclose(fp)
    return true
}

fn open(string path, OpenMode m) -> Result(File, string) {
    string ms = _mode_str(m)
    object fp = fopen(path, ms)
    if fp == nil {
        return Err(_err("open failed"))
    }
    bool b = _is_binary(m)
    File f = File { handle: fp, is_binary: b }
    return Ok(f)
}

fn close(File f) -> int {
    return fclose(f.handle)
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
    i64 cap = sz + 1
    *u8 buf = calloc(cap, 1)
    i64 nread = fread(buf, 1, sz, f.handle)
    string s = from_cstr(buf)
    free(buf)
    if nread != sz {
        return Err(_err("read_all: read incomplete"))
    }
    return Ok(s)
}

fn write(File f, string content) -> Result(int, string) {
    if f.handle == nil {
        return Err(_err("write: file is closed"))
    }
    i64 len = strlen(content)
    *u8 cstr = content.to_cstr() as *u8
    i64 wrote = fwrite(cstr, 1, len, f.handle)
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
    int r = _libc_unlink(path)
    if r != 0 {
        return Err(_err("remove failed"))
    }
    int z = 0
    return Ok(z)
}
