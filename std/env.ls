// std/env.ls — Environment variable module.
// Pure LS — no platform conditionals, no direct extern fn.
// All OS differences handled by std.os; exit helper via std.c.


import std.os as _os
import std.c as c

// ---- Public API ----

// Returns Some(value) or None if the variable is not set.
fn get(string name) -> Option(string) {
    object r = _os.raw_getenv(name)
    if r == nil {
        return None
    }
    return Some(from_cstr(r))
}

// Returns the value or `default` if not set.
fn get_or(string name, string default) -> string {
    object r = _os.raw_getenv(name)
    if r == nil {
        return default
    }
    return from_cstr(r)
}

// Returns the value or prints an error and exits if not set.
fn require(string name) -> string {
    object r = _os.raw_getenv(name)
    if r == nil {
        print(f"env: required variable '{name}' is not set")
        c.__ls_proc_exit(1)
    }
    return from_cstr(r)
}

// Returns true if the variable exists (even if empty).
fn has(string name) -> bool {
    object r = _os.raw_getenv(name)
    return r != nil
}

// Sets an environment variable.
fn set(string name, string value) {
    _os.raw_setenv(name, value)
}

// Deletes an environment variable.
fn delete(string name) {
    _os.raw_unsetenv(name)
}

// Returns a snapshot of all environment variables as a map.
fn all() -> map(string, string) {
    _os.raw_env_prepare()
    int n = _os.raw_env_count()
    map(string, string) m = {}
    int i = 0
    while i < n {
        string entry = from_cstr(_os.raw_env_entry(i))
        int eq = entry.find("=")
        if eq > 0 {
            string key = entry.substr(0, eq)
            string val = entry.substr(eq + 1, entry.length - eq - 1)
            m.set(key, val)
        }
        i = i + 1
    }
    return m
}
