// std/core/value.ls — a neutral, format-agnostic value tree for serialization.
//
// @derive(Serialize) (docs/plan_static_reflection.md Stage 2) synthesizes
//   def to_value(&self) -> Value
// from a struct's fields. Format modules render the SAME tree to their own
// output: this module's `Value.to_json()` -> JSON; future csv/toml/msgpack
// encoders consume the same tree. The tree itself knows no format — that lives in
// the renderer, so one @derive(Serialize) feeds every format.
//
// Object uses parallel keys/vals vectors (not a Map) to PRESERVE field order
// (deterministic, testable output) and to avoid a Pair<->Value mutual recursion.
//
// `Value` lives HERE (not in std.core.str): it must NOT leak into every program's
// global namespace. std.core.str is prelude-imported everywhere, so a `Value` enum
// there would collide with any user's own `enum Value`. Keeping it in value.ls means
// `Value`/`VInt`/`VStr`/... are visible ONLY when a module `import std.core.value`.

import std.core.str
import std.core.vec

// Variants are V-prefixed so they never collide with another value-tree enum's bare
// constructors (e.g. std.text.json's JsonValue has Null/Bool/Text/Object).
enum Value {
    VNull
    VBool(bool)
    VInt(i64)
    VFloat(f64)
    VStr(Str)
    VList(Vec(Value))
    VObj(Vec(Str), Vec(Value))
}

interface Serialize {
    def to_value(&self) -> Value
}

// @derive(Deserialize) target: rebuild a value from a neutral tree. `from_value` is
// BEST-EFFORT (missing/mismatched fields fall back to a zero/empty default).
interface Deserialize {
    static def from_value(Value v) -> Self
}

// @derive(Deserialize) ALSO synthesizes `try_from_value` — the STRICT variant: a
// missing field or a type mismatch is an `Err("...")` instead of a silent default,
// with the error propagated (the first bad field wins). Use it when you need to
// validate input; use `from_value` when lenient best-effort is fine.
interface TryDeserialize {
    static def try_from_value(Value v) -> Result(Self, Str)
}

// ---- Deserialize leaf/field helpers (used by the synthesized from_value) ----

// Look up a field by key in a VObj; returns a clone of its value, else VNull.
def obj_get(&Value v, &Str key) -> Value {
    match v {
        VObj(keys, vals) => {
            int n = keys.len()
            for i in 0..n {
                if keys.get_ref(i).eq?(key) {
                    // get(i) returns Option(Value) = an owned clone (can't copy
                    // an owned value out of a borrow result directly).
                    match vals.get(i) {
                        Some(out) => { return out }
                        None => { return VNull }
                    }
                }
            }
            return VNull
        }
        _ => { return VNull }
    }
}

def as_int(&Value v) -> i64 {
    // Accept VFloat too: JSON numbers are f64, and from_json maps whole numbers to
    // VInt but a non-integral value still reads as a truncated int rather than 0.
    match v { VInt(i) => { return i } VFloat(f) => { return f as i64 } _ => { return 0 as i64 } }
}
def as_f64(&Value v) -> f64 {
    match v {
        VFloat(f) => { return f }
        VInt(i) => { return i as f64 }
        _ => { return 0.0 }
    }
}
def as_bool(&Value v) -> bool {
    match v { VBool(b) => { return b } _ => { return false } }
}
def as_str(&Value v) -> Str {
    match v { VStr(s) => { return s.copy() } _ => { return "" } }
}

// ---- STRICT leaf extractors (used by the synthesized try_from_value) ----
// A wrong variant is an Err, not a silent default. (Numbers accept VInt<->VFloat,
// since JSON has only one number type and from_json maps whole numbers to VInt.)
def try_as_int(Value v) -> Result(i64, Str) {
    match v { VInt(i) => { return Ok(i) } VFloat(f) => { return Ok(f as i64) }
              _ => { return Err("expected int") } }
}
def try_as_f64(Value v) -> Result(f64, Str) {
    match v { VFloat(f) => { return Ok(f) } VInt(i) => { return Ok(i as f64) }
              _ => { return Err("expected number") } }
}
def try_as_bool(Value v) -> Result(bool, Str) {
    match v { VBool(b) => { return Ok(b) } _ => { return Err("expected bool") } }
}
def try_as_str(Value v) -> Result(Str, Str) {
    match v { VStr(s) => { return Ok(s) } _ => { return Err("expected string") } }
}

// ---- primitive + Str Serialize / Deserialize impls ----
//
// @derive(Serialize/Deserialize) on a GENERIC struct lowers a type-param field `T f`
// to `self.f.to_value()` / `T.from_value(...)` (the concrete type isn't known at
// synthesis), so every type a `Box(T)` may hold needs these impls. Concrete
// (non-generic) derives format primitives/Str inline (VInt/VStr/... + as_int/...)
// and never call them — they back the monomorphized generic path.
//
// Builtin scalar targets carry no module prefix, so importers resolve the same
// symbol (int.to_value, int.from_value). `Str` is a struct defined in std.core.str;
// its trait-impl methods are keyed by Str's real type prefix (std_core_str__Str.*)
// at both registration (impl_key_of_type) and dispatch, so placing them HERE — the
// only module that has BOTH `Str` (imported) and `Value` (local) without a cycle —
// resolves correctly. (str.ls can't host them: it would have to import value back =
// circular; value.ls can't import str's Value into str.)
methods int: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods i64: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods f64: Serialize  { def to_value(&self) -> Value { return VFloat(self) } }
methods bool: Serialize { def to_value(&self) -> Value { return VBool(self) } }
methods char: Serialize { def to_value(&self) -> Value { return VInt(self as i64) } }
methods Str: Serialize  { def to_value(&self) -> Value { return VStr(self.copy()) } }
// Sized integer / f32 scalars. Integers widen to i64 (VInt); f32 widens to f64.
methods i8: Serialize   { def to_value(&self) -> Value { return VInt(self as i64) } }
methods i16: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods i32: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods u8: Serialize   { def to_value(&self) -> Value { return VInt(self as i64) } }
methods u16: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods u32: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods u64: Serialize  { def to_value(&self) -> Value { return VInt(self as i64) } }
methods f32: Serialize  { def to_value(&self) -> Value { return VFloat(self as f64) } }

methods int: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) as int } }
methods i64: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) } }
methods f64: Deserialize  { static def from_value(Value v) -> Self { return as_f64(v) } }
methods bool: Deserialize { static def from_value(Value v) -> Self { return as_bool(v) } }
methods char: Deserialize { static def from_value(Value v) -> Self { return as_int(v) as char } }
methods Str: Deserialize  { static def from_value(Value v) -> Self { return as_str(v) } }
// Sized integer / f32 scalars (narrow back from the widened i64 / f64 leaf).
methods i8: Deserialize   { static def from_value(Value v) -> Self { return as_int(v) as i8 } }
methods i16: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) as i16 } }
methods i32: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) as i32 } }
methods u8: Deserialize   { static def from_value(Value v) -> Self { return as_int(v) as u8 } }
methods u16: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) as u16 } }
methods u32: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) as u32 } }
methods u64: Deserialize  { static def from_value(Value v) -> Self { return as_int(v) as u64 } }
methods f32: Deserialize  { static def from_value(Value v) -> Self { return as_f64(v) as f32 } }

// STRICT primitive + Str try_from_value impls. These back @derive(TryDeserialize on
// generics: a type-param field T lowers to `try T.try_from_value(...)`, resolved on
// the concrete T at monomorphization. `try` propagates a strict-leaf Err.
methods int: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as int) } }
methods i64: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok(try try_as_int(v)) } }
methods f64: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok(try try_as_f64(v)) } }
methods bool: TryDeserialize { static def try_from_value(Value v) -> Result(Self, Str) { return Ok(try try_as_bool(v)) } }
methods char: TryDeserialize { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as char) } }
methods Str: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok(try try_as_str(v)) } }
methods i8: TryDeserialize   { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as i8) } }
methods i16: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as i16) } }
methods i32: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as i32) } }
methods u8: TryDeserialize   { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as u8) } }
methods u16: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as u16) } }
methods u32: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as u32) } }
methods u64: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_int(v)) as u64) } }
methods f32: TryDeserialize  { static def try_from_value(Value v) -> Result(Self, Str) { return Ok((try try_as_f64(v)) as f32) } }

// JSON-escape a string body (no surrounding quotes).
def _json_escape(&Str s) -> Str {
    Str out = ""
    int n = s.len()
    for i in 0..n {
        int b = s.byte_at(i)
        if b == 34 { out = out + "\\\"" }        // "
        else if b == 92 { out = out + "\\\\" }   // backslash
        else if b == 10 { out = out + "\\n" }
        else if b == 9  { out = out + "\\t" }
        else if b == 13 { out = out + "\\r" }
        else { out.push_byte(b) }
    }
    return out
}

// Built-in renderers over the neutral tree. to_value() (the @derive(Serialize)
// output) stays format-agnostic; this method is where JSON rendering lives. Add
// to_csv()/to_toml() similarly as needed. Recursion is the natural chained form —
// `items.get_ref(i).to_json()` — a method call on a borrow-returning call result.
methods Value {
    def to_json(&self) -> Str {
        match self {
            VNull => { return "null" }
            VBool(b) => { if b { return "true" } return "false" }
            VInt(i) => { return f"{i}" }
            VFloat(f) => { return f"{f}" }
            VStr(s) => {
                Str out = "\""
                out = out + _json_escape(s)
                out = out + "\""
                return out
            }
            VList(items) => {
                Str out = "["
                int n = items.len()
                for i in 0..n {
                    if i > 0 { out = out + "," }
                    out = out + items.get_ref(i).to_json()
                }
                out = out + "]"
                return out
            }
            VObj(keys, vals) => {
                Str out = "{"
                int n = keys.len()
                for i in 0..n {
                    if i > 0 { out = out + "," }
                    out = out + "\""
                    out = out + keys.get_ref(i)
                    out = out + "\":"
                    out = out + vals.get_ref(i).to_json()
                }
                out = out + "}"
                return out
            }
        }
    }
}

// ============================================================================
// from_json — parse a JSON STRING straight into the neutral Value tree, so a
// derived T.from_value can rebuild a struct from text:
//     T.from_value(from_json(text).unwrap())
// completing struct -> to_value() -> to_json() -> [text] -> from_json() -> from_value().
//
// A self-contained recursive-descent parser lives HERE (not std.text.json) on
// purpose: json.ls returns its own JsonValue, and a bridge there would export a
// `-> Value` signature, which breaks every json importer that does not also import
// std.core.value (a cross-module export-type visibility limit). value.ls owns
// Value, so the JSON->Value parser belongs with it.
// ============================================================================

struct _VP { Str s; int pos; int len }

def _vp_ws(&!_VP p) {
    while p.pos < p.len {
        int c = p.s.byte_at!(p.pos)
        if c == 32 || c == 9 || c == 10 || c == 13 { p.pos = p.pos + 1 } else { return }
    }
}

def _vp_hex(int c) -> int {
    if c >= 48 && c <= 57 { return c - 48 }
    if c >= 97 && c <= 102 { return c - 87 }
    if c >= 65 && c <= 70 { return c - 55 }
    return 0 - 1
}

// Encode a Unicode codepoint as UTF-8 bytes into out (BMP; 1-3 bytes).
def _vp_utf8(&!Str out, int cp) {
    if cp < 128 { out.push_byte(cp) }
    else if cp < 2048 {
        out.push_byte(192 + (cp / 64))
        out.push_byte(128 + (cp % 64))
    } else {
        out.push_byte(224 + (cp / 4096))
        out.push_byte(128 + ((cp / 64) % 64))
        out.push_byte(128 + (cp % 64))
    }
}

def _vp_string(&!_VP p) -> Result(Str, Str) {
    p.pos = p.pos + 1
    Str out = ""
    while p.pos < p.len {
        int c = p.s.byte_at!(p.pos)
        p.pos = p.pos + 1
        if c == 34 { return Ok(out) }
        if c == 92 {
            if p.pos >= p.len { return Err("json: bad escape") }
            int e = p.s.byte_at!(p.pos)
            p.pos = p.pos + 1
            if e == 34 { out.push_byte(34) }
            else if e == 92 { out.push_byte(92) }
            else if e == 47 { out.push_byte(47) }
            else if e == 110 { out.push_byte(10) }
            else if e == 116 { out.push_byte(9) }
            else if e == 114 { out.push_byte(13) }
            else if e == 98 { out.push_byte(8) }
            else if e == 102 { out.push_byte(12) }
            else if e == 117 {
                if p.pos + 4 > p.len { return Err("json: bad u escape") }
                int cp = 0
                for i in 0..4 {
                    int h = _vp_hex(p.s.byte_at!(p.pos + i))
                    if h < 0 { return Err("json: bad u hex") }
                    cp = cp * 16 + h
                }
                p.pos = p.pos + 4
                _vp_utf8(&!out, cp)
            }
            else { return Err("json: bad escape") }
        } else {
            out.push_byte(c)
        }
    }
    return Err("json: unterminated string")
}

def _vp_number(&!_VP p) -> Result(Value, Str) {
    int start = p.pos
    bool isf = false
    if p.pos < p.len {
        if p.s.byte_at!(p.pos) == 45 { p.pos = p.pos + 1 }
    }
    while p.pos < p.len {
        int c = p.s.byte_at!(p.pos)
        if c >= 48 && c <= 57 { p.pos = p.pos + 1 }
        else if c == 46 || c == 101 || c == 69 || c == 43 || c == 45 {
            isf = true
            p.pos = p.pos + 1
        } else { break }
    }
    if p.pos == start { return Err("json: invalid number") }
    Str num = p.s.substr(start, p.pos - start)
    if isf {
        match num.to_float() {
            Ok(f) => { return Ok(VFloat(f)) }
            Err(e) => { return Err("json: bad float") }
        }
    }
    match num.to_i64() {
        Ok(n) => { return Ok(VInt(n)) }
        Err(e) => { return Err("json: bad int") }
    }
}

def _vp_array(&!_VP p) -> Result(Value, Str) {
    p.pos = p.pos + 1
    Vec(Value) vs = []
    _vp_ws(&!p)
    if p.pos < p.len {
        if p.s.byte_at!(p.pos) == 93 { p.pos = p.pos + 1  return Ok(VList(vs)) }
    }
    while true {
        match _vp_value(&!p) {
            Ok(v) => { vs.push(v) }
            Err(e) => { return Err(e) }
        }
        _vp_ws(&!p)
        if p.pos >= p.len { return Err("json: unterminated array") }
        int c = p.s.byte_at!(p.pos)
        p.pos = p.pos + 1
        if c == 44 {  }
        else if c == 93 { return Ok(VList(vs)) }
        else { return Err("json: expected comma or close-bracket") }
    }
    return Err("json: unreachable")
}

def _vp_object(&!_VP p) -> Result(Value, Str) {
    p.pos = p.pos + 1
    Vec(Str) ks = []
    Vec(Value) vs = []
    _vp_ws(&!p)
    if p.pos < p.len {
        if p.s.byte_at!(p.pos) == 125 { p.pos = p.pos + 1  return Ok(VObj(ks, vs)) }
    }
    while true {
        _vp_ws(&!p)
        if p.pos >= p.len { return Err("json: expected key") }
        if p.s.byte_at!(p.pos) != 34 { return Err("json: expected key string") }
        Str key = ""
        match _vp_string(&!p) {
            Ok(k) => { key = k }
            Err(e) => { return Err(e) }
        }
        _vp_ws(&!p)
        if p.pos >= p.len { return Err("json: expected colon") }
        if p.s.byte_at!(p.pos) != 58 { return Err("json: expected colon") }
        p.pos = p.pos + 1
        match _vp_value(&!p) {
            Ok(val) => { ks.push(key)  vs.push(val) }
            Err(e) => { return Err(e) }
        }
        _vp_ws(&!p)
        if p.pos >= p.len { return Err("json: unterminated object") }
        int c = p.s.byte_at!(p.pos)
        p.pos = p.pos + 1
        if c == 44 {  }
        else if c == 125 { return Ok(VObj(ks, vs)) }
        else { return Err("json: expected comma or close-brace") }
    }
    return Err("json: unreachable")
}

def _vp_value(&!_VP p) -> Result(Value, Str) {
    _vp_ws(&!p)
    if p.pos >= p.len { return Err("json: unexpected end of input") }
    int c = p.s.byte_at!(p.pos)
    if c == 123 { return _vp_object(&!p) }
    if c == 91 { return _vp_array(&!p) }
    if c == 34 {
        match _vp_string(&!p) {
            Ok(str) => { return Ok(VStr(str)) }
            Err(e) => { return Err(e) }
        }
    }
    if c == 116 {
        if p.pos + 4 <= p.len && p.s.byte_at!(p.pos + 1) == 114 && p.s.byte_at!(p.pos + 2) == 117 && p.s.byte_at!(p.pos + 3) == 101 {
            p.pos = p.pos + 4
            return Ok(VBool(true))
        }
        return Err("json: invalid literal")
    }
    if c == 102 {
        if p.pos + 5 <= p.len && p.s.byte_at!(p.pos + 1) == 97 && p.s.byte_at!(p.pos + 2) == 108 && p.s.byte_at!(p.pos + 3) == 115 && p.s.byte_at!(p.pos + 4) == 101 {
            p.pos = p.pos + 5
            return Ok(VBool(false))
        }
        return Err("json: invalid literal")
    }
    if c == 110 {
        if p.pos + 4 <= p.len && p.s.byte_at!(p.pos + 1) == 117 && p.s.byte_at!(p.pos + 2) == 108 && p.s.byte_at!(p.pos + 3) == 108 {
            p.pos = p.pos + 4
            return Ok(VNull)
        }
        return Err("json: invalid literal")
    }
    return _vp_number(&!p)
}

def from_json(Str input) -> Result(Value, Str) {
    int n = input.len()
    _VP p = _VP { s: input, pos: 0, len: n }
    match _vp_value(&!p) {
        Ok(v) => {
            _vp_ws(&!p)
            if p.pos < p.len { return Err("json: unexpected trailing content") }
            return Ok(v)
        }
        Err(e) => { return Err(e) }
    }
}
