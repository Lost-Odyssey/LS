// std/json.ls — JSON parser and serializer for LS.
// Pure LS implementation; recursive-descent parser + recursive serializer.

import io

// ---- Core type ----

enum JsonValue {
    Null
    Bool(bool val)
    Number(f64 val)
    Str(string val)
    Array(vec(JsonValue) items)
    Object(vec(string) keys, map(string, JsonValue) entries)
}

// ---- JsonValue impl: methods ----

impl JsonValue {
    // -- Static constructors --
    static fn null_val() -> JsonValue { return Null }
    static fn bool_val(bool b) -> JsonValue { return Bool(b) }
    static fn num_val(f64 n) -> JsonValue { return Number(n) }
    static fn int_val(int n) -> JsonValue { return Number(n as f64) }
    static fn str_val(string s) -> JsonValue { return Str(s) }
    static fn array_new() -> JsonValue {
        vec(JsonValue) v = []
        return Array(v)
    }
    static fn object_new() -> JsonValue {
        vec(string) ks = []
        map(string, JsonValue) m = {}
        return Object(ks, m)
    }

    // -- &self predicates --
    fn is_null(&self) -> bool {
        match self { Null => true, _ => false, }
    }
    fn is_bool(&self) -> bool {
        match self { Bool(_) => true, _ => false, }
    }
    fn is_number(&self) -> bool {
        match self { Number(_) => true, _ => false, }
    }
    fn is_str(&self) -> bool {
        match self { Str(_) => true, _ => false, }
    }
    fn is_array(&self) -> bool {
        match self { Array(_) => true, _ => false, }
    }
    fn is_object(&self) -> bool {
        match self { Object(k, _) => true, _ => false, }
    }

    // -- &self accessors (primitives only; string/enum via match) --
    fn get_number(&self) -> f64 {
        match self { Number(v) => v, _ => 0.0, }
    }
    fn get_bool(&self) -> bool {
        match self { Bool(v) => v, _ => false, }
    }

    // -- &self navigation --
    fn array_len(&self) -> int {
        match self {
            Array(items) => { return items.length }
            _ => { return 0 - 1 }
        }
    }
    fn object_len(&self) -> int {
        match self {
            Object(ks, _) => { return ks.length }
            _ => { return 0 - 1 }
        }
    }
    fn object_has(&self, string key) -> bool {
        match self {
            Object(_, entries) => { return entries.contains_key(key) }
            _ => { return false }
        }
    }

    // -- &!self mutation (COPY+REPLACE pattern) --

    // Append an element to an Array variant in place.
    fn push(&!self, JsonValue item) {
        match self {
            Array(items) => {
                vec(JsonValue) nv = items.copy()
                nv.push(item)
                self = Array(nv)
            }
            _ => {}
        }
    }

    // Insert or update a key in an Object variant in place.
    // 'key' must be an owned string (call key.copy() if passing a literal).
    fn set(&!self, string key, JsonValue val) {
        match self {
            Object(keys, entries) => {
                vec(string) nks = keys.copy()
                map(string, JsonValue) nem = entries.copy()
                string k = key.copy()   // cap=0 param -> owned copy
                if (!nem.contains_key(k)) {
                    nks.push(k.copy())  // k still valid after push (push takes .copy() result)
                }
                nem.set(k, val)         // map clones key+val internally
                self = Object(nks, nem)
            }
            _ => {}
        }
    }
}

// ---- Convenience constructors ----

fn null_val() -> JsonValue {
    return Null
}

fn bool_val(bool b) -> JsonValue {
    return Bool(b)
}

fn number(f64 n) -> JsonValue {
    return Number(n)
}

fn number_int(int n) -> JsonValue {
    return Number(n as f64)
}

fn str_val(string s) -> JsonValue {
    return Str(s)
}

fn array_new() -> JsonValue {
    vec(JsonValue) items = []
    return Array(items)
}

fn object_new() -> JsonValue {
    vec(string) ks = []
    map(string, JsonValue) m = {}
    return Object(ks, m)
}

// ---- Type predicates ----

fn is_null(JsonValue v) -> bool {
    match v {
        Null => { return true }
        _ => { return false }
    }
}

fn is_bool(JsonValue v) -> bool {
    match v {
        Bool(b) => { return true }
        _ => { return false }
    }
}

fn is_number(JsonValue v) -> bool {
    match v {
        Number(n) => { return true }
        _ => { return false }
    }
}

fn is_string(JsonValue v) -> bool {
    match v {
        Str(s) => { return true }
        _ => { return false }
    }
}

fn is_array(JsonValue v) -> bool {
    match v {
        Array(a) => { return true }
        _ => { return false }
    }
}

fn is_object(JsonValue v) -> bool {
    match v {
        Object(ks, o) => { return true }
        _ => { return false }
    }
}

// ---- Value extraction ----

fn as_bool(JsonValue v) -> Result(bool, string) {
    match v {
        Bool(b) => { return Ok(b) }
        _ => { return Err("json: expected Bool") }
    }
}

fn as_number(JsonValue v) -> Result(f64, string) {
    match v {
        Number(n) => { return Ok(n) }
        _ => { return Err("json: expected Number") }
    }
}

fn as_string(JsonValue v) -> Result(string, string) {
    match v {
        Str(s) => { return Ok(s) }
        _ => { return Err("json: expected Str") }
    }
}

fn as_int(JsonValue v) -> Result(int, string) {
    match v {
        Number(n) => { return Ok(n as int) }
        _ => { return Err("json: expected Number") }
    }
}

// ---- Parser ----

// Internal parser state: input string + cursor position.
// Since LS doesn't support mutable struct borrow for enum,
// we use a struct to thread state through the recursive descent.

struct JParser {
    string input
    int pos
    int len
}

fn _new_parser(string input) -> JParser {
    JParser p = JParser { input: input, pos: 0, len: input.length }
    return p
}

// Skip whitespace (space, tab, \n, \r)
fn _skip_ws(&!JParser p) {
    while p.pos < p.len {
        int ch = p.input.at_unsafe(p.pos)
        if ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' {
            p.pos = p.pos + 1
        } else {
            return
        }
    }
}

// Peek current char (returns -1 at end)
fn _peek(&!JParser p) -> int {
    if p.pos >= p.len { return 0 - 1 }
    return p.input.at_unsafe(p.pos)
}

// Consume one char and advance
fn _advance(&!JParser p) -> int {
    if p.pos >= p.len { return 0 - 1 }
    int ch = p.input.at_unsafe(p.pos)
    p.pos = p.pos + 1
    return ch
}

// Expect a specific char, or return error
fn _expect(&!JParser p, int expected) -> Result(int, string) {
    int ch = _advance(&!p)
    if ch != expected {
        return Err(f"json: expected '{expected}' at position {p.pos}")
    }
    return Ok(0)
}

// ---- Recursive descent parser ----
// Note: mutual recursion between _parse_value, _parse_array, _parse_object
// works because LS codegen does forward-declare pass before body compilation.

// Advance p.pos past all plain chars (not '"' or '\'), return count skipped.
// Lets _parse_string_raw append a whole segment in one substr call.
fn _scan_plain(&!JParser p) -> int {
    int start = p.pos
    while p.pos < p.len {
        int c = p.input.at_unsafe(p.pos)
        if c == '"' || c == '\\' { return p.pos - start }
        p.pos = p.pos + 1
    }
    return p.pos - start
}

fn _parse_string_raw(&!JParser p) -> Result(string, string) {
    int ch = _advance(&!p)
    if ch != '"' { return Err(f"json: expected '\"' at position {p.pos}") }

    string result = ""
    while p.pos < p.len {
        // Bulk-copy plain chars in one substr call — no per-char overhead
        int seg_start = p.pos
        int seg_len   = _scan_plain(&!p)
        if seg_len > 0 {
            result.append(p.input.substr(seg_start, seg_len))
        }
        // p.pos is now at '"', '\\', or p.len
        ch = _advance(&!p)
        if ch == '"' { return Ok(result) }
        if ch == '\\' {
            int next = _advance(&!p)
            match next {
                '"'  => { result.append('"') }
                '/'  => { result.append('/') }
                '\\' => { result.append('\\') }
                'n'  => { result.append('\n') }
                'r'  => { result.append('\r') }
                't'  => { result.append('\t') }
                'b'  => { result.append(8) }   // backspace U+0008
                'f'  => { result.append(12) }  // form feed U+000C
                'u'  => {
                    if p.pos + 4 <= p.len {
                        string hex = p.input.substr(p.pos, 4)
                        result.append("\\u")
                        result.append(hex)
                        p.pos = p.pos + 4
                    } else {
                        return Err(f"json: incomplete \\u escape at position {p.pos}")
                    }
                }
                _ => { return Err(f"json: unknown escape at position {p.pos}") }
            }
        }
        // ch == -1 (end of input): while condition fails next iteration
    }
    return Err("json: unterminated string")
}

fn _is_digit(int ch) -> bool {
    return ch >= '0' && ch <= '9'
}

fn _parse_number(&!JParser p) -> Result(JsonValue, string) {
    int start = p.pos
    // Optional leading minus
    if _peek(&!p) == '-' { p.pos = p.pos + 1 }
    // Integer part (inline at() — p.pos < p.len already checked by while)
    if !_is_digit(_peek(&!p)) {
        return Err(f"json: expected digit at position {p.pos}")
    }
    while p.pos < p.len && _is_digit(p.input.at_unsafe(p.pos)) {
        p.pos = p.pos + 1
    }
    // Optional fraction
    if p.pos < p.len && p.input.at_unsafe(p.pos) == '.' {
        p.pos = p.pos + 1
        if !_is_digit(_peek(&!p)) {
            return Err(f"json: expected digit after '.' at position {p.pos}")
        }
        while p.pos < p.len && _is_digit(p.input.at_unsafe(p.pos)) {
            p.pos = p.pos + 1
        }
    }
    // Optional exponent
    if p.pos < p.len {
        int ec = p.input.at_unsafe(p.pos)
        if ec == 'e' || ec == 'E' {
            p.pos = p.pos + 1
            int sc = _peek(&!p)
            if sc == '+' || sc == '-' { p.pos = p.pos + 1 }
            if !_is_digit(_peek(&!p)) {
                return Err(f"json: expected digit in exponent at position {p.pos}")
            }
            while p.pos < p.len && _is_digit(p.input.at_unsafe(p.pos)) {
                p.pos = p.pos + 1
            }
        }
    }
    string num_str = p.input.substr(start, p.pos - start)
    Result(f64, string) conv = num_str.to_float()
    match conv {
        Ok(val) => { return Ok(Number(val)) }
        Err(e) => { return Err(f"json: invalid number '{num_str}' at position {start}") }
    }
}

fn _parse_array(&!JParser p) -> Result(JsonValue, string) {
    // '[' already peeked, consume it
    p.pos = p.pos + 1
    _skip_ws(&!p)

    vec(JsonValue) items = []

    // Empty array
    if _peek(&!p) == ']' {
        p.pos = p.pos + 1
        return Ok(Array(items))
    }

    // First element
    Result(JsonValue, string) first = _parse_value(&!p)
    match first {
        Err(e) => { return Err(e) }
        Ok(v) => { items.push(v) }
    }

    _skip_ws(&!p)
    while _peek(&!p) == ',' {
        p.pos = p.pos + 1
        _skip_ws(&!p)
        Result(JsonValue, string) elem = _parse_value(&!p)
        match elem {
            Err(e) => { return Err(e) }
            Ok(v) => { items.push(v) }
        }
        _skip_ws(&!p)
    }

    if _peek(&!p) != ']' {
        return Err(f"json: expected ']' at position {p.pos}")
    }
    p.pos = p.pos + 1
    return Ok(Array(items))
}

fn _parse_object(&!JParser p) -> Result(JsonValue, string) {
    // '{' already peeked, consume it
    p.pos = p.pos + 1
    _skip_ws(&!p)

    vec(string) ks = []
    map(string, JsonValue) entries = {}

    // Empty object
    if _peek(&!p) == '}' {
        p.pos = p.pos + 1
        return Ok(Object(ks, entries))
    }

    // Parse key-value pairs
    // First pair
    _skip_ws(&!p)
    Result(string, string) k1 = _parse_string_raw(&!p)
    match k1 {
        Err(e) => { return Err(e) }
        Ok(key) => {
            _skip_ws(&!p)
            if _peek(&!p) != ':' {
                return Err(f"json: expected ':' at position {p.pos}")
            }
            p.pos = p.pos + 1
            _skip_ws(&!p)
            Result(JsonValue, string) val_r = _parse_value(&!p)
            match val_r {
                Err(e) => { return Err(e) }
                Ok(val) => {
                    entries.set(key.copy(), val)
                    ks.push(key)
                }
            }
        }
    }

    _skip_ws(&!p)
    while _peek(&!p) == ',' {
        p.pos = p.pos + 1
        _skip_ws(&!p)
        Result(string, string) kn = _parse_string_raw(&!p)
        match kn {
            Err(e) => { return Err(e) }
            Ok(key) => {
                _skip_ws(&!p)
                if _peek(&!p) != ':' {
                    return Err(f"json: expected ':' at position {p.pos}")
                }
                p.pos = p.pos + 1
                _skip_ws(&!p)
                Result(JsonValue, string) val_r = _parse_value(&!p)
                match val_r {
                    Err(e) => { return Err(e) }
                    Ok(val) => {
                        entries.set(key.copy(), val)
                        ks.push(key)
                    }
                }
            }
        }
        _skip_ws(&!p)
    }

    if _peek(&!p) != '}' {
        return Err(f"json: expected '}}' at position {p.pos}")
    }
    p.pos = p.pos + 1
    return Ok(Object(ks, entries))
}

fn _match4(&!JParser p, int c0, int c1, int c2, int c3) -> bool {
    if p.pos + 4 > p.len { return false }
    return p.input.at_unsafe(p.pos)   == c0 &&
           p.input.at_unsafe(p.pos+1) == c1 &&
           p.input.at_unsafe(p.pos+2) == c2 &&
           p.input.at_unsafe(p.pos+3) == c3
}

fn _match5(&!JParser p, int c0, int c1, int c2, int c3, int c4) -> bool {
    if p.pos + 5 > p.len { return false }
    return p.input.at_unsafe(p.pos)   == c0 &&
           p.input.at_unsafe(p.pos+1) == c1 &&
           p.input.at_unsafe(p.pos+2) == c2 &&
           p.input.at_unsafe(p.pos+3) == c3 &&
           p.input.at_unsafe(p.pos+4) == c4
}

fn _parse_value(&!JParser p) -> Result(JsonValue, string) {
    _skip_ws(&!p)
    if p.pos >= p.len {
        return Err("json: unexpected end of input")
    }
    int ch = _peek(&!p)

    match ch {
        '"' => {
            Result(string, string) sr = _parse_string_raw(&!p)
            match sr {
                Err(e) => { return Err(e) }
                Ok(s)  => { return Ok(Str(s)) }
            }
        }
        '-' | '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9' => {
            return _parse_number(&!p)
        }
        't' => {
            if _match4(&!p, 't', 'r', 'u', 'e') {
                p.pos = p.pos + 4
                return Ok(Bool(true))
            }
            return Err(f"json: unexpected token at position {p.pos}")
        }
        'f' => {
            if _match5(&!p, 'f', 'a', 'l', 's', 'e') {
                p.pos = p.pos + 5
                return Ok(Bool(false))
            }
            return Err(f"json: unexpected token at position {p.pos}")
        }
        'n' => {
            if _match4(&!p, 'n', 'u', 'l', 'l') {
                p.pos = p.pos + 4
                return Ok(Null)
            }
            return Err(f"json: unexpected token at position {p.pos}")
        }
        '[' => { return _parse_array(&!p) }
        '{' => { return _parse_object(&!p) }
        _   => { return Err(f"json: unexpected character at position {p.pos}") }
    }
}

// ---- Public parse API ----

fn parse(string input) -> Result(JsonValue, string) {
    JParser p = _new_parser(input)
    Result(JsonValue, string) result = _parse_value(&!p)
    match result {
        Err(e) => { return Err(e) }
        Ok(v) => {
            // Check for trailing non-whitespace
            _skip_ws(&!p)
            if p.pos < p.len {
                return Err(f"json: unexpected trailing content at position {p.pos}")
            }
            return Ok(v)
        }
    }
}

// ---- Serializer (stringify) ----

fn _escape_string(string s) -> string {
    int n = s.length
    int i = 0
    // Fast-path: scan for first char that needs escaping.
    // Most JSON keys/values are plain ASCII — avoid per-char match entirely.
    while i < n {
        int ch = s.at_unsafe(i)
        if ch == '"' || ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t' || ch == 8 || ch == 12 {
            // Slow path: copy clean prefix, then escape from i onward.
            string result = s.substr(0, i)
            while i < n {
                ch = s.at_unsafe(i)
                match ch {
                    '"'  => { result.append("\\\"") }
                    '\\' => { result.append("\\\\") }
                    '\n' => { result.append("\\n") }
                    '\r' => { result.append("\\r") }
                    '\t' => { result.append("\\t") }
                    8    => { result.append("\\b") }
                    12   => { result.append("\\f") }
                    _    => { result.append(ch) }
                }
                i = i + 1
            }
            return result
        }
        i = i + 1
    }
    // No escaping needed — return a direct copy.
    return s.copy()
}

fn _indent_str(int depth, int indent) -> string {
    int total = depth * indent
    if total <= 0 { return "" }
    string s = ""
    int i = 0
    while i < total {
        s.append(" ")
        i = i + 1
    }
    return s
}

fn _format_number(f64 n) -> string {
    // Check if it's an integer value (no fractional part)
    int i = n as int
    f64 check = i as f64
    if check == n && n >= (0 - 2000000000) as f64 && n <= 2000000000 as f64 {
        return f"{i}"
    }
    return f"{n}"
}

fn _stringify_impl(&JsonValue val, int depth, int indent) -> string {
    match val {
        Null => { return "null" }
        Bool(b) => {
            if b { return "true" }
            return "false"
        }
        Number(n) => {
            return _format_number(n)
        }
        Str(s) => {
            string r = "\""
            r.append(_escape_string(s))
            r.append('"')
            return r
        }
        Array(items) => {
            if items.length == 0 { return "[]" }
            if indent <= 0 {
                // Compact mode
                string result = "["
                int i = 0
                while i < items.length {
                    if i > 0 { result.append(",") }
                    result.append(_stringify_impl(items[i], depth, 0))
                    i = i + 1
                }
                result.append("]")
                return result
            } else {
                // Pretty mode
                string result = "[\n"
                string child_pad = _indent_str(depth + 1, indent)
                string close_pad = _indent_str(depth, indent)
                int i = 0
                while i < items.length {
                    if i > 0 { result.append(",\n") }
                    result.append(child_pad)
                    result.append(_stringify_impl(items[i], depth + 1, indent))
                    i = i + 1
                }
                result.append("\n")
                result.append(close_pad)
                result.append("]")
                return result
            }
        }
        Object(ks, entries) => {
            if ks.length == 0 { return "{}" }
            if indent <= 0 {
                // Compact mode
                string result = "{"
                int i = 0
                while i < ks.length {
                    if i > 0 { result.append(",") }
                    string key = ks[i]   // cache: avoid two separate vec clones
                    result.append("\"")
                    result.append(_escape_string(key))
                    result.append("\":")
                    result.append(_stringify_impl(entries.get(key), depth, 0))
                    i = i + 1
                }
                result.append("}")
                return result
            } else {
                // Pretty mode
                string result = "{\n"
                string child_pad = _indent_str(depth + 1, indent)
                string close_pad = _indent_str(depth, indent)
                int i = 0
                while i < ks.length {
                    if i > 0 { result.append(",\n") }
                    string key = ks[i]   // cache: avoid two separate vec clones
                    result.append(child_pad)
                    result.append("\"")
                    result.append(_escape_string(key))
                    result.append("\": ")
                    result.append(_stringify_impl(entries.get(key), depth + 1, indent))
                    i = i + 1
                }
                result.append("\n")
                result.append(close_pad)
                result.append("}")
                return result
            }
        }
    }
}

// ---- Public stringify API ----

fn stringify(JsonValue val) -> string {
    return _stringify_impl(val, 0, 0)
}

fn stringify_pretty(JsonValue val, int indent) -> string {
    return _stringify_impl(val, 0, indent)
}

// ---- Navigation API (read-only) ----

// Return the number of elements in an Array, or -1 if not an array.
fn array_len(JsonValue v) -> int {
    match v {
        Array(items) => { return items.length }
        _ => { return 0 - 1 }
    }
}

// Return the number of keys in an Object, or -1 if not an object.
fn object_len(JsonValue v) -> int {
    match v {
        Object(ks, entries) => { return ks.length }
        _ => { return 0 - 1 }
    }
}

// Return true if v is an Object that contains the given key.
fn object_has(JsonValue v, string key) -> bool {
    match v {
        Object(ks, entries) => { return entries.contains_key(key) }
        _ => { return false }
    }
}

// Return an owned copy of the key list of an Object (preserving insertion order),
// or an empty vec if v is not an object.
// Note: we deep-copy each key string so the returned vec owns independent data
// (the binder `ks` shares memory with the JsonValue payload which is dropped on
// function return — Phase H / has_drop binder copy limitation).
fn object_keys(JsonValue v) -> vec(string) {
    match v {
        Object(ks, entries) => {
            vec(string) result = []
            int i = 0
            while i < ks.length {
                result.push(ks[i].copy())
                i = i + 1
            }
            return result
        }
        _ => {
            vec(string) empty = []
            return empty
        }
    }
}

// ---- File I/O convenience wrappers ----
// Note: names avoid conflict with io.ls (LLVM has no module-level name mangling).

fn load_file(string path) -> Result(JsonValue, string) {
    string content = try io.read_file(path)
    return parse(content)
}

fn save_file(string path, JsonValue val) -> Result(int, string) {
    string content = stringify_pretty(val, 2)
    return io.write_file(path, content)
}

fn save_compact(string path, JsonValue val) -> Result(int, string) {
    string content = stringify(val)
    return io.write_file(path, content)
}
