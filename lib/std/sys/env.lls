// std/env.ls — Environment variable module.
// Pure LS — no platform conditionals, no direct extern def.
// All OS differences handled by std.sys.os; exit helper via std.sys.c.


import std.sys.os as _os
import std.sys.c as c
import std.core.map
import std.core.str

// ---- Public API ----

// Returns Some(value) or None if the variable is not set.
def get(Str name) -> Option(Str) {
    object r = _os.raw_getenv(name)
    if r == nil {
        return None
    }
    Str v = from_cstr(r)
    return Some(v)
}

// Returns the value or `default` if not set.
def get_or(Str name, Str default) -> Str {
    object r = _os.raw_getenv(name)
    if r == nil {
        return default
    }
    Str v = from_cstr(r)
    return v
}

// Returns the value or prints an error and exits if not set.
def require(Str name) -> Str {
    object r = _os.raw_getenv(name)
    if r == nil {
        @print(f"env: required variable '{name}' is not set")
        c.__ls_proc_exit(1)
    }
    Str v = from_cstr(r)
    return v
}

// Returns true if the variable exists (even if empty).
def has(Str name) -> bool {
    object r = _os.raw_getenv(name)
    return r != nil
}

// Sets an environment variable.
def set(Str name, Str value) {
    _os.raw_setenv(name, value)
}

// Deletes an environment variable.
def delete(Str name) {
    _os.raw_unsetenv(name)
}

// Returns a snapshot of all environment variables as a map.
def all() -> Map(Str, Str) {
    _os.raw_env_prepare()
    int n = _os.raw_env_count()
    Map(Str, Str) m = {}
    Str eqs = "="
    int i = 0
    while i < n {
        Str entry = from_cstr(_os.raw_env_entry(i))
        int eq = entry.find(eqs)
        if eq > 0 {
            Str key = entry.substr(0, eq)
            Str val = entry.substr(eq + 1, entry.len() - eq - 1)
            m.set(key, val)
        }
        i = i + 1
    }
    return m
}
