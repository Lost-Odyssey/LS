// std/proc.ls — Process module: args, exec, run, pid, exit.
// Pure LS — no platform conditionals, no direct extern fn.
// All OS differences handled by std.os; C bindings via std.c.

import std.vec
import std.os as _os
import std.c as c
import std.str

// ---- ExecResult struct ----

struct ExecResult {
    Str stdout
    Str stderr
    int exit_code
}

// ---- Public API ----

// Returns command-line arguments (excluding argv[0]).
fn args() -> Vec(Str) {
    int n = c.__ls_get_argc()
    Vec(Str) result = {}
    int i = 1
    while i < n {
        Str a = from_cstr(c.__ls_get_argv(i))
        result.push(a)
        i = i + 1
    }
    return result
}

// Returns argv[0] — the program path/name.
fn program() -> Str {
    Str p = from_cstr(c.__ls_get_argv(0))
    return p
}

// Returns the current process ID.
fn pid() -> int {
    return _os.raw_pid()
}

// Exits the process with the given status code.
fn exit(int code) {
    c.__ls_proc_exit(code)
}

// Runs cmd via the system shell, streaming output to the terminal.
// Returns the exit code (0 = success).
fn run(Str cmd) -> int {
    /* c.system is a DIRECT extern fn taking *u8 (NUL-terminated char*).
       Str.c_str() guarantees the NUL terminator. */
    int raw = c.system(cmd.c_str())
    return _os.raw_wait_exit_code(raw)
}

// Runs cmd via popen, captures stdout+stderr (merged via 2>&1).
// Returns Ok(output) on success, Err(output) on non-zero exit.
// v1 limit: output is capped at 4 MB; use exec_full for large outputs.
fn exec(Str cmd) -> Result(Str, Str) {
    Str full = cmd.concat(" 2>&1")
    object fp = _os.raw_popen(full)
    if fp == nil {
        return Err("proc: exec: failed to start process")
    }
    i64 maxsz = 4194304
    *u8 buf = c.malloc(maxsz)
    i64 nread = _os.raw_pread(fp, buf as object, maxsz)
    // Own the c.malloc'd buffer zero-copy (Str drop frees it) — like io._own_buf.
    // Replaces the old `string raw_out = __string_take_buffer(...); Str out = raw_out`
    // which cloned into out and leaked the builtin-string intermediate.
    Str out = Str { data: buf, len: nread as int, cap: maxsz as int }
    int code = _os.raw_pclose(fp)
    if code != 0 {
        return Err(out)
    }
    return Ok(out)
}

// Copy n bytes out of a backend-owned buffer into an owned Str. Binary-safe
// (no NUL dependence). The backend frees its buffer on the next exec_run —
// taking ownership instead would make LS free a backend-malloc'd pointer,
// which --memcheck reports as INVALID FREE (untracked allocation).
fn _copy_buf(object p, i64 n) -> Str {
    Str s = ""
    if p == nil { return s }
    int len = n as int
    s.reserve(len)
    *u8 b = p as *u8
    for (int i = 0; i < len; i = i + 1) {
        s.push_byte(b[i])
    }
    return s
}

// Runs cmd with separate stdout and stderr capture.
// Returns Ok(ExecResult) or Err(message) if the process could not be started.
fn exec_full(Str cmd) -> Result(ExecResult, Str) {
    _os.raw_exec_run(cmd)
    if _os.raw_exec_get_ok() == 0 {
        return Err("proc: exec_full: failed to start process")
    }
    Str out = _copy_buf(_os.raw_exec_stdout_ptr(), _os.raw_exec_stdout_len())
    Str err = _copy_buf(_os.raw_exec_stderr_ptr(), _os.raw_exec_stderr_len())
    int code = _os.raw_exec_get_code()
    ExecResult r = ExecResult { stdout: out, stderr: err, exit_code: code }
    return Ok(r)
}
