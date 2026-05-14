# LS Standard Library — Expansion Plan

## Context

The existing `docs/pure_ls_stdlib_plan.md` defines the **strategic direction**:
write stdlib in pure LS using `extern` C FFI, not compiler built-ins.
This document defines **what to build next** and the priority order.

### Current stdlib (as of Phase E.4)

| Module | Location | Status |
|--------|----------|--------|
| `math` | built-in codegen | ✅ complete |
| `io` | `stdlib/io.ls` (pure-LS) | ✅ v2 (seek/tell/size/append/remove) |

### Target stdlib surface

```
stdlib/
  io.ls       ✅ existing
  fs.ls       ← Phase S.1
  string.ls   ← Phase S.2
  time.ls     ← Phase S.3
  env.ls      ← Phase S.4
  json.ls     ← Phase S.5
  process.ls  ← Phase S.6
  net.ls      ← Phase S.7 (long-term)
  regex.ls    ← Phase S.8 (long-term)
```

All modules follow the same pattern as `stdlib/io.ls`:
pure LS source + `extern` calls into libc / platform APIs.

---

## Phase S.1 — `fs` Module (filesystem)

### API

```ls
import fs

// --- Path operations (pure string manipulation, no syscall) ---
string dir  = fs.dirname("/foo/bar/baz.ls")   // "/foo/bar"
string base = fs.basename("/foo/bar/baz.ls")  // "baz.ls"
string ext  = fs.extension("/foo/bar/baz.ls") // ".ls"
string stem = fs.stem("/foo/bar/baz.ls")      // "baz"
string p    = fs.join("/foo/bar", "baz.ls")   // "/foo/bar/baz.ls"
bool   abs  = fs.is_absolute("/foo")          // true

// --- Filesystem queries ---
bool   ok  = fs.exists("/foo/bar")
bool   dir = fs.is_dir("/foo/bar")
bool   fil = fs.is_file("/foo/bar/baz.ls")
i64    sz  = fs.file_size("/foo/bar/baz.ls")  // -1 on error

// --- Directory operations ---
Result(int, string) r = fs.mkdir("/foo/new")         // mkdir (one level)
Result(int, string) r = fs.mkdir_all("/foo/a/b/c")   // mkdir -p
Result(int, string) r = fs.remove("/foo/bar.ls")     // unlink
Result(int, string) r = fs.remove_dir("/foo/bar")    // rmdir (must be empty)
Result(int, string) r = fs.rename("/foo/a", "/foo/b")
Result(int, string) r = fs.copy("/foo/a.ls", "/foo/b.ls")

// --- Directory listing ---
Result(vec(string), string) entries = fs.list_dir("/foo")  // filenames only
Result(vec(string), string) all     = fs.glob("/foo/**/*.ls")

// --- Working directory ---
Result(string, string) cwd = fs.cwd()
Result(int, string)    r   = fs.chdir("/new/path")
```

### Implementation notes

- Pure LS using `extern` wrappers around:
  - POSIX: `stat`, `mkdir`, `unlink`, `rmdir`, `rename`, `opendir`/`readdir`/`closedir`, `getcwd`, `chdir`
  - Windows: `_stat`, `CreateDirectoryA`, `DeleteFileA`, `RemoveDirectoryA`, `FindFirstFileA` / `FindNextFileA`, `GetCurrentDirectoryA`
- `fs.glob` implemented in LS using `fs.list_dir` recursively; no external dependency.
- Path separator normalisation: `fs.join` always uses `/` internally; Windows paths accept both.

**Effort**: 4–6 days

---

## Phase S.2 — `string` Module (advanced string operations)

### API

```ls
import string as str   // avoids collision with built-in string type keyword

// --- Parsing ---
Result(int, string)   r = str.parse_int("42")
Result(f64, string)   r = str.parse_float("3.14")
Result(bool, string)  r = str.parse_bool("true")

// --- Formatting (complement to f"..." interpolation) ---
string s = str.format("{} items at ${:.2f} each", count, price)
// format specifiers:  {}  {:d}  {:.2f}  {:x}  {:o}  {:b}  {:08d}  {:<10}  {:>10}

// --- Splitting / joining (already in built-in string, wrappers here for consistency) ---
vec(string) parts = str.split_n("a,b,c,d", ",", 2)  // max 2 splits → ["a", "b,c,d"]
vec(string) lines = str.lines(text)                   // split on \n / \r\n
string joined     = str.join(parts, ", ")

// --- Inspection ---
bool  ok = str.is_numeric("3.14")
bool  ok = str.is_alpha("hello")
bool  ok = str.is_alnum("hello42")
int   n  = str.char_count(s)          // Unicode codepoint count (not byte count)
vec(int) cps = str.codepoints(s)      // Unicode codepoints as vec(int)

// --- Case and whitespace ---
string s = str.title("hello world")   // "Hello World"
string s = str.repeat("ab", 3)        // "ababab"
string s = str.pad_left("42", 6, '0') // "000042"
string s = str.pad_right("hi", 5, '.') // "hi..."
string s = str.strip_prefix(s, "http://")
string s = str.strip_suffix(s, ".ls")
```

### Format specifier implementation

`str.format` is implemented as a pure-LS function that walks the format string,
identifies `{}` placeholders, and calls the appropriate `int_to_str` / `float_to_str`
conversion. No `printf` at runtime for the format cases — the LS runtime already has
`__ls_int_to_str` / `__ls_float_to_str` IR helpers.

**Effort**: 5–7 days

---

## Phase S.3 — `time` Module

### API

```ls
import time

// --- Current time ---
i64    unix_ns  = time.now_unix_ns()    // nanoseconds since Unix epoch
i64    unix_ms  = time.now_unix_ms()    // milliseconds since Unix epoch
f64    unix_s   = time.now_unix_s()     // seconds (float)

// --- Struct-based datetime ---
time.DateTime dt = time.now_local()     // local time
time.DateTime dt = time.now_utc()       // UTC

// dt.year, dt.month (1-12), dt.day (1-31)
// dt.hour (0-23), dt.minute (0-59), dt.second (0-59)
// dt.weekday (0=Mon..6=Sun), dt.yday (0-365)

// --- Formatting ---
string s = time.format(dt, "%Y-%m-%d %H:%M:%S")   // strftime-style
string s = time.iso8601(dt)                         // "2026-05-14T10:30:00+08:00"

// --- Parsing ---
Result(time.DateTime, string) r = time.parse("2026-05-14", "%Y-%m-%d")

// --- Duration arithmetic ---
time.Duration d  = time.duration_ns(1_000_000)  // 1 ms
time.DateTime dt2 = time.add(dt, d)
i64 diff_s = time.diff_s(dt1, dt2)              // signed seconds between two DateTimes

// --- Sleep ---
time.sleep_ms(500)     // block for 500 ms (uses nanosleep on Linux, Sleep on Windows)
time.sleep_us(100)
```

### Implementation notes

- `time.now_local()` / `time.now_utc()` via `time(NULL)` + `localtime_r` / `gmtime_r`
- `time.format` wraps `strftime`
- `time.sleep_ms` uses `nanosleep` (POSIX) / `Sleep` (Windows) via `#if WINDOWS` at LS source level using the LS conditional compilation feature

**Effort**: 3–4 days

---

## Phase S.4 — `env` Module

### API

```ls
import env

// --- Program arguments ---
vec(string) args = env.args()       // [program_name, arg1, arg2, ...]

// --- Environment variables ---
Option(string) v = env.get("HOME")
string v         = env.get_or("HOME", "/tmp")
Result(int,string) r = env.set("MY_VAR", "value")
Result(int,string) r = env.unset("MY_VAR")
map(string,string) all = env.all()  // full environment

// --- Process ---
int pid = env.pid()
int r   = env.exit(0)               // equivalent to exit(0)
```

### Implementation notes

- `env.args()` reads `argc`/`argv` passed through a global set by `main()` entry point.
  Requires `codegen.c` to emit a `__ls_set_args(argc, argv)` call at the start of `main`.
- All others wrap libc: `getenv`, `setenv`/`putenv`, `unsetenv`, `getenv`+environ ptr, `getpid`, `exit`.

**Effort**: 2–3 days

---

## Phase S.5 — `json` Module

### API

```ls
import json

// --- Parsing ---
Result(json.Value, string) r = json.parse("{\"key\": 42}")

json.Value v = r.unwrap()
// Type checks:   v.is_object(), v.is_array(), v.is_string(), v.is_int(), v.is_float(), v.is_bool(), v.is_null()
// Access:
string s   = v.get_string()          // panics if wrong type
int    n   = v.get_int()
f64    f   = v.get_float()
bool   b   = v.get_bool()

// Object access:
Option(json.Value) child = v.get("key")
json.Value child         = v["key"]    // panics if missing

// Array access:
int len              = v.len()
json.Value item      = v[0]            // panics if out of range
vec(json.Value) arr  = v.as_array()

// Path access (dot-notation, no regex):
Option(json.Value) r = v.path("users[0].name")

// --- Building ---
json.Value obj  = json.object()
json.set(obj, "name",  json.str("Alice"))
json.set(obj, "age",   json.int(30))
json.set(obj, "score", json.float(9.5))

json.Value arr = json.array()
json.push(arr, json.str("a"))
json.push(arr, json.int(1))

// --- Serialising ---
string s        = json.stringify(v)             // compact
string s        = json.stringify_pretty(v, 2)   // 2-space indent
```

### Implementation

`json.Value` is a tagged union (`enum`-like struct):

```
JsonValue {
    disc: int     // 0=null 1=bool 2=int 3=float 4=string 5=array 6=object
    union:
        bool   b
        i64    i
        f64    f
        string s
        vec(JsonValue)           arr
        map(string, JsonValue)   obj
}
```

Implemented entirely in pure LS + the existing `vec`/`map`/`string` builtins.
The parser is a hand-written recursive descent parser in LS.

**Effort**: 7–10 days

---

## Phase S.6 — `process` Module

### API

```ls
import process

// --- Run subprocess, capture output ---
Result(string, string) r = process.capture("ls -la /tmp")  // stdout as string
Result(int, string)    r = process.run("make test")         // exit code only

// --- Structured subprocess ---
process.Command cmd = process.command("git")
process.arg(cmd, "log")
process.arg(cmd, "--oneline")
process.arg(cmd, "-5")
Result(process.Output, string) r = process.exec(cmd)
// r.stdout : string
// r.stderr : string
// r.exit_code : int
```

### Implementation notes

- Linux: `fork` + `execvp` + `pipe` + `waitpid`
- Windows: `CreateProcessA` + anonymous pipes + `WaitForSingleObject`
- Both wrapped via `extern` in LS; the platform difference handled by `#if WINDOWS`

**Effort**: 4–6 days

---

## Phase S.7 — `net` Module (long-term)

Provides basic TCP/UDP socket I/O. This is a larger undertaking.

### Minimal API

```ls
import net

// --- TCP client ---
Result(net.Conn, string) r = net.dial_tcp("example.com:80")
net.write(conn, "GET / HTTP/1.0\r\n\r\n")
string resp = net.read_all(conn)
net.close(conn)

// --- TCP server ---
Result(net.Listener, string) r = net.listen_tcp(":8080")
net.Conn c = net.accept(listener)
```

**Effort**: 10–14 days (socket API is large and platform-specific)

---

## Phase S.8 — `regex` Module (long-term)

Two options:

**Option A** (recommended): Pure-LS NFA-based regex engine.
- Subset of PCRE: `.` `*` `+` `?` `[abc]` `[^abc]` `{m,n}` `^` `$` `\d` `\w` `\s` `\b`
- No backtracking (NFA guarantees O(n) matching)
- Effort: 14–21 days

**Option B** (FFI): Wrap `<regex.h>` (POSIX) / `pcre2` (Windows + Linux optional).
- Much less code in LS
- Adds an optional external dependency
- Effort: 3–5 days

---

## Priority Order

```
S.1 (fs)       ← high value, unlocks file-manipulation scripts
S.2 (string)   ← str.format alone is high-demand
S.3 (time)     ← simple, frequently needed
S.4 (env)      ← prerequisite for many real programs
S.5 (json)     ← high value for web/config use cases
S.6 (process)  ← enables scripting use case
S.7 (net)      ← long-term
S.8 (regex)    ← long-term, Option B can be shipped fast
```

| Module | Effort | Value |
|--------|--------|-------|
| S.1 `fs` | 4–6 d | ★★★★★ |
| S.2 `string` | 5–7 d | ★★★★☆ |
| S.3 `time` | 3–4 d | ★★★★☆ |
| S.4 `env` | 2–3 d | ★★★☆☆ |
| S.5 `json` | 7–10 d | ★★★★★ |
| S.6 `process` | 4–6 d | ★★★☆☆ |
| S.7 `net` | 10–14 d | ★★★☆☆ |
| S.8 `regex` | 3–5 d (B) | ★★★★☆ |
