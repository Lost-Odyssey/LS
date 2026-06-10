// std/proc.ls — Process module: args, exec, run, pid, exit.
// Pure LS — no platform conditionals, no direct extern fn.
// All OS differences handled by std.os; C bindings via std.c.

import std.vec
import std.os as _os
import std.c as c

// ---- ExecResult struct ----

struct ExecResult {
    string stdout
    string stderr
    int    exit_code
}

// ---- Public API ----

// Returns command-line arguments (excluding argv[0]).
fn args() -> Vec(string) {
    int n = c.__ls_get_argc()
    Vec(string) result = {}
    int i = 1
    while i < n {
        result.push(from_cstr(c.__ls_get_argv(i)))
        i = i + 1
    }
    return result
}

// Returns argv[0] — the program path/name.
fn program() -> string {
    return from_cstr(c.__ls_get_argv(0))
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
fn run(string cmd) -> int {
    int raw = c.system(cmd)
    return _os.raw_wait_exit_code(raw)
}

// Runs cmd via popen, captures stdout+stderr (merged via 2>&1).
// Returns Ok(output) on success, Err(output) on non-zero exit.
// v1 limit: output is capped at 4 MB; use exec_full for large outputs.
fn exec(string cmd) -> Result(string, string) {
    string full = cmd + " 2>&1"
    object fp = _os.raw_popen(full)
    if fp == nil {
        return Err("proc: exec: failed to start process")
    }
    i64 maxsz = 4194304
    *u8 buf = c.malloc(maxsz)
    i64 nread = _os.raw_pread(fp, buf as object, maxsz)
    string out = __string_take_buffer(buf, nread)
    int code = _os.raw_pclose(fp)
    if code != 0 {
        return Err(out)
    }
    return Ok(out)
}

// Runs cmd with separate stdout and stderr capture.
// Returns Ok(ExecResult) or Err(message) if the process could not be started.
fn exec_full(string cmd) -> Result(ExecResult, string) {
    _os.raw_exec_run(cmd)
    if _os.raw_exec_get_ok() == 0 {
        return Err("proc: exec_full: failed to start process")
    }
    object out_ptr = _os.raw_exec_take_stdout()
    i64 out_len = _os.raw_exec_stdout_len()
    object err_ptr = _os.raw_exec_take_stderr()
    i64 err_len = _os.raw_exec_stderr_len()
    string out  = __string_take_buffer(out_ptr as *u8, out_len)
    string err  = __string_take_buffer(err_ptr as *u8, err_len)
    int code    = _os.raw_exec_get_code()
    ExecResult r = ExecResult { stdout: out, stderr: err, exit_code: code }
    return Ok(r)
}
