// model/lsm.ls — .lsm model description language: lexer + parser + IR + validator.
//
// .lsm is a DATA format, not an LS language feature (compiler untouched).
// Spec: docs/spec_lsm.md. See tests/samples for example .lsm models.
//
// Contract: shapes are resolved offline by the toolchain; LS only VALIDATES,
// never infers. Every value carries `: dtype[dims]`.
//
// v1 keeps everything in one module to avoid cross-module enum/struct friction;
// split into lex/parse/check later if it grows.

import std.core.vec
import std.core.str

// ---- Token kinds ----
int TOK_IDENT  = 1
int TOK_INT    = 2
int TOK_FLOAT  = 3
int TOK_STRING = 4
int TOK_PUNCT  = 5
int TOK_NL     = 6
int TOK_EOF    = 7

// ---- Declaration kinds ----
int KIND_IN    = 0
int KIND_OUT   = 1
int KIND_PARAM = 2
int KIND_GROUP = 3

// ---- Token ----
struct Token {
    int kind
    Str text     // IDENT/FLOAT/STRING lexeme; "" otherwise
    int ival     // PUNCT: char code; INT: parsed value
    int line
}

// ---- IR ----
struct TypeInfo {
    Str dtype
    Vec(int) dims     // -1 = dynamic '?'
}

struct Decl {
    int kind          // KIND_IN/OUT/PARAM/GROUP
    Str name
    TypeInfo ty
    Str layout        // "" if none
    Vec(Str) members  // group members; empty otherwise
}

struct Op {
    Str dst           // output name (single output in v1)
    Str kind          // op name: gemm/relu/...
    Vec(Str) ins
    Vec(Str) attr_keys
    Vec(Str) attr_vals
    TypeInfo ty       // output type
    Vec(Str) annos    // canonical annotation text, e.g. "inplace(n0)"
}

struct LsModel {
    Str name
    Vec(Decl) decls   // in/out/param/group, in order
    Vec(Op) ops       // bindings, in order
}

// ---- Char classes ----
def is_digit(int c) -> bool { return c >= '0' && c <= '9' }
def is_alpha(int c) -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'
}
def is_alnum(int c) -> bool { return is_alpha(c) || is_digit(c) }
def is_punct(int c) -> bool {
    return c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']'
        || c == ':' || c == ',' || c == '=' || c == '@' || c == '?'
}

def is_dtype(&Str s) -> bool {
    return s.eq?("f16") || s.eq?("bf16") || s.eq?("f32") || s.eq?("f64")
        || s.eq?("i8") || s.eq?("i16") || s.eq?("i32") || s.eq?("i64")
        || s.eq?("u8") || s.eq?("u16") || s.eq?("u32") || s.eq?("u64")
        || s.eq?("bool")
}

def is_known_op(&Str k) -> bool {
    // Tier A (core CNN/MLP)
    if k.eq?("gemm") || k.eq?("matmul") || k.eq?("conv1d") || k.eq?("conv2d")
        || k.eq?("batchnorm") || k.eq?("relu") || k.eq?("sigmoid")
        || k.eq?("tanh") || k.eq?("silu") || k.eq?("gelu") || k.eq?("leakyrelu")
        || k.eq?("add") || k.eq?("sub") || k.eq?("mul") || k.eq?("div")
        || k.eq?("reshape") || k.eq?("transpose") || k.eq?("concat") { return true }
    // Tier B (pooling / normalization / more activations)
    if k.eq?("softmax") || k.eq?("layernorm") || k.eq?("pool")
        || k.eq?("maxpool") || k.eq?("avgpool") || k.eq?("gap")
        || k.eq?("clip") || k.eq?("erf") { return true }
    // Tier C (Transformer building blocks)
    if k.eq?("attention") || k.eq?("split") || k.eq?("slice") || k.eq?("gather")
        || k.eq?("cast") || k.eq?("pow") || k.eq?("reducemean") || k.eq?("embedding") { return true }
    return false
}

def punct_str(int ch) -> Str {
    Str s = ""
    s.push_byte(ch)
    return s
}

// ---- Lexer ----
// Single pass, local cursor (no recursion → no state struct needed).
def tokenize(Str src) -> Vec(Token) {
    Vec(Token) toks = {}
    int n = src.len()
    int i = 0
    int line = 1
    while i < n {
        int c = src.byte_at!(i)
        if c == ' ' || c == '\t' || c == '\r' {
            i = i + 1
        } else if c == '\n' {
            Token t = Token { kind: TOK_NL, text: "", ival: 0, line: line }
            toks.push(t)
            line = line + 1
            i = i + 1
        } else if c == '#' {
            while i < n && src.byte_at!(i) != '\n' { i = i + 1 }
        } else if is_alpha(c) {
            int s = i
            while i < n && is_alnum(src.byte_at!(i)) { i = i + 1 }
            Str lex = src.substr(s, i - s)
            Token t = Token { kind: TOK_IDENT, text: lex, ival: 0, line: line }
            toks.push(t)
        } else if is_digit(c) || (c == '-' && i + 1 < n && is_digit(src.byte_at!(i + 1))) {
            int s = i
            if src.byte_at!(i) == '-' { i = i + 1 }
            while i < n && is_digit(src.byte_at!(i)) { i = i + 1 }
            bool isf = false
            if i < n && src.byte_at!(i) == '.' {
                isf = true
                i = i + 1
                while i < n && is_digit(src.byte_at!(i)) { i = i + 1 }
            }
            if i < n && (src.byte_at!(i) == 'e' || src.byte_at!(i) == 'E') {
                isf = true
                i = i + 1
                if i < n && (src.byte_at!(i) == '+' || src.byte_at!(i) == '-') { i = i + 1 }
                while i < n && is_digit(src.byte_at!(i)) { i = i + 1 }
            }
            Str lex = src.substr(s, i - s)
            if isf {
                Token t = Token { kind: TOK_FLOAT, text: lex, ival: 0, line: line }
                toks.push(t)
            } else {
                int v = 0
                match lex.to_int() {
                    Ok(x) => { v = x }
                    Err(e) => { v = 0 }
                }
                Token t = Token { kind: TOK_INT, text: lex, ival: v, line: line }
                toks.push(t)
            }
        } else if c == '"' {
            i = i + 1
            int s = i
            while i < n && src.byte_at!(i) != '"' { i = i + 1 }
            Str lex = src.substr(s, i - s)
            if i < n { i = i + 1 }
            Token t = Token { kind: TOK_STRING, text: lex, ival: 0, line: line }
            toks.push(t)
        } else if is_punct(c) {
            Token t = Token { kind: TOK_PUNCT, text: "", ival: c, line: line }
            toks.push(t)
            i = i + 1
        } else {
            // unknown char: emit as PUNCT so parser reports "unexpected token"
            Token t = Token { kind: TOK_PUNCT, text: "", ival: c, line: line }
            toks.push(t)
            i = i + 1
        }
    }
    Token eof = Token { kind: TOK_EOF, text: "", ival: 0, line: line }
    toks.push(eof)
    return toks
}

// ---- Parser state ----
struct PState {
    Vec(Token) toks
    int pos
    bool ok
    Str err
}

def p_kind(&!PState p) -> int {
    if p.pos >= p.toks.len() { return TOK_EOF }
    Token t = p.toks[p.pos]
    return t.kind
}
def p_ival(&!PState p) -> int {
    if p.pos >= p.toks.len() { return 0 }
    Token t = p.toks[p.pos]
    return t.ival
}
def p_text(&!PState p) -> Str {
    if p.pos >= p.toks.len() { return "" }
    Token t = p.toks[p.pos]
    return t.text.copy()
}
def p_line(&!PState p) -> int {
    if p.pos >= p.toks.len() { return 0 }
    Token t = p.toks[p.pos]
    return t.line
}
def p_adv(&!PState p) { p.pos = p.pos + 1 }

def fail(&!PState p, Str msg) {
    if p.ok {
        p.ok = false
        p.err = msg
    }
}

def is_close(&!PState p) -> bool {
    return p_kind(&!p) == TOK_PUNCT && p_ival(&!p) == '}'
}
def is_punct_tok(&!PState p, int ch) -> bool {
    return p_kind(&!p) == TOK_PUNCT && p_ival(&!p) == ch
}

def skip_nl(&!PState p) {
    while p_kind(&!p) == TOK_NL { p_adv(&!p) }
}

def eat_punct(&!PState p, int ch) -> bool {
    if !p.ok { return false }
    if is_punct_tok(&!p, ch) {
        p_adv(&!p)
        return true
    }
    fail(&!p, f"expected '{punct_str(ch)}' at line {p_line(&!p)}")
    return false
}

def parse_name(&!PState p) -> Str {
    if p.ok && p_kind(&!p) == TOK_IDENT {
        Str s = p_text(&!p)
        p_adv(&!p)
        return s
    }
    fail(&!p, f"expected name at line {p_line(&!p)}")
    return ""
}

def parse_dim(&!PState p, &!Vec(int) dims) {
    if p_kind(&!p) == TOK_INT {
        dims.push(p_ival(&!p))
        p_adv(&!p)
    } else if is_punct_tok(&!p, '?') {
        dims.push(0 - 1)
        p_adv(&!p)
    } else {
        fail(&!p, f"expected dim at line {p_line(&!p)}")
    }
}

def parse_type(&!PState p) -> TypeInfo {
    Str dt = ""
    if p.ok && p_kind(&!p) == TOK_IDENT {
        dt = p_text(&!p)
        p_adv(&!p)
    } else {
        fail(&!p, f"expected dtype at line {p_line(&!p)}")
    }
    if p.ok && !is_dtype(dt) {
        fail(&!p, f"unknown dtype '{dt}' at line {p_line(&!p)}")
    }
    Vec(int) dims = {}
    eat_punct(&!p, '[')
    if p.ok && !is_punct_tok(&!p, ']') {
        parse_dim(&!p, &!dims)
        while p.ok && is_punct_tok(&!p, ',') {
            p_adv(&!p)
            parse_dim(&!p, &!dims)
        }
    }
    eat_punct(&!p, ']')
    TypeInfo ty = TypeInfo { dtype: dt, dims: dims }
    return ty
}

// Parse a value and return its canonical text (for attr values & reprint).
def parse_value_text(&!PState p) -> Str {
    int k = p_kind(&!p)
    if k == TOK_INT || k == TOK_FLOAT || k == TOK_IDENT {
        Str s = p_text(&!p)
        p_adv(&!p)
        return s
    }
    if k == TOK_STRING {
        Str s = ""
        s.push_byte('"')
        s.push_str(p_text(&!p))
        s.push_byte('"')
        p_adv(&!p)
        return s
    }
    if is_punct_tok(&!p, '[') {
        Str s = ""
        s.push_byte('[')
        p_adv(&!p)
        bool first = true
        while p.ok && !is_punct_tok(&!p, ']') {
            if !first { s.push_str(",") }
            first = false
            Str e = parse_value_text(&!p)
            s.push_str(e)
            if is_punct_tok(&!p, ',') { p_adv(&!p) }
        }
        eat_punct(&!p, ']')
        s.push_byte(']')
        return s
    }
    fail(&!p, f"expected value at line {p_line(&!p)}")
    return ""
}

// Optional trailing decl attrs; v1 only extracts `layout=...`, ignores others.
def parse_opt_layout(&!PState p) -> Str {
    Str layout = ""
    while p.ok && p_kind(&!p) == TOK_IDENT {
        Str key = p_text(&!p)
        p_adv(&!p)
        eat_punct(&!p, '=')
        Str val = parse_value_text(&!p)
        if key.eq?("layout") { layout = val }
    }
    return layout
}

def parse_io_decl(&!PState p, int kind) -> Decl {
    Str name = parse_name(&!p)
    eat_punct(&!p, ':')
    TypeInfo ty = parse_type(&!p)
    Str layout = parse_opt_layout(&!p)
    Vec(Str) members = {}
    Decl d = Decl { kind: kind, name: name, ty: ty, layout: layout, members: members }
    return d
}

def parse_param_decl(&!PState p) -> Decl {
    Str name = parse_name(&!p)
    eat_punct(&!p, ':')
    Vec(Str) members = {}
    if is_punct_tok(&!p, '{') {
        p_adv(&!p)
        if p_kind(&!p) == TOK_IDENT {
            members.push(p_text(&!p))
            p_adv(&!p)
        } else {
            fail(&!p, f"expected member name at line {p_line(&!p)}")
        }
        while p.ok && is_punct_tok(&!p, ',') {
            p_adv(&!p)
            if p_kind(&!p) == TOK_IDENT {
                members.push(p_text(&!p))
                p_adv(&!p)
            } else {
                fail(&!p, f"expected member name at line {p_line(&!p)}")
            }
        }
        eat_punct(&!p, '}')
        eat_punct(&!p, ':')
        TypeInfo ty = parse_type(&!p)
        Str layout = parse_opt_layout(&!p)
        Decl d = Decl { kind: KIND_GROUP, name: name, ty: ty, layout: layout, members: members }
        return d
    }
    TypeInfo ty = parse_type(&!p)
    Str layout = parse_opt_layout(&!p)
    Decl d = Decl { kind: KIND_PARAM, name: name, ty: ty, layout: layout, members: members }
    return d
}

def parse_anno(&!PState p) -> Str {
    Str name = ""
    if p.ok && p_kind(&!p) == TOK_IDENT {
        name = p_text(&!p)
        p_adv(&!p)
    } else {
        fail(&!p, f"expected annotation name at line {p_line(&!p)}")
        return ""
    }
    if is_punct_tok(&!p, '(') {
        p_adv(&!p)
        name.push_byte('(')
        bool first = true
        while p.ok && !is_punct_tok(&!p, ')') {
            if !first { name.push_str(",") }
            first = false
            if p_kind(&!p) == TOK_IDENT {
                name.push_str(p_text(&!p))
                p_adv(&!p)
            } else {
                fail(&!p, f"expected annotation arg at line {p_line(&!p)}")
            }
            if is_punct_tok(&!p, ',') { p_adv(&!p) }
        }
        eat_punct(&!p, ')')
        name.push_byte(')')
        return name
    }
    return name
}

def parse_binding(&!PState p) -> Op {
    Str dst = parse_name(&!p)
    if is_punct_tok(&!p, ',') {
        fail(&!p, f"multi-output binding not supported in v1 at line {p_line(&!p)}")
    }
    eat_punct(&!p, '=')
    Str opname = parse_name(&!p)
    eat_punct(&!p, '(')
    Vec(Str) ins = {}
    if p.ok && !is_punct_tok(&!p, ')') {
        if p_kind(&!p) == TOK_IDENT {
            ins.push(p_text(&!p))
            p_adv(&!p)
        } else {
            fail(&!p, f"expected arg at line {p_line(&!p)}")
        }
        while p.ok && is_punct_tok(&!p, ',') {
            p_adv(&!p)
            if p_kind(&!p) == TOK_IDENT {
                ins.push(p_text(&!p))
                p_adv(&!p)
            } else {
                fail(&!p, f"expected arg at line {p_line(&!p)}")
            }
        }
    }
    eat_punct(&!p, ')')
    Vec(Str) akeys = {}
    Vec(Str) avals = {}
    while p.ok && p_kind(&!p) == TOK_IDENT {
        Str key = p_text(&!p)
        p_adv(&!p)
        eat_punct(&!p, '=')
        Str val = parse_value_text(&!p)
        akeys.push(key)
        avals.push(val)
    }
    eat_punct(&!p, ':')
    TypeInfo ty = parse_type(&!p)
    Vec(Str) annos = {}
    while p.ok && is_punct_tok(&!p, '@') {
        p_adv(&!p)
        Str a = parse_anno(&!p)
        annos.push(a)
    }
    Op o = Op { dst: dst, kind: opname, ins: ins, attr_keys: akeys, attr_vals: avals, ty: ty, annos: annos }
    return o
}

def parse_stmt(&!PState p, &!Vec(Decl) decls, &!Vec(Op) ops) {
    if !p.ok { return }
    if p_kind(&!p) != TOK_IDENT {
        fail(&!p, f"expected declaration at line {p_line(&!p)}")
        return
    }
    Str w = p_text(&!p)
    if w.eq?("in") {
        p_adv(&!p)
        Decl d = parse_io_decl(&!p, KIND_IN)
        decls.push(d)
    } else if w.eq?("out") {
        p_adv(&!p)
        Decl d = parse_io_decl(&!p, KIND_OUT)
        decls.push(d)
    } else if w.eq?("param") {
        p_adv(&!p)
        Decl d = parse_param_decl(&!p)
        decls.push(d)
    } else {
        Op o = parse_binding(&!p)
        ops.push(o)
    }
}

def parse_program(&!PState p) -> LsModel {
    skip_nl(&!p)
    if !(p_kind(&!p) == TOK_IDENT) {
        fail(&!p, f"expected 'model' at line {p_line(&!p)}")
    }
    Str kw = p_text(&!p)
    if p.ok && !kw.eq?("model") {
        fail(&!p, f"expected 'model' at line {p_line(&!p)}")
    }
    if p.ok { p_adv(&!p) }
    Str name = parse_name(&!p)
    eat_punct(&!p, '{')
    skip_nl(&!p)
    Vec(Decl) decls = {}
    Vec(Op) ops = {}
    while p.ok && !is_close(&!p) && p_kind(&!p) != TOK_EOF {
        int before = p.pos
        parse_stmt(&!p, &!decls, &!ops)
        skip_nl(&!p)
        if p.ok && p.pos == before {
            fail(&!p, f"unexpected token at line {p_line(&!p)}")
        }
    }
    eat_punct(&!p, '}')
    LsModel m = LsModel { name: name, decls: decls, ops: ops }
    return m
}

// ---- Public: parse ----
def parse(Str src) -> Result(LsModel, Str) {
    Vec(Token) toks = tokenize(src)
    PState p = PState { toks: toks, pos: 0, ok: true, err: "" }
    LsModel m = parse_program(&!p)
    if !p.ok {
        return Err(p.err.copy())
    }
    return Ok(m)
}

// ---- Reprint (canonical form) ----
def reprint_type(&TypeInfo ty) -> Str {
    Str s = ty.dtype.copy()
    s.push_byte('[')
    int i = 0
    while i < ty.dims.len() {
        if i > 0 { s.push_str(", ") }
        int d = ty.dims.get!(i)
        if d < 0 {
            s.push_byte('?')
        } else {
            s.push_str(f"{d}")
        }
        i = i + 1
    }
    s.push_byte(']')
    return s
}

def reprint_decl(&Decl d) -> Str {
    Str s = "  "
    if d.kind == KIND_IN {
        s.push_str("in ")
    } else if d.kind == KIND_OUT {
        s.push_str("out ")
    } else {
        s.push_str("param ")
    }
    s.push_str(d.name)
    s.push_str(" : ")
    if d.kind == KIND_GROUP {
        s.push_byte('{')
        int i = 0
        while i < d.members.len() {
            if i > 0 { s.push_str(", ") }
            s.push_str(d.members.get!(i))
            i = i + 1
        }
        s.push_str("} : ")
    }
    s.push_str(reprint_type(&d.ty))
    if d.layout.len() > 0 {
        s.push_str(" layout=")
        s.push_str(d.layout)
    }
    return s
}

def reprint_op(&Op o) -> Str {
    Str s = "  "
    s.push_str(o.dst)
    s.push_str(" = ")
    s.push_str(o.kind)
    s.push_byte('(')
    int i = 0
    while i < o.ins.len() {
        if i > 0 { s.push_str(", ") }
        s.push_str(o.ins.get!(i))
        i = i + 1
    }
    s.push_byte(')')
    int a = 0
    while a < o.attr_keys.len() {
        s.push_byte(' ')
        s.push_str(o.attr_keys.get!(a))
        s.push_byte('=')
        s.push_str(o.attr_vals.get!(a))
        a = a + 1
    }
    s.push_str(" : ")
    s.push_str(reprint_type(&o.ty))
    int nn = 0
    while nn < o.annos.len() {
        s.push_str(" @")
        s.push_str(o.annos.get!(nn))
        nn = nn + 1
    }
    return s
}

def reprint(&LsModel m) -> Str {
    Str s = "model "
    s.push_str(m.name)
    s.push_str(" {\n")
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        s.push_str(reprint_decl(&d))
        s.push_byte('\n')
        i = i + 1
    }
    if m.ops.len() > 0 { s.push_byte('\n') }
    int j = 0
    while j < m.ops.len() {
        Op o = m.ops.get!(j)
        s.push_str(reprint_op(&o))
        s.push_byte('\n')
        j = j + 1
    }
    s.push_str("}\n")
    return s
}

// ---- Validate ----
def check_dims(&TypeInfo ty) -> Result(int, Str) {
    int i = 0
    while i < ty.dims.len() {
        int d = ty.dims.get!(i)
        if d < 0 && i != 0 {
            return Err("dynamic dim '?' only allowed at batch position (dim 0)")
        }
        i = i + 1
    }
    return Ok(0)
}

// Linear name-set membership. Model name sets are tiny (dozens), so O(n^2) is
// fine and a plain Vec(Str) avoids pulling Map into the toolchain.
def name_in(&Vec(Str) names, &Str name) -> bool {
    int i = 0
    while i < names.len() {
        Str e = names.get!(i)
        if e.eq?(name) { return true }
        i = i + 1
    }
    return false
}

def validate(&LsModel m) -> Result(int, Str) {
    Vec(Str) defined = {}

    // Pass 1: declarations. `out` names are validated in pass 3, not defined here.
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        if d.kind == KIND_GROUP {
            int j = 0
            while j < d.members.len() {
                Str mem = d.members.get!(j)
                if name_in(&defined, mem) {
                    return Err(f"duplicate definition '{mem}'")
                }
                defined.push(mem)
                j = j + 1
            }
            Str gn = d.name.copy()
            if name_in(&defined, gn) {
                return Err(f"duplicate definition '{gn}'")
            }
            defined.push(gn)
        } else if d.kind == KIND_IN || d.kind == KIND_PARAM {
            Str dn = d.name.copy()
            if name_in(&defined, dn) {
                return Err(f"duplicate definition '{dn}'")
            }
            match check_dims(&d.ty) {
                Err(e) => { return Err(e) }
                Ok(z) => {}
            }
            defined.push(dn)
        }
        i = i + 1
    }

    // Pass 2: bindings, in topological order.
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        Str opk = o.kind.copy()
        if !is_known_op(opk) {
            return Err(f"unknown op '{opk}'")
        }
        int a = 0
        while a < o.ins.len() {
            Str in_name = o.ins.get!(a)
            if !name_in(&defined, in_name) {
                return Err(f"undefined reference '{in_name}' in op '{o.dst}'")
            }
            a = a + 1
        }
        Str dn = o.dst.copy()
        if name_in(&defined, dn) {
            return Err(f"duplicate definition '{dn}'")
        }
        match check_dims(&o.ty) {
            Err(e) => { return Err(e) }
            Ok(z) => {}
        }
        defined.push(dn)
        k = k + 1
    }

    // Pass 3: every declared output must be produced.
    int q = 0
    while q < m.decls.len() {
        Decl d = m.decls.get!(q)
        if d.kind == KIND_OUT {
            Str on = d.name.copy()
            if !name_in(&defined, on) {
                return Err(f"output '{on}' is never produced")
            }
        }
        q = q + 1
    }

    return Ok(0)
}

// ---- Memory budget / liveness (P2) ----

// Bytes per element for a dtype.
def dtype_bytes(&Str dt) -> int {
    if dt.eq?("f64") || dt.eq?("i64") || dt.eq?("u64") { return 8 }
    if dt.eq?("f32") || dt.eq?("i32") || dt.eq?("u32") { return 4 }
    if dt.eq?("f16") || dt.eq?("bf16") || dt.eq?("i16") || dt.eq?("u16") { return 2 }
    if dt.eq?("i8") || dt.eq?("u8") || dt.eq?("bool") { return 1 }
    return 4
}

// Total bytes of a tensor type. Dynamic dim '?' (-1) is counted as 1 (batch=1).
def tensor_bytes(&TypeInfo ty) -> int {
    int elem = dtype_bytes(&ty.dtype)
    int n = 1
    int i = 0
    while i < ty.dims.len() {
        int d = ty.dims.get!(i)
        if d < 0 { d = 1 }
        n = n * d
        i = i + 1
    }
    return n * elem
}

// Byte size of a Decl/Op tensor field. (Named convenience helpers; the codegen
// leak that once made `&owned_local.ty` clone the field is fixed — a direct
// `tensor_bytes(&d.ty)` on an owned local is now clean too. See
// tests/samples/field_borrow_owned_local_test.ls.)
def decl_bytes(&Decl d) -> int { return tensor_bytes(&d.ty) }
def op_bytes(&Op o) -> int { return tensor_bytes(&o.ty) }

// Extract X from an "inplace(X)" annotation, else "".
def anno_inplace_arg(&Str anno) -> Str {
    if anno.starts_with?("inplace(") {
        int n = anno.len()
        return anno.substr(8, n - 9)
    }
    return ""
}

// Single-core memory budget: params (resident) + scratch (activation liveness
// peak, honouring @inplace) + io; emits Pool.reserve for the hard-RT static pool.
def plan_memory(&LsModel m) -> Str {
    // declared output names (produced by an op but live as model output, not scratch)
    Vec(Str) outs = {}
    int d0 = 0
    while d0 < m.decls.len() {
        Decl d = m.decls.get!(d0)
        if d.kind == KIND_OUT { Str on = d.name.copy(); outs.push(on) }
        d0 = d0 + 1
    }

    // params + io budgets
    int params_bytes = 0
    int param_count = 0
    int io_bytes = 0
    int di = 0
    while di < m.decls.len() {
        Decl d = m.decls.get!(di)
        if d.kind == KIND_PARAM {
            params_bytes = params_bytes + decl_bytes(&d)
            param_count = param_count + 1
        } else if d.kind == KIND_GROUP {
            params_bytes = params_bytes + decl_bytes(&d) * d.members.len()
            param_count = param_count + d.members.len()
        } else if d.kind == KIND_IN || d.kind == KIND_OUT {
            io_bytes = io_bytes + decl_bytes(&d)
        }
        di = di + 1
    }

    // intermediates (op outputs that are not declared outputs) — parallel arrays
    Vec(Str) bn = {}
    Vec(int) bb = {}   // bytes
    Vec(int) bs = {}   // def op index
    Vec(int) bl = {}   // last-use op index
    Vec(int) bv = {}   // 1 = live buffer, 0 = aliased away
    int N = m.ops.len()
    int k = 0
    while k < N {
        Op o = m.ops.get!(k)
        Str dst = o.dst.copy()
        if !name_in(&outs, dst) {
            Str nm = o.dst.copy()
            bn.push(nm)
            bb.push(op_bytes(&o))
            bs.push(k)
            bl.push(k)
            bv.push(1)
        }
        k = k + 1
    }

    // last use of each intermediate
    int j = 0
    while j < N {
        Op o = m.ops.get!(j)
        int a = 0
        while a < o.ins.len() {
            Str in_name = o.ins.get!(a)
            int bi = svg_find_row(&bn, in_name)
            if bi >= 0 {
                if j > bl.get!(bi) { bl.set!(bi, j) }
            }
            a = a + 1
        }
        j = j + 1
    }

    // @inplace: dst aliases src — merge dst's buffer into src's interval
    int k2 = 0
    while k2 < N {
        Op o = m.ops.get!(k2)
        int an = 0
        while an < o.annos.len() {
            Str av = o.annos.get!(an)
            Str src = anno_inplace_arg(&av)
            if src.len() > 0 {
                Str dst = o.dst.copy()
                int dix = svg_find_row(&bn, dst)
                int six = svg_find_row(&bn, src)
                if dix >= 0 && six >= 0 {
                    if bl.get!(dix) > bl.get!(six) { bl.set!(six, bl.get!(dix)) }
                    if bb.get!(dix) > bb.get!(six) { bb.set!(six, bb.get!(dix)) }
                    bv.set!(dix, 0)
                }
            }
            an = an + 1
        }
        k2 = k2 + 1
    }

    // peak concurrent live bytes (interval scan)
    int peak = 0
    int peak_at = 0
    int s = 0
    while s < N {
        int live = 0
        int b = 0
        while b < bn.len() {
            if bv.get!(b) == 1 && bs.get!(b) <= s && s <= bl.get!(b) {
                live = live + bb.get!(b)
            }
            b = b + 1
        }
        if live > peak { peak = live; peak_at = s }
        s = s + 1
    }
    int pool_floats = (peak + 3) / 4

    // report
    int pkt = (params_bytes * 10) / 1024
    Str r = "=== memory budget: "
    r.push_str(m.name)
    r.push_str(" (single-core, batch=1) ===\n\n")
    r.push_str(f"  params ......... {params_bytes} B  ({pkt/10}.{pkt%10} KB, {param_count} tensors, resident)\n")
    r.push_str(f"  scratch (peak) . {peak} B\n")
    r.push_str(f"  io (in+out) .... {io_bytes} B\n")
    r.push_str(f"  -> Pool.reserve = {pool_floats} floats (f32)\n")
    r.push_str("\n  [scratch liveness]\n")
    int b2 = 0
    while b2 < bn.len() {
        if bv.get!(b2) == 1 {
            r.push_str(f"    {bn.get!(b2)} : {bb.get!(b2)} B  live ops [{bs.get!(b2)}..{bl.get!(b2)}]\n")
        }
        b2 = b2 + 1
    }
    r.push_str(f"    peak {peak} B at op {peak_at}\n")
    return r
}

// ---- Forward emission (P4-b): lowering -> runnable self-checking LS program ----

// eps attr text of an op, default "0.00001"
def op_eps(&Op o) -> Str {
    int a = 0
    while a < o.attr_keys.len() {
        Str k = o.attr_keys.get!(a)
        if k.eq?("eps") || k.eq?("epsilon") { return o.attr_vals.get!(a) }
        a = a + 1
    }
    return "0.00001"
}

// Integer attribute value by key (e.g. attention `heads=8`); dflt if absent.
def op_attr_int(&Op o, Str key, int dflt) -> int {
    int a = 0
    while a < o.attr_keys.len() {
        Str k = o.attr_keys.get!(a)
        if k.eq?(key) { return o.attr_vals.get!(a).to_int()! }
        a = a + 1
    }
    return dflt
}

// member names of a param GROUP by name; empty if not a group / not found.
def find_group_members(&LsModel m, &Str gname) -> Vec(Str) {
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        if d.kind == KIND_GROUP {
            Str dn = d.name.copy()
            if dn.eq?(gname) {
                Vec(Str) out = {}
                int j = 0
                while j < d.members.len() {
                    out.push(d.members.get!(j))
                    j = j + 1
                }
                return out
            }
        }
        i = i + 1
    }
    Vec(Str) e = {}
    return e
}

// Emit a complete, self-contained, self-checking LS forward for the lowered model:
// inline Vec(f64) reference kernels (gemv + runtime BN-affine epilogue + relu),
// deterministic inline weights/input, prints a stable checksum + BUILD-RUN OK.
// v1: gemm/matmul (+ individual-param batchnorm fold) (+ relu epilogue), raw
// [N,K] weight layout (no packing — that is the P4-c perf step). Group-form
// batchnorm args and conv lowering are deferred.
def build_forward(&LsModel m) -> Str {
    // params (PARAM + GROUP members) -> p{idx}, with element sizes
    Vec(Str) pn = {}
    Vec(int) psz = {}
    int di = 0
    while di < m.decls.len() {
        Decl d = m.decls.get!(di)
        if d.kind == KIND_PARAM {
            Str nm = d.name.copy()
            pn.push(nm)
            Vec(int) dd = d.ty.dims.copy()
            psz.push(dims_prod(&dd))
        } else if d.kind == KIND_GROUP {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            int j = 0
            while j < d.members.len() {
                Str mm = d.members.get!(j)
                pn.push(mm)
                psz.push(sz)
                j = j + 1
            }
        }
        di = di + 1
    }

    Str r = "// === AUTO-GENERATED by model.build_forward (P4-b) — DO NOT EDIT ===\n"
    r.push_str("import std.core.vec\n")
    r.push_str("import std.core.math as math\n\n")
    r.push_str("def fill(int n, int seed) -> Vec(f64) {\n")
    r.push_str("    Vec(f64) v = {}\n    int i = 0\n")
    r.push_str("    while i < n { int t = ((i + seed) * 131 + 17) % 100; v.push((t as f64) * 0.01 + 0.05); i = i + 1 }\n")
    r.push_str("    return v\n}\n")
    r.push_str("def gemv(&Vec(f64) inp, &Vec(f64) W, &Vec(f64) bb, int K, int N) -> Vec(f64) {\n")
    r.push_str("    Vec(f64) out = {}\n    int j = 0\n")
    r.push_str("    while j < N {\n        f64 acc = bb.get!(j)\n        int k = 0\n")
    r.push_str("        while k < K { acc = acc + inp.get!(k) * W.get!(j * K + k); k = k + 1 }\n")
    r.push_str("        out.push(acc)\n        j = j + 1\n    }\n    return out\n}\n")
    r.push_str("def bn_affine(&!Vec(f64) v, &Vec(f64) g, &Vec(f64) be, &Vec(f64) mu, &Vec(f64) va, f64 eps) {\n")
    r.push_str("    int j = 0\n    while j < v.len() {\n")
    r.push_str("        f64 s = g.get!(j) / math.sqrt(va.get!(j) + eps)\n")
    r.push_str("        v.set!(j, (v.get!(j) - mu.get!(j)) * s + be.get!(j))\n        j = j + 1\n    }\n}\n")
    r.push_str("def relu_v(&!Vec(f64) v) {\n    int j = 0\n")
    r.push_str("    while j < v.len() { if v.get!(j) < 0.0 { v.set!(j, 0.0) } j = j + 1 }\n}\n")
    r.push_str("def zeros(int n) -> Vec(f64) {\n    Vec(f64) v = {}\n    int i = 0\n")
    r.push_str("    while i < n { v.push(0.0); i = i + 1 }\n    return v\n}\n")
    r.push_str("def cks(&Vec(f64) v) -> f64 {\n    f64 s = 0.0\n    int j = 0\n")
    r.push_str("    while j < v.len() { s = s + v.get!(j); j = j + 1 }\n    return s\n}\n\n")
    r.push_str("// ---- ")
    r.push_str(m.name)
    r.push_str(" forward ----\n")

    // param Vecs
    int pi = 0
    while pi < pn.len() {
        r.push_str(f"Vec(f64) p{pi} = fill({psz.get!(pi)}, {pi + 2})\n")
        pi = pi + 1
    }
    // input Vecs
    int ii = 0
    while ii < m.decls.len() {
        Decl d = m.decls.get!(ii)
        if d.kind == KIND_IN {
            Vec(int) dd = d.ty.dims.copy()
            r.push_str(f"Vec(f64) {d.name} = fill({dims_prod(&dd)}, 1)\n")
        }
        ii = ii + 1
    }

    // lowered op emission (fusion mirrors plan_lowering)
    int N = m.ops.len()
    Vec(int) consumed = {}
    int z = 0
    while z < N { consumed.push(0); z = z + 1 }

    int k = 0
    while k < N {
        if consumed.get!(k) == 0 {
            Op o = m.ops.get!(k)
            Str kind = o.kind.copy()
            Str dst = o.dst.copy()
            if is_gemm_like(&kind) {
                Str in0 = o.ins.get!(0)
                Str wnm = o.ins.get!(1)
                int wpi = svg_find_row(&pn, wnm)
                Str bref = ""
                if o.ins.len() >= 3 {
                    Str bnm = o.ins.get!(2)
                    int bpi = svg_find_row(&pn, bnm)
                    bref = f"&p{bpi}"
                }
                Vec(int) ind = find_dims(&m, in0)
                int K = dims_last(&ind)
                Vec(int) odims = o.ty.dims.copy()
                int Nn = dims_last(&odims)
                // scan the epilogue chain; ALL epilogue ops act on the final name
                Str finalname = dst.copy()
                bool has_bn = false
                int bg1 = 0
                int bg2 = 0
                int bg3 = 0
                int bg4 = 0
                Str beps = "0.00001"
                bool has_relu = false
                int cur = k
                Str curout = dst.copy()
                int nxt = value_consumer(&m, curout, cur)
                if nxt >= 0 {
                    Op no = m.ops.get!(nxt)
                    Str nk = no.kind.copy()
                    if nk.eq?("batchnorm") && value_use_count(&m, curout) == 1 {
                        // gamma/beta/mean/var: individual (>=5 ins) or group form
                        Vec(Str) bnp = {}
                        if no.ins.len() >= 5 {
                            bnp.push(no.ins.get!(1))
                            bnp.push(no.ins.get!(2))
                            bnp.push(no.ins.get!(3))
                            bnp.push(no.ins.get!(4))
                        } else if no.ins.len() == 2 {
                            Str gn = no.ins.get!(1)
                            Vec(Str) mem = find_group_members(&m, gn)
                            if mem.len() >= 4 {
                                bnp.push(mem.get!(0))
                                bnp.push(mem.get!(1))
                                bnp.push(mem.get!(2))
                                bnp.push(mem.get!(3))
                            }
                        }
                        if bnp.len() == 4 {
                            consumed.set!(nxt, 1)
                            has_bn = true
                            Str q0 = bnp.get!(0)
                            Str q1 = bnp.get!(1)
                            Str q2 = bnp.get!(2)
                            Str q3 = bnp.get!(3)
                            bg1 = svg_find_row(&pn, q0)
                            bg2 = svg_find_row(&pn, q1)
                            bg3 = svg_find_row(&pn, q2)
                            bg4 = svg_find_row(&pn, q3)
                            beps = op_eps(&no)
                            finalname = no.dst.copy()
                            cur = nxt
                            curout = no.dst.copy()
                        }
                    }
                }
                int nxt2 = value_consumer(&m, curout, cur)
                if nxt2 >= 0 {
                    Op ao = m.ops.get!(nxt2)
                    Str ak = ao.kind.copy()
                    if ak.eq?("relu") && value_use_count(&m, curout) == 1 {
                        consumed.set!(nxt2, 1)
                        has_relu = true
                        finalname = ao.dst.copy()
                    }
                }
                if o.ins.len() < 3 {
                    r.push_str(f"Vec(f64) {finalname}_zb = zeros({Nn})\n")
                    bref = f"&{finalname}_zb"
                }
                r.push_str(f"Vec(f64) {finalname} = gemv(&{in0}, &p{wpi}, {bref}, {K}, {Nn})\n")
                if has_bn {
                    r.push_str(f"bn_affine(&!{finalname}, &p{bg1}, &p{bg2}, &p{bg3}, &p{bg4}, {beps})\n")
                }
                if has_relu {
                    r.push_str(f"relu_v(&!{finalname})\n")
                }
            } else if kind.eq?("relu") {
                Str in0 = o.ins.get!(0)
                r.push_str(f"Vec(f64) {dst} = {in0}.copy()\n")
                r.push_str(f"relu_v(&!{dst})\n")
            } else {
                r.push_str(f"// note: op '{kind}' not lowered by build_forward v1\n")
                Str in0b = o.ins.get!(0)
                r.push_str(f"Vec(f64) {dst} = {in0b}.copy()\n")
            }
        }
        k = k + 1
    }

    // output checksum
    Str outname = ""
    int oi = 0
    while oi < m.decls.len() {
        Decl d = m.decls.get!(oi)
        if d.kind == KIND_OUT { outname = d.name.copy() }
        oi = oi + 1
    }
    r.push_str(f"@print(f\"checksum=" )
    r.push_str("{cks(&")
    r.push_str(outname)
    r.push_str(")}\")\n")
    r.push_str("@print(\"BUILD-RUN OK\")\n")
    return r
}

// ---- Forward emission, binary .lsw weights (P4-c-next #1) ----

// Like build_forward_real but loads weights from the binary .lsw blob
// (header "LSW1" + i32 count + concatenated f32 in param-declaration order).
// f32 is reconstructed from 4 LE bytes via a *u8 scratch reinterpreted as *f32.
// gemm weights are loaded transposed [N,K]->[K,N] for nn.sgemm. golden is text.
def build_forward_lsw(&LsModel m, Str lswpath, Str ginpath, Str goutpath) -> Str {
    Vec(Str) pn = {}
    Vec(int) psz = {}
    int di = 0
    while di < m.decls.len() {
        Decl d = m.decls.get!(di)
        if d.kind == KIND_PARAM {
            Str nm = d.name.copy()
            pn.push(nm)
            Vec(int) dd = d.ty.dims.copy()
            psz.push(dims_prod(&dd))
        } else if d.kind == KIND_GROUP {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            int jx = 0
            while jx < d.members.len() {
                pn.push(d.members.get!(jx))
                psz.push(sz)
                jx = jx + 1
            }
        }
        di = di + 1
    }
    // byte offsets into the blob: 8 (magic + i32 count) then cumulative f32 bytes
    Vec(int) poff = {}
    int acc = 8
    int pj = 0
    while pj < pn.len() {
        poff.push(acc)
        acc = acc + psz.get!(pj) * 4
        pj = pj + 1
    }

    Str r = "// === AUTO-GENERATED by model.build_forward_lsw (P4-c-next #1) — DO NOT EDIT ===\n"
    r.push_str("import std.sci.nn as nn\n")
    r.push_str("import std.sys.c as sc\n")
    r.push_str("import std.core.math as math\n")
    r.push_str("import std.sys.io as io\n")
    r.push_str("import std.core.vec\n")
    r.push_str("import std.core.map\n\n")
    r.push_str("def rdf32(&Str raw, *u8 s, int off) -> f32 {\n")
    r.push_str("    s[0] = raw.byte_at!(off) as u8\n    s[1] = raw.byte_at!(off + 1) as u8\n")
    r.push_str("    s[2] = raw.byte_at!(off + 2) as u8\n    s[3] = raw.byte_at!(off + 3) as u8\n")
    r.push_str("    return (s as *f32)[0]\n}\n")
    r.push_str("def load_blk(&Str raw, *u8 s, int base, *f32 dst, int n) {\n")
    r.push_str("    int i = 0\n    while i < n { dst[i] = rdf32(&raw, s, base + i * 4); i = i + 1 }\n}\n")
    r.push_str("def load_blk_t(&Str raw, *u8 s, int base, *f32 dst, int N, int K) {\n")
    r.push_str("    int j = 0\n    while j < N {\n        int k = 0\n")
    r.push_str("        while k < K { dst[k * N + j] = rdf32(&raw, s, base + (j * K + k) * 4); k = k + 1 }\n")
    r.push_str("        j = j + 1\n    }\n}\n")
    r.push_str("def load_named(Str path) -> Map(Str, Vec(f64)) {\n")
    r.push_str("    Map(Str, Vec(f64)) mm = {}\n    Str raw = io.read_file(path)!\n")
    r.push_str("    Vec(Str) lines = raw.split(\"\\n\")\n    int li = 0\n")
    r.push_str("    while li < lines.len() {\n        Str line = lines.get!(li).trim()\n")
    r.push_str("        if line.len() > 0 {\n            Vec(Str) toks = line.split(\" \")\n")
    r.push_str("            Str nm = toks.get!(0)\n            Vec(f64) vals = {}\n            int t = 1\n")
    r.push_str("            while t < toks.len() { vals.push(toks.get!(t).to_float()!); t = t + 1 }\n")
    r.push_str("            mm.set(nm, vals)\n        }\n        li = li + 1\n    }\n    return mm\n}\n")
    r.push_str("def fill_v(&Map(Str, Vec(f64)) mm, Str name, *f32 dst, int n) {\n")
    r.push_str("    match mm.get(name) { Some(v) => {\n        int i = 0\n")
    r.push_str("        while i < n { dst[i] = (v.get!(i)) as f32; i = i + 1 }\n    } None => {} }\n}\n")
    r.push_str("def bias_add(*f32 v, *f32 b, int n) { int j = 0\n    while j < n { v[j] = v[j] + b[j]; j = j + 1 } }\n")
    r.push_str("def bn_f32(*f32 v, *f32 g, *f32 be, *f32 mu, *f32 va, int n, f64 eps) {\n    int j = 0\n")
    r.push_str("    while j < n {\n        f64 sd = (g[j] as f64) / math.sqrt((va[j] as f64) + eps)\n")
    r.push_str("        v[j] = (((v[j] as f64) - (mu[j] as f64)) * sd + (be[j] as f64)) as f32\n        j = j + 1\n    }\n}\n")
    r.push_str("def maxerr(*f32 y, &Vec(f64) g, int n) -> f64 {\n    f64 e = 0.0\n    int j = 0\n")
    r.push_str("    while j < n { f64 d = (y[j] as f64) - g.get!(j); if d < 0.0 { d = 0.0 - d } if d > e { e = d } j = j + 1 }\n    return e\n}\n\n")
    r.push_str("// ---- ")
    r.push_str(m.name)
    r.push_str(" .lsw forward ----\n")
    r.push_str(f"Str BLOB = io.read_file(\"{lswpath}\")!\n")
    r.push_str("*u8 s4 = sc.malloc(4) as *u8\n")
    r.push_str(f"Map(Str, Vec(f64)) GI = load_named(\"{ginpath}\")\n")
    r.push_str(f"Map(Str, Vec(f64)) GO = load_named(\"{goutpath}\")\n")

    Vec(Str) freelist = {}
    Str s4f = "s4"
    freelist.push(s4f)

    // input buffers (text golden)
    int ii = 0
    while ii < m.decls.len() {
        Decl d = m.decls.get!(ii)
        if d.kind == KIND_IN {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            r.push_str(f"*f32 {d.name} = sc.malloc({sz} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&GI, \"{d.name}\", {d.name}, {sz})\n")
            Str fn = d.name.copy()
            freelist.push(fn)
        }
        ii = ii + 1
    }
    // pool
    int total_act = 16
    int ti = 0
    while ti < m.ops.len() {
        Op ot = m.ops.get!(ti)
        Vec(int) td = ot.ty.dims.copy()
        total_act = total_act + ((dims_prod(&td) + 15) / 16) * 16
        ti = ti + 1
    }
    r.push_str("nn.Pool pool = {}\n")
    r.push_str(f"pool.reserve({total_act})\n")

    int N = m.ops.len()
    Vec(int) consumed = {}
    int z = 0
    while z < N { consumed.push(0); z = z + 1 }
    int k = 0
    while k < N {
        if consumed.get!(k) == 0 {
            Op o = m.ops.get!(k)
            Str kind = o.kind.copy()
            Str dst = o.dst.copy()
            if is_gemm_like(&kind) {
                Str in0 = o.ins.get!(0)
                int wpi = svg_find_row(&pn, o.ins.get!(1))
                int woff = poff.get!(wpi)
                Vec(int) ind = find_dims(&m, in0)
                int K = dims_last(&ind)
                Vec(int) od = o.ty.dims.copy()
                int Nn = dims_last(&od)
                // weight (transposed load [N,K]->[K,N])
                r.push_str(f"*f32 {dst}_W = sc.malloc({K} * {Nn} * sizeof(f32)) as *f32\n")
                r.push_str(f"load_blk_t(&BLOB, s4, {woff}, {dst}_W, {Nn}, {K})\n")
                Str fw = f"{dst}_W"
                freelist.push(fw)
                bool has_bias = false
                if o.ins.len() >= 3 {
                    int bpi = svg_find_row(&pn, o.ins.get!(2))
                    int boff = poff.get!(bpi)
                    r.push_str(f"*f32 {dst}_b = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"load_blk(&BLOB, s4, {boff}, {dst}_b, {Nn})\n")
                    Str fb = f"{dst}_b"
                    freelist.push(fb)
                    has_bias = true
                }
                Str finalname = dst.copy()
                bool has_bn = false
                Str beps = "0.00001"
                bool has_relu = false
                int cur = k
                Str curout = dst.copy()
                Vec(int) bnoff = {}
                int nxt = value_consumer(&m, curout, cur)
                if nxt >= 0 {
                    Op no = m.ops.get!(nxt)
                    Str nk = no.kind.copy()
                    if nk.eq?("batchnorm") && value_use_count(&m, curout) == 1 {
                        Vec(Str) bnp = {}
                        if no.ins.len() >= 5 {
                            bnp.push(no.ins.get!(1))
                            bnp.push(no.ins.get!(2))
                            bnp.push(no.ins.get!(3))
                            bnp.push(no.ins.get!(4))
                        } else if no.ins.len() == 2 {
                            Vec(Str) mem = find_group_members(&m, no.ins.get!(1))
                            if mem.len() >= 4 {
                                bnp.push(mem.get!(0))
                                bnp.push(mem.get!(1))
                                bnp.push(mem.get!(2))
                                bnp.push(mem.get!(3))
                            }
                        }
                        if bnp.len() == 4 {
                            consumed.set!(nxt, 1)
                            has_bn = true
                            int q = 0
                            while q < 4 {
                                int pidx = svg_find_row(&pn, bnp.get!(q))
                                bnoff.push(poff.get!(pidx))
                                q = q + 1
                            }
                            beps = op_eps(&no)
                            finalname = no.dst.copy()
                            cur = nxt
                            curout = no.dst.copy()
                        }
                    }
                }
                int nxt2 = value_consumer(&m, curout, cur)
                if nxt2 >= 0 {
                    Op ao = m.ops.get!(nxt2)
                    Str ak = ao.kind.copy()
                    if ak.eq?("relu") && value_use_count(&m, curout) == 1 {
                        consumed.set!(nxt2, 1)
                        has_relu = true
                        finalname = ao.dst.copy()
                    }
                }
                r.push_str(f"*f32 {finalname} = pool.tensor({Nn})\n")
                r.push_str(f"nn.sgemm({in0}, {dst}_W, {finalname}, 1, {Nn}, {K})\n")
                if has_bias { r.push_str(f"bias_add({finalname}, {dst}_b, {Nn})\n") }
                if has_bn {
                    r.push_str(f"*f32 {finalname}_g = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"load_blk(&BLOB, s4, {bnoff.get!(0)}, {finalname}_g, {Nn})\n")
                    r.push_str(f"*f32 {finalname}_be = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"load_blk(&BLOB, s4, {bnoff.get!(1)}, {finalname}_be, {Nn})\n")
                    r.push_str(f"*f32 {finalname}_mu = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"load_blk(&BLOB, s4, {bnoff.get!(2)}, {finalname}_mu, {Nn})\n")
                    r.push_str(f"*f32 {finalname}_va = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"load_blk(&BLOB, s4, {bnoff.get!(3)}, {finalname}_va, {Nn})\n")
                    r.push_str(f"bn_f32({finalname}, {finalname}_g, {finalname}_be, {finalname}_mu, {finalname}_va, {Nn}, {beps})\n")
                    Str f1 = f"{finalname}_g"
                    Str f2 = f"{finalname}_be"
                    Str f3 = f"{finalname}_mu"
                    Str f4 = f"{finalname}_va"
                    freelist.push(f1)
                    freelist.push(f2)
                    freelist.push(f3)
                    freelist.push(f4)
                }
                if has_relu { r.push_str(f"nn.relu_inplace({finalname}, {Nn})\n") }
            }
        }
        k = k + 1
    }

    Str outname = ""
    int outN = 0
    int oi = 0
    while oi < m.decls.len() {
        Decl d = m.decls.get!(oi)
        if d.kind == KIND_OUT {
            outname = d.name.copy()
            Vec(int) od2 = d.ty.dims.copy()
            outN = dims_last(&od2)
        }
        oi = oi + 1
    }
    r.push_str("f64 err = 1.0\n")
    r.push_str("match GO.get(\"")
    r.push_str(outname)
    r.push_str("\") { Some(gy) => { err = maxerr(")
    r.push_str(outname)
    r.push_str(", &gy, ")
    r.push_str(f"{outN}")
    r.push_str(") } None => {} }\n")
    r.push_str("@print(f\"max_abs_err={err}\")\n")
    r.push_str("if err < 0.001 { @print(\"GOLDEN OK\") } else { @print(\"GOLDEN FAIL\") }\n")
    int fi = 0
    while fi < freelist.len() {
        r.push_str(f"sc.free({freelist.get!(fi)} as *u8)\n")
        fi = fi + 1
    }
    return r
}

// ---- Forward emission, real weights + golden (P4-c-next) ----

// Emit a SIMD f32 forward that loads REAL weights (text "name v0 v1 ..."),
// transpose-packs gemm weights [N,K]->[K,N] for nn.sgemm, carves activations from
// an nn.Pool, runs, and compares against a golden output (max_abs_err + GOLDEN OK).
// wpath/ginpath/goutpath are baked in as the weights / golden-input / golden-output files.
def build_forward_real(&LsModel m, Str wpath, Str ginpath, Str goutpath) -> Str {
    Str r = "// === AUTO-GENERATED by model.build_forward_real (P4-c-next) — DO NOT EDIT ===\n"
    r.push_str("import std.sci.nn as nn\n")
    r.push_str("import std.sys.c as sc\n")
    r.push_str("import std.core.math as math\n")
    r.push_str("import std.sys.io as io\n")
    r.push_str("import std.core.vec\n")
    r.push_str("import std.core.map\n\n")
    r.push_str("def load_named(Str path) -> Map(Str, Vec(f64)) {\n")
    r.push_str("    Map(Str, Vec(f64)) mm = {}\n    Str raw = io.read_file(path)!\n")
    r.push_str("    Vec(Str) lines = raw.split(\"\\n\")\n    int li = 0\n")
    r.push_str("    while li < lines.len() {\n        Str line = lines.get!(li).trim()\n")
    r.push_str("        if line.len() > 0 {\n            Vec(Str) toks = line.split(\" \")\n")
    r.push_str("            Str nm = toks.get!(0)\n            Vec(f64) vals = {}\n            int t = 1\n")
    r.push_str("            while t < toks.len() { vals.push(toks.get!(t).to_float()!); t = t + 1 }\n")
    r.push_str("            mm.set(nm, vals)\n        }\n        li = li + 1\n    }\n    return mm\n}\n")
    r.push_str("def fill_t(&Map(Str, Vec(f64)) mm, Str name, *f32 dst, int K, int N) {\n")
    r.push_str("    match mm.get(name) { Some(v) => {\n        int k = 0\n        while k < K {\n            int j = 0\n")
    r.push_str("            while j < N { dst[k * N + j] = (v.get!(j * K + k)) as f32; j = j + 1 }\n")
    r.push_str("            k = k + 1\n        }\n    } None => {} }\n}\n")
    r.push_str("def fill_v(&Map(Str, Vec(f64)) mm, Str name, *f32 dst, int n) {\n")
    r.push_str("    match mm.get(name) { Some(v) => {\n        int i = 0\n")
    r.push_str("        while i < n { dst[i] = (v.get!(i)) as f32; i = i + 1 }\n    } None => {} }\n}\n")
    r.push_str("def bias_add(*f32 v, *f32 b, int n) { int j = 0\n    while j < n { v[j] = v[j] + b[j]; j = j + 1 } }\n")
    r.push_str("def bn_f32(*f32 v, *f32 g, *f32 be, *f32 mu, *f32 va, int n, f64 eps) {\n    int j = 0\n")
    r.push_str("    while j < n {\n        f64 sd = (g[j] as f64) / math.sqrt((va[j] as f64) + eps)\n")
    r.push_str("        v[j] = (((v[j] as f64) - (mu[j] as f64)) * sd + (be[j] as f64)) as f32\n        j = j + 1\n    }\n}\n")
    r.push_str("def maxerr(*f32 y, &Vec(f64) g, int n) -> f64 {\n    f64 e = 0.0\n    int j = 0\n")
    r.push_str("    while j < n { f64 d = (y[j] as f64) - g.get!(j); if d < 0.0 { d = 0.0 - d } if d > e { e = d } j = j + 1 }\n    return e\n}\n\n")
    r.push_str("// ---- ")
    r.push_str(m.name)
    r.push_str(" real-weights forward ----\n")
    r.push_str(f"Map(Str, Vec(f64)) W = load_named(\"{wpath}\")\n")
    r.push_str(f"Map(Str, Vec(f64)) GI = load_named(\"{ginpath}\")\n")
    r.push_str(f"Map(Str, Vec(f64)) GO = load_named(\"{goutpath}\")\n")

    Vec(Str) freelist = {}
    // input buffers
    int ii = 0
    while ii < m.decls.len() {
        Decl d = m.decls.get!(ii)
        if d.kind == KIND_IN {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            r.push_str(f"*f32 {d.name} = sc.malloc({sz} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&GI, \"{d.name}\", {d.name}, {sz})\n")
            Str fn = d.name.copy()
            freelist.push(fn)
        }
        ii = ii + 1
    }
    // activation pool: reserve sum of all op outputs, each rounded up to 16 floats
    // (Pool aligns every carved tensor to 64 bytes), plus 16 floats of slack.
    // Conservative (counts folded ops too; no liveness reuse yet).
    int total_act = 16
    int ti = 0
    while ti < m.ops.len() {
        Op ot = m.ops.get!(ti)
        Vec(int) td = ot.ty.dims.copy()
        int e = dims_prod(&td)
        total_act = total_act + ((e + 15) / 16) * 16
        ti = ti + 1
    }
    r.push_str("nn.Pool pool = {}\n")
    r.push_str(f"pool.reserve({total_act})\n")

    // ops (fusion scan; gemm weights via fill_t, others via fill_v)
    int N = m.ops.len()
    Vec(int) consumed = {}
    int z = 0
    while z < N { consumed.push(0); z = z + 1 }
    int k = 0
    while k < N {
        if consumed.get!(k) == 0 {
            Op o = m.ops.get!(k)
            Str kind = o.kind.copy()
            Str dst = o.dst.copy()
            if is_gemm_like(&kind) {
                Str in0 = o.ins.get!(0)
                Str wnm = o.ins.get!(1)
                Vec(int) ind = find_dims(&m, in0)
                int K = dims_last(&ind)
                Vec(int) od = o.ty.dims.copy()
                int Nn = dims_last(&od)
                // weight buffer (transposed) + optional bias
                r.push_str(f"*f32 {dst}_W = sc.malloc({K} * {Nn} * sizeof(f32)) as *f32\n")
                r.push_str(f"fill_t(&W, \"{wnm}\", {dst}_W, {K}, {Nn})\n")
                Str fw = f"{dst}_W"
                freelist.push(fw)
                bool has_bias = false
                if o.ins.len() >= 3 {
                    Str bnm = o.ins.get!(2)
                    r.push_str(f"*f32 {dst}_b = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"fill_v(&W, \"{bnm}\", {dst}_b, {Nn})\n")
                    Str fb = f"{dst}_b"
                    freelist.push(fb)
                    has_bias = true
                }
                // epilogue scan
                Str finalname = dst.copy()
                bool has_bn = false
                Str beps = "0.00001"
                bool has_relu = false
                int cur = k
                Str curout = dst.copy()
                Vec(Str) bnnames = {}
                int nxt = value_consumer(&m, curout, cur)
                if nxt >= 0 {
                    Op no = m.ops.get!(nxt)
                    Str nk = no.kind.copy()
                    if nk.eq?("batchnorm") && value_use_count(&m, curout) == 1 {
                        if no.ins.len() >= 5 {
                            bnnames.push(no.ins.get!(1))
                            bnnames.push(no.ins.get!(2))
                            bnnames.push(no.ins.get!(3))
                            bnnames.push(no.ins.get!(4))
                        } else if no.ins.len() == 2 {
                            Vec(Str) mem = find_group_members(&m, no.ins.get!(1))
                            if mem.len() >= 4 {
                                bnnames.push(mem.get!(0))
                                bnnames.push(mem.get!(1))
                                bnnames.push(mem.get!(2))
                                bnnames.push(mem.get!(3))
                            }
                        }
                        if bnnames.len() == 4 {
                            consumed.set!(nxt, 1)
                            has_bn = true
                            beps = op_eps(&no)
                            finalname = no.dst.copy()
                            cur = nxt
                            curout = no.dst.copy()
                        }
                    }
                }
                int nxt2 = value_consumer(&m, curout, cur)
                if nxt2 >= 0 {
                    Op ao = m.ops.get!(nxt2)
                    Str ak = ao.kind.copy()
                    if ak.eq?("relu") && value_use_count(&m, curout) == 1 {
                        consumed.set!(nxt2, 1)
                        has_relu = true
                        finalname = ao.dst.copy()
                    }
                }
                // emit compute (activation from Pool)
                r.push_str(f"*f32 {finalname} = pool.tensor({Nn})\n")
                r.push_str(f"nn.sgemm({in0}, {dst}_W, {finalname}, 1, {Nn}, {K})\n")
                if has_bias { r.push_str(f"bias_add({finalname}, {dst}_b, {Nn})\n") }
                if has_bn {
                    r.push_str(f"*f32 {finalname}_g = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"fill_v(&W, \"{bnnames.get!(0)}\", {finalname}_g, {Nn})\n")
                    r.push_str(f"*f32 {finalname}_be = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"fill_v(&W, \"{bnnames.get!(1)}\", {finalname}_be, {Nn})\n")
                    r.push_str(f"*f32 {finalname}_mu = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"fill_v(&W, \"{bnnames.get!(2)}\", {finalname}_mu, {Nn})\n")
                    r.push_str(f"*f32 {finalname}_va = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                    r.push_str(f"fill_v(&W, \"{bnnames.get!(3)}\", {finalname}_va, {Nn})\n")
                    r.push_str(f"bn_f32({finalname}, {finalname}_g, {finalname}_be, {finalname}_mu, {finalname}_va, {Nn}, {beps})\n")
                    Str f1 = f"{finalname}_g"
                    Str f2 = f"{finalname}_be"
                    Str f3 = f"{finalname}_mu"
                    Str f4 = f"{finalname}_va"
                    freelist.push(f1)
                    freelist.push(f2)
                    freelist.push(f3)
                    freelist.push(f4)
                }
                if has_relu { r.push_str(f"nn.relu_inplace({finalname}, {Nn})\n") }
            }
        }
        k = k + 1
    }

    // golden compare on the output
    Str outname = ""
    int outN = 0
    int oi = 0
    while oi < m.decls.len() {
        Decl d = m.decls.get!(oi)
        if d.kind == KIND_OUT {
            outname = d.name.copy()
            Vec(int) od2 = d.ty.dims.copy()
            outN = dims_last(&od2)
        }
        oi = oi + 1
    }
    r.push_str("f64 err = 1.0\n")
    r.push_str("match GO.get(\"")
    r.push_str(outname)
    r.push_str("\") { Some(gy) => { err = maxerr(")
    r.push_str(outname)
    r.push_str(", &gy, ")
    r.push_str(f"{outN}")
    r.push_str(") } None => {} }\n")
    r.push_str("@print(f\"max_abs_err={err}\")\n")
    r.push_str("if err < 0.001 { @print(\"GOLDEN OK\") } else { @print(\"GOLDEN FAIL\") }\n")
    // free malloc'd buffers (Pool auto-frees activations via ~)
    int fi = 0
    while fi < freelist.len() {
        r.push_str(f"sc.free({freelist.get!(fi)} as *u8)\n")
        fi = fi + 1
    }
    return r
}

// True if the model uses any Tier-B/C op that the gemm-centric real emitter does
// not handle (transpose / softmax / layernorm / attention / add / gelu) — route
// these to build_forward_transformer.
def has_transformer_op(&LsModel m) -> bool {
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        Str kk = o.kind.copy()
        if kk.eq?("transpose") || kk.eq?("softmax") || kk.eq?("layernorm")
           || kk.eq?("attention") || kk.eq?("add") || kk.eq?("gelu")
           || kk.eq?("conv2d") || kk.eq?("gap") || kk.eq?("maxpool")
           || kk.eq?("avgpool") || kk.eq?("pool") { return true }
        k = k + 1
    }
    return false
}

// C1 (#3) — liveness-based Pool slot reuse for the transformer backend.
// Each op output is a Pool buffer live over [def, last-use]; a buffer dies once
// no later op reads it, and a dead buffer's Pool offset can be reused by a later
// buffer. Returns one Pool offset (in f32 units, 16-aligned) per op index; with
// `peak_out` set to the high-water (= the exact reserve). Declared model outputs
// live to the end (the golden compare reads them after all ops) so they never
// share a slot. The residual constraint (add(x,o) needs x still live at the add)
// is handled for free: x's [def,last-use] interval covers the add, so x stays
// placed and the add's fresh output is given a non-overlapping offset. In-place
// kernels (softmax/layernorm/gelu/relu/add vcopy src->dst) are likewise safe —
// the src input is live at that op, so dst gets a different offset (src != dst).
def assign_pool_slots(&LsModel m, &!Vec(int) peak_out) -> Vec(int) {
    int N = m.ops.len()
    // declared output names (live to the end, not reusable scratch)
    Vec(Str) outs = {}
    int d0 = 0
    while d0 < m.decls.len() {
        Decl d = m.decls.get!(d0)
        if d.kind == KIND_OUT { Str on = d.name.copy(); outs.push(on) }
        d0 = d0 + 1
    }
    // per-op buffer size (16-aligned floats), last-use op index, output name
    Vec(int) need = {}
    Vec(int) lastuse = {}
    Vec(Str) dname = {}
    int k = 0
    while k < N {
        Op o = m.ops.get!(k)
        Vec(int) od = o.ty.dims.copy()
        int e = dims_prod(&od)
        need.push(((e + 15) / 16) * 16)
        lastuse.push(k)
        Str dn = o.dst.copy()
        dname.push(dn)
        k = k + 1
    }
    // last use = highest op index that reads each buffer as an input
    int j = 0
    while j < N {
        Op o = m.ops.get!(j)
        int a = 0
        while a < o.ins.len() {
            Str inm = o.ins.get!(a)
            int bi = svg_find_row(&dname, &inm)
            if bi >= 0 { if j > lastuse.get!(bi) { lastuse.set!(bi, j) } }
            a = a + 1
        }
        j = j + 1
    }
    // declared outputs: extend last-use past the final op (sentinel N)
    int o2 = 0
    while o2 < N {
        Str dn = dname.get!(o2)
        if name_in(&outs, &dn) { lastuse.set!(o2, N) }
        o2 = o2 + 1
    }
    // greedy-by-size placement (TFLite-style): place the largest buffers first at
    // the lowest offset that conflicts with no already-placed buffer overlapping
    // it in time (overlap = def_a <= lu_b && def_b <= lu_a). Beats first-fit-by-def
    // on fragmentation — near-optimal packing of the liveness peak. def[b] = b.
    Vec(int) order = {}
    int z = 0
    while z < N { order.push(z); z = z + 1 }
    // insertion sort `order` by need desc, tie-break by longer lifetime desc
    int si = 1
    while si < N {
        int cur = order.get!(si)
        int sj = si - 1
        bool stop = false
        while sj >= 0 && !stop {
            int o0 = order.get!(sj)
            bool less = false
            if need.get!(o0) < need.get!(cur) {
                less = true
            } else if need.get!(o0) == need.get!(cur) {
                int lifeo = lastuse.get!(o0) - o0
                int lifec = lastuse.get!(cur) - cur
                if lifeo < lifec { less = true }
            }
            if less { order.set!(sj + 1, o0); sj = sj - 1 } else { stop = true }
        }
        order.set!(sj + 1, cur)
        si = si + 1
    }
    Vec(int) offset = {}
    int zz = 0
    while zz < N { offset.push(0 - 1); zz = zz + 1 }
    int peak = 0
    int oi2 = 0
    while oi2 < N {
        int b = order.get!(oi2)
        int blu = lastuse.get!(b)
        // offset intervals of already-placed buffers overlapping b in time
        Vec(int) ivo = {}
        Vec(int) ive = {}
        int p = 0
        while p < N {
            if offset.get!(p) >= 0 {
                if p <= blu && b <= lastuse.get!(p) {
                    ivo.push(offset.get!(p))
                    ive.push(offset.get!(p) + need.get!(p))
                }
            }
            p = p + 1
        }
        int place = 0
        bool changed = true
        while changed {
            changed = false
            int q = 0
            while q < ivo.len() {
                int s0 = ivo.get!(q)
                int e0 = ive.get!(q)
                if place < e0 && s0 < place + need.get!(b) {
                    if e0 > place { place = e0 }
                    changed = true
                }
                q = q + 1
            }
        }
        offset.set!(b, place)
        int top = place + need.get!(b)
        if top > peak { peak = top }
        oi2 = oi2 + 1
    }
    peak_out.set!(0, peak)
    return offset
}

// ---- Forward emission, transformer backend (P5-2): general per-op lowering ----
// Unlike build_forward_real (gemm-centric epilogue fusion, M=1 GEMV), this emits
// one kernel call per op: M>1 sgemm, DAG inputs (residual adds keep earlier
// activations live), and the Tier-B/C kernels. C1 (#3): op outputs are Pool
// buffers placed by a liveness planner (assign_pool_slots) so dead buffers'
// offsets are reused — peak (= reserve) instead of the sum of all outputs.
// In-place kernels (softmax/layernorm/gelu/relu/add) copy their input into the
// output buffer first so all values stay live for residuals. Weights: gemm
// W[N,K] transpose-loaded to [K,N]; matmul weights row-major [K,N] direct;
// matmul value inputs reuse the producer's buffer.
def build_forward_transformer(&LsModel m, Str wpath, Str ginpath, Str goutpath) -> Str {
    Str r = "// === AUTO-GENERATED by model.build_forward_transformer (P5-2) — DO NOT EDIT ===\n"
    r.push_str("import std.sci.nn as nn\n")
    r.push_str("import std.sys.c as sc\n")
    r.push_str("import std.core.math as math\n")
    r.push_str("import std.sys.io as io\n")
    r.push_str("import std.core.vec\n")
    r.push_str("import std.core.map\n\n")
    r.push_str("def load_named(Str path) -> Map(Str, Vec(f64)) {\n")
    r.push_str("    Map(Str, Vec(f64)) mm = {}\n    Str raw = io.read_file(path)!\n")
    r.push_str("    Vec(Str) lines = raw.split(\"\\n\")\n    int li = 0\n")
    r.push_str("    while li < lines.len() {\n        Str line = lines.get!(li).trim()\n")
    r.push_str("        if line.len() > 0 {\n            Vec(Str) toks = line.split(\" \")\n")
    r.push_str("            Str nm = toks.get!(0)\n            Vec(f64) vals = {}\n            int t = 1\n")
    r.push_str("            while t < toks.len() { vals.push(toks.get!(t).to_float()!); t = t + 1 }\n")
    r.push_str("            mm.set(nm, vals)\n        }\n        li = li + 1\n    }\n    return mm\n}\n")
    r.push_str("def fill_t(&Map(Str, Vec(f64)) mm, Str name, *f32 dst, int K, int N) {\n")
    r.push_str("    match mm.get(name) { Some(v) => {\n        int k = 0\n        while k < K {\n            int j = 0\n")
    r.push_str("            while j < N { dst[k * N + j] = (v.get!(j * K + k)) as f32; j = j + 1 }\n")
    r.push_str("            k = k + 1\n        }\n    } None => {} }\n}\n")
    r.push_str("def fill_v(&Map(Str, Vec(f64)) mm, Str name, *f32 dst, int n) {\n")
    r.push_str("    match mm.get(name) { Some(v) => {\n        int i = 0\n")
    r.push_str("        while i < n { dst[i] = (v.get!(i)) as f32; i = i + 1 }\n    } None => {} }\n}\n")
    r.push_str("def bias_add_rows(*f32 v, *f32 b, int M, int N) {\n    int i = 0\n")
    r.push_str("    while i < M {\n        int j = 0\n        while j < N { v[i * N + j] = v[i * N + j] + b[j]; j = j + 1 }\n        i = i + 1\n    }\n}\n")
    r.push_str("def vcopy(*f32 d, *f32 s, int n) {\n    int i = 0\n    while i < n { d[i] = s[i]; i = i + 1 }\n}\n")
    r.push_str("def maxerr(*f32 y, &Vec(f64) g, int n) -> f64 {\n    f64 e = 0.0\n    int j = 0\n")
    r.push_str("    while j < n { f64 d = (y[j] as f64) - g.get!(j); if d < 0.0 { d = 0.0 - d } if d > e { e = d } j = j + 1 }\n    return e\n}\n\n")
    r.push_str("// ---- ")
    r.push_str(m.name)
    r.push_str(" transformer forward ----\n")
    r.push_str(f"Map(Str, Vec(f64)) W = load_named(\"{wpath}\")\n")
    r.push_str(f"Map(Str, Vec(f64)) GI = load_named(\"{ginpath}\")\n")
    r.push_str(f"Map(Str, Vec(f64)) GO = load_named(\"{goutpath}\")\n")

    Vec(Str) freelist = {}
    // input buffers (malloc + fill from GI)
    int ii = 0
    while ii < m.decls.len() {
        Decl d = m.decls.get!(ii)
        if d.kind == KIND_IN {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            r.push_str(f"*f32 {d.name} = sc.malloc({sz} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&GI, \"{d.name}\", {d.name}, {sz})\n")
            Str fn = d.name.copy()
            freelist.push(fn)
        }
        ii = ii + 1
    }
    // activation pool: liveness-planned slot reuse (C1/#3). offsets[k] is op k's
    // Pool offset in f32 units; reserve = peak (not the sum of all outputs).
    Vec(int) pk = {}
    pk.push(0)
    Vec(int) offsets = assign_pool_slots(&m, &!pk)
    int total_act = pk.get!(0) + 16
    r.push_str("nn.Pool pool = {}\n")
    r.push_str(f"pool.reserve({total_act})\n")

    // per-op emission (no fusion)
    int N = m.ops.len()
    int k = 0
    while k < N {
        Op o = m.ops.get!(k)
        Str kind = o.kind.copy()
        Str dst = o.dst.copy()
        Vec(int) od = o.ty.dims.copy()
        int outE = dims_prod(&od)
        int M = dims_first(&od)
        int Ncol = dims_last(&od)
        if kind.eq?("matmul") {
            Str in0 = o.ins.get!(0)
            Str in1 = o.ins.get!(1)
            Vec(int) ad = find_dims(&m, in0)
            int K = dims_last(&ad)
            Str Bbuf = in1.copy()
            if name_is_param(&m, &in1) {
                r.push_str(f"*f32 {dst}_W = sc.malloc({K} * {Ncol} * sizeof(f32)) as *f32\n")
                r.push_str(f"fill_v(&W, \"{in1}\", {dst}_W, {K} * {Ncol})\n")
                Bbuf = f"{dst}_W"
                Str fw = Bbuf.copy()
                freelist.push(fw)
            }
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"nn.sgemm({in0}, {Bbuf}, {dst}, {M}, {Ncol}, {K})\n")
        } else if kind.eq?("gemm") {
            Str in0 = o.ins.get!(0)
            Str wnm = o.ins.get!(1)
            Vec(int) ad = find_dims(&m, in0)
            int K = dims_last(&ad)
            r.push_str(f"*f32 {dst}_W = sc.malloc({K} * {Ncol} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_t(&W, \"{wnm}\", {dst}_W, {K}, {Ncol})\n")
            Str fw = f"{dst}_W"
            freelist.push(fw)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"nn.sgemm({in0}, {dst}_W, {dst}, {M}, {Ncol}, {K})\n")
            if o.ins.len() >= 3 {
                Str bnm = o.ins.get!(2)
                r.push_str(f"*f32 {dst}_b = sc.malloc({Ncol} * sizeof(f32)) as *f32\n")
                r.push_str(f"fill_v(&W, \"{bnm}\", {dst}_b, {Ncol})\n")
                Str fb = f"{dst}_b"
                freelist.push(fb)
                r.push_str(f"bias_add_rows({dst}, {dst}_b, {M}, {Ncol})\n")
            }
        } else if kind.eq?("transpose") {
            Str in0 = o.ins.get!(0)
            Vec(int) ad = find_dims(&m, in0)
            int rows = dims_first(&ad)
            int cols = dims_last(&ad)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"nn.transpose({in0}, {dst}, {rows}, {cols})\n")
        } else if kind.eq?("softmax") {
            Str in0 = o.ins.get!(0)
            Vec(int) ad = find_dims(&m, in0)
            int rows = dims_first(&ad)
            int cols = dims_last(&ad)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"vcopy({dst}, {in0}, {outE})\n")
            r.push_str(f"nn.softmax_rows({dst}, {rows}, {cols})\n")
        } else if kind.eq?("layernorm") {
            Str in0 = o.ins.get!(0)
            Str gnm = o.ins.get!(1)
            Str bnm = o.ins.get!(2)
            Vec(int) ad = find_dims(&m, in0)
            int rows = dims_first(&ad)
            int cols = dims_last(&ad)
            Str eps = op_eps(&o)
            r.push_str(f"*f32 {dst}_g = sc.malloc({cols} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{gnm}\", {dst}_g, {cols})\n")
            r.push_str(f"*f32 {dst}_be = sc.malloc({cols} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{bnm}\", {dst}_be, {cols})\n")
            Str fg = f"{dst}_g"
            Str fb = f"{dst}_be"
            freelist.push(fg)
            freelist.push(fb)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"vcopy({dst}, {in0}, {outE})\n")
            r.push_str(f"nn.layernorm_rows({dst}, {dst}_g, {dst}_be, {rows}, {cols}, {eps})\n")
        } else if kind.eq?("gelu") {
            Str in0 = o.ins.get!(0)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"vcopy({dst}, {in0}, {outE})\n")
            r.push_str(f"nn.gelu_inplace({dst}, {outE})\n")
        } else if kind.eq?("relu") {
            Str in0 = o.ins.get!(0)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"vcopy({dst}, {in0}, {outE})\n")
            r.push_str(f"nn.relu_inplace({dst}, {outE})\n")
        } else if kind.eq?("add") {
            Str in0 = o.ins.get!(0)
            Str in1 = o.ins.get!(1)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"vcopy({dst}, {in0}, {outE})\n")
            r.push_str(f"nn.add_inplace({dst}, {in1}, {outE})\n")
        } else if kind.eq?("attention") {
            // Multi-head scaled dot-product self-attention. ins = x,Wq,Wk,Wv,Wo;
            // heads=H attr. Projections are full matmul (weights [K,D] row-major);
            // each head slices a [T,hd] column block, runs scaled softmax(QKᵀ)V,
            // and scatters its context back; the merged ctx is projected by Wo.
            // Internal buffers are malloc/free (not Pool); only the output o is a
            // Pool tensor.
            Str xin = o.ins.get!(0)
            Str wq = o.ins.get!(1)
            Str wk = o.ins.get!(2)
            Str wv = o.ins.get!(3)
            Str wo = o.ins.get!(4)
            int H = op_attr_int(&o, "heads", 1)
            Vec(int) xd = find_dims(&m, xin)
            int K = dims_last(&xd)
            int T = M
            int D = Ncol
            int hd = D / H
            // q/k/v full projections (matmul weight [K,D] direct fill_v)
            r.push_str(f"*f32 {dst}_Wq = sc.malloc({K} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{wq}\", {dst}_Wq, {K} * {D})\n")
            r.push_str(f"*f32 {dst}_q = sc.malloc({T} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"nn.sgemm({xin}, {dst}_Wq, {dst}_q, {T}, {D}, {K})\n")
            r.push_str(f"*f32 {dst}_Wk = sc.malloc({K} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{wk}\", {dst}_Wk, {K} * {D})\n")
            r.push_str(f"*f32 {dst}_k = sc.malloc({T} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"nn.sgemm({xin}, {dst}_Wk, {dst}_k, {T}, {D}, {K})\n")
            r.push_str(f"*f32 {dst}_Wv = sc.malloc({K} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{wv}\", {dst}_Wv, {K} * {D})\n")
            r.push_str(f"*f32 {dst}_v = sc.malloc({T} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"nn.sgemm({xin}, {dst}_Wv, {dst}_v, {T}, {D}, {K})\n")
            r.push_str(f"*f32 {dst}_Wo = sc.malloc({D} * {D} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{wo}\", {dst}_Wo, {D} * {D})\n")
            r.push_str(f"*f32 {dst}_ctx = sc.malloc({T} * {D} * sizeof(f32)) as *f32\n")
            // per-head scratch (reused across heads). A·V is computed transposed
            // (ctxᵀ = vhᵀ·shᵀ) so the wide T dim lands on the SIMD axis instead of
            // the narrow head dim hd<16 (C2, fixes the D1 AV-scalar finding): vht
            // = slice_cols_t (V column block, transposed [hd,T]); sht = transpose
            // of the softmaxed scores [T,T]; cht [hd,T] = sgemm(vht, sht) with N=T.
            r.push_str(f"*f32 {dst}_qh = sc.malloc({T} * {hd} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst}_kh = sc.malloc({T} * {hd} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst}_vht = sc.malloc({hd} * {T} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst}_kt = sc.malloc({hd} * {T} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst}_sh = sc.malloc({T} * {T} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst}_sht = sc.malloc({T} * {T} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst}_cht = sc.malloc({hd} * {T} * sizeof(f32)) as *f32\n")
            r.push_str(f"int {dst}_h = 0\n")
            r.push_str(f"while {dst}_h < {H} ")
            r.push_str("{\n")
            r.push_str(f"    int {dst}_c0 = {dst}_h * {hd}\n")
            r.push_str(f"    nn.slice_cols({dst}_q, {dst}_qh, {T}, {D}, {dst}_c0, {hd})\n")
            r.push_str(f"    nn.slice_cols({dst}_k, {dst}_kh, {T}, {D}, {dst}_c0, {hd})\n")
            r.push_str(f"    nn.slice_cols_t({dst}_v, {dst}_vht, {T}, {D}, {dst}_c0, {hd})\n")
            r.push_str(f"    nn.transpose({dst}_kh, {dst}_kt, {T}, {hd})\n")
            r.push_str(f"    nn.sgemm({dst}_qh, {dst}_kt, {dst}_sh, {T}, {T}, {hd})\n")
            r.push_str(f"    nn.scale_inplace({dst}_sh, {T} * {T}, 1.0 / math.sqrt(({hd}) as f64))\n")
            r.push_str(f"    nn.softmax_rows({dst}_sh, {T}, {T})\n")
            r.push_str(f"    nn.transpose({dst}_sh, {dst}_sht, {T}, {T})\n")
            r.push_str(f"    nn.sgemm({dst}_vht, {dst}_sht, {dst}_cht, {hd}, {T}, {T})\n")
            r.push_str(f"    nn.place_cols_t({dst}_ctx, {dst}_cht, {T}, {D}, {dst}_c0, {hd})\n")
            r.push_str(f"    {dst}_h = {dst}_h + 1\n")
            r.push_str("}\n")
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"nn.sgemm({dst}_ctx, {dst}_Wo, {dst}, {T}, {D}, {D})\n")
            Str a1 = f"{dst}_Wq"
            Str a2 = f"{dst}_q"
            Str a3 = f"{dst}_Wk"
            Str a4 = f"{dst}_k"
            Str a5 = f"{dst}_Wv"
            Str a6 = f"{dst}_v"
            Str a7 = f"{dst}_Wo"
            Str a8 = f"{dst}_ctx"
            Str a9 = f"{dst}_qh"
            Str a10 = f"{dst}_kh"
            Str a11 = f"{dst}_vht"
            Str a12 = f"{dst}_kt"
            Str a13 = f"{dst}_sh"
            Str a14 = f"{dst}_cht"
            Str a15 = f"{dst}_sht"
            freelist.push(a1)
            freelist.push(a2)
            freelist.push(a3)
            freelist.push(a4)
            freelist.push(a5)
            freelist.push(a6)
            freelist.push(a7)
            freelist.push(a8)
            freelist.push(a9)
            freelist.push(a10)
            freelist.push(a11)
            freelist.push(a12)
            freelist.push(a13)
            freelist.push(a14)
            freelist.push(a15)
        } else if kind.eq?("conv2d") {
            // 2D conv via nn.conv2d_direct (im2col-free, width-vectorized; C2).
            // input [Cin,H,W], weight [Cout,Cin,Kh,Kw] (flat fill_v), bias [Cout].
            // pbuf = zero-padded input copy Cin*(H+2pad)*(W+2pad) (≪ im2col col),
            // weight/bias are malloc; output is a Pool tensor.
            Str xin = o.ins.get!(0)
            Str wnm = o.ins.get!(1)
            Str bnm = o.ins.get!(2)
            int cpad = op_attr_int(&o, "pad", 0)
            Vec(int) xd = find_dims(&m, xin)
            int Cin = xd.get!(0)
            int Hh = xd.get!(1)
            int Ww = xd.get!(2)
            Vec(int) wd = find_dims(&m, wnm)
            int Cout = wd.get!(0)
            int Kh = wd.get!(2)
            int Kw = wd.get!(3)
            int pbsz = Cin * (Hh + 2 * cpad) * (Ww + 2 * cpad)
            int wsz = Cout * Cin * Kh * Kw
            r.push_str(f"*f32 {dst}_W = sc.malloc({wsz} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{wnm}\", {dst}_W, {wsz})\n")
            r.push_str(f"*f32 {dst}_b = sc.malloc({Cout} * sizeof(f32)) as *f32\n")
            r.push_str(f"fill_v(&W, \"{bnm}\", {dst}_b, {Cout})\n")
            r.push_str(f"*f32 {dst}_pb = sc.malloc({pbsz} * sizeof(f32)) as *f32\n")
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"nn.conv2d_direct({xin}, {dst}_W, {dst}_b, {dst}, {dst}_pb, {Cin}, {Cout}, {Hh}, {Ww}, {Kh}, {Kw}, {cpad})\n")
            Str cw = f"{dst}_W"
            Str cb = f"{dst}_b"
            Str cc = f"{dst}_pb"
            freelist.push(cw)
            freelist.push(cb)
            freelist.push(cc)
        } else if kind.eq?("gap") {
            Str xin = o.ins.get!(0)
            Vec(int) xd = find_dims(&m, xin)
            int C = xd.get!(0)
            int Hh = xd.get!(1)
            int Ww = xd.get!(2)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            r.push_str(f"nn.gap({xin}, {dst}, {C}, {Hh}, {Ww})\n")
        } else if kind.eq?("maxpool") || kind.eq?("avgpool") || kind.eq?("pool") {
            Str xin = o.ins.get!(0)
            int pk = op_attr_int(&o, "kernel", 2)
            int ps = op_attr_int(&o, "stride", pk)
            Vec(int) xd = find_dims(&m, xin)
            int C = xd.get!(0)
            int Hh = xd.get!(1)
            int Ww = xd.get!(2)
            r.push_str(f"*f32 {dst} = pool.at({offsets.get!(k)}, {outE})\n")
            if kind.eq?("avgpool") {
                r.push_str(f"nn.avgpool2d({xin}, {dst}, {C}, {Hh}, {Ww}, {pk}, {ps})\n")
            } else {
                r.push_str(f"nn.maxpool2d({xin}, {dst}, {C}, {Hh}, {Ww}, {pk}, {ps})\n")
            }
        }
        k = k + 1
    }

    // golden compare on the output (full tensor)
    Str outname = ""
    int outN = 0
    int oi = 0
    while oi < m.decls.len() {
        Decl d = m.decls.get!(oi)
        if d.kind == KIND_OUT {
            outname = d.name.copy()
            Vec(int) od2 = d.ty.dims.copy()
            outN = dims_prod(&od2)
        }
        oi = oi + 1
    }
    r.push_str("f64 err = 1.0\n")
    r.push_str("match GO.get(\"")
    r.push_str(outname)
    r.push_str("\") { Some(gy) => { err = maxerr(")
    r.push_str(outname)
    r.push_str(", &gy, ")
    r.push_str(f"{outN}")
    r.push_str(") } None => {} }\n")
    r.push_str("@print(f\"max_abs_err={err}\")\n")
    r.push_str("if err < 0.001 { @print(\"GOLDEN OK\") } else { @print(\"GOLDEN FAIL\") }\n")
    int fi = 0
    while fi < freelist.len() {
        r.push_str(f"sc.free({freelist.get!(fi)} as *u8)\n")
        fi = fi + 1
    }
    return r
}

// ---- Forward emission, SIMD backend (P4-c): lower to nn.sgemm f32 ----

// Emit a self-contained f32 forward using the SIMD nn.sgemm kernel on raw *f32
// buffers (sgemm B is [K,N], no bias -> bias added separately), runtime BN-affine
// + nn.relu_inplace epilogue, malloc/free buffers. Deterministic inline data +
// checksum. v1: gemm/matmul (+bn +relu); weights laid out [K,N] directly (no
// transpose-pack — that + real .lsw weights + Pool = P4-c-next).
def build_forward_simd(&LsModel m) -> Str {
    Vec(Str) pn = {}
    Vec(int) psz = {}
    int di = 0
    while di < m.decls.len() {
        Decl d = m.decls.get!(di)
        if d.kind == KIND_PARAM {
            Str nm = d.name.copy()
            pn.push(nm)
            Vec(int) dd = d.ty.dims.copy()
            psz.push(dims_prod(&dd))
        } else if d.kind == KIND_GROUP {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            int j = 0
            while j < d.members.len() {
                pn.push(d.members.get!(j))
                psz.push(sz)
                j = j + 1
            }
        }
        di = di + 1
    }

    Str r = "// === AUTO-GENERATED by model.build_forward_simd (P4-c) — DO NOT EDIT ===\n"
    r.push_str("import std.sci.nn as nn\n")
    r.push_str("import std.sys.c as sc\n")
    r.push_str("import std.core.math as math\n\n")
    r.push_str("def fillp(*f32 p, int n, int seed) {\n    int i = 0\n")
    r.push_str("    while i < n { f64 t = (((i + seed) * 131 + 17) % 100) as f64; p[i] = (t * 0.01 + 0.05) as f32; i = i + 1 }\n}\n")
    r.push_str("def bias_add(*f32 v, *f32 b, int n) {\n    int j = 0\n")
    r.push_str("    while j < n { v[j] = v[j] + b[j]; j = j + 1 }\n}\n")
    r.push_str("def bn_f32(*f32 v, *f32 g, *f32 be, *f32 mu, *f32 va, int n, f64 eps) {\n    int j = 0\n")
    r.push_str("    while j < n {\n        f64 sd = (g[j] as f64) / math.sqrt((va[j] as f64) + eps)\n")
    r.push_str("        v[j] = (((v[j] as f64) - (mu[j] as f64)) * sd + (be[j] as f64)) as f32\n        j = j + 1\n    }\n}\n")
    r.push_str("def cks(*f32 v, int n) -> f64 {\n    f64 s = 0.0\n    int j = 0\n")
    r.push_str("    while j < n { s = s + (v[j] as f64); j = j + 1 }\n    return s\n}\n\n")
    r.push_str("// ---- ")
    r.push_str(m.name)
    r.push_str(" SIMD forward ----\n")

    Vec(Str) freelist = {}

    // params
    int pi = 0
    while pi < pn.len() {
        r.push_str(f"*f32 p{pi} = sc.malloc({psz.get!(pi)} * sizeof(f32)) as *f32\n")
        r.push_str(f"fillp(p{pi}, {psz.get!(pi)}, {pi + 2})\n")
        Str fp = f"p{pi}"
        freelist.push(fp)
        pi = pi + 1
    }
    // inputs
    int ii = 0
    while ii < m.decls.len() {
        Decl d = m.decls.get!(ii)
        if d.kind == KIND_IN {
            Vec(int) dd = d.ty.dims.copy()
            int sz = dims_prod(&dd)
            r.push_str(f"*f32 {d.name} = sc.malloc({sz} * sizeof(f32)) as *f32\n")
            r.push_str(f"fillp({d.name}, {sz}, 1)\n")
            Str fn = d.name.copy()
            freelist.push(fn)
        }
        ii = ii + 1
    }

    // ops
    int N = m.ops.len()
    Vec(int) consumed = {}
    int z = 0
    while z < N { consumed.push(0); z = z + 1 }
    int k = 0
    while k < N {
        if consumed.get!(k) == 0 {
            Op o = m.ops.get!(k)
            Str kind = o.kind.copy()
            Str dst = o.dst.copy()
            if is_gemm_like(&kind) {
                Str in0 = o.ins.get!(0)
                Str wnm = o.ins.get!(1)
                int wpi = svg_find_row(&pn, wnm)
                int bpi = 0 - 1
                if o.ins.len() >= 3 { bpi = svg_find_row(&pn, o.ins.get!(2)) }
                Vec(int) ind = find_dims(&m, in0)
                int K = dims_last(&ind)
                Vec(int) od = o.ty.dims.copy()
                int Nn = dims_last(&od)
                Str finalname = dst.copy()
                bool has_bn = false
                int bg1 = 0
                int bg2 = 0
                int bg3 = 0
                int bg4 = 0
                Str beps = "0.00001"
                bool has_relu = false
                int cur = k
                Str curout = dst.copy()
                int nxt = value_consumer(&m, curout, cur)
                if nxt >= 0 {
                    Op no = m.ops.get!(nxt)
                    Str nk = no.kind.copy()
                    if nk.eq?("batchnorm") && value_use_count(&m, curout) == 1 {
                        Vec(Str) bnp = {}
                        if no.ins.len() >= 5 {
                            bnp.push(no.ins.get!(1))
                            bnp.push(no.ins.get!(2))
                            bnp.push(no.ins.get!(3))
                            bnp.push(no.ins.get!(4))
                        } else if no.ins.len() == 2 {
                            Vec(Str) mem = find_group_members(&m, no.ins.get!(1))
                            if mem.len() >= 4 {
                                bnp.push(mem.get!(0))
                                bnp.push(mem.get!(1))
                                bnp.push(mem.get!(2))
                                bnp.push(mem.get!(3))
                            }
                        }
                        if bnp.len() == 4 {
                            consumed.set!(nxt, 1)
                            has_bn = true
                            bg1 = svg_find_row(&pn, bnp.get!(0))
                            bg2 = svg_find_row(&pn, bnp.get!(1))
                            bg3 = svg_find_row(&pn, bnp.get!(2))
                            bg4 = svg_find_row(&pn, bnp.get!(3))
                            beps = op_eps(&no)
                            finalname = no.dst.copy()
                            cur = nxt
                            curout = no.dst.copy()
                        }
                    }
                }
                int nxt2 = value_consumer(&m, curout, cur)
                if nxt2 >= 0 {
                    Op ao = m.ops.get!(nxt2)
                    Str ak = ao.kind.copy()
                    if ak.eq?("relu") && value_use_count(&m, curout) == 1 {
                        consumed.set!(nxt2, 1)
                        has_relu = true
                        finalname = ao.dst.copy()
                    }
                }
                r.push_str(f"*f32 {finalname} = sc.malloc({Nn} * sizeof(f32)) as *f32\n")
                r.push_str(f"nn.sgemm({in0}, p{wpi}, {finalname}, 1, {Nn}, {K})\n")
                if bpi >= 0 { r.push_str(f"bias_add({finalname}, p{bpi}, {Nn})\n") }
                if has_bn { r.push_str(f"bn_f32({finalname}, p{bg1}, p{bg2}, p{bg3}, p{bg4}, {Nn}, {beps})\n") }
                if has_relu { r.push_str(f"nn.relu_inplace({finalname}, {Nn})\n") }
                Str ff = finalname.copy()
                freelist.push(ff)
            }
        }
        k = k + 1
    }

    // output checksum (out decl: name + last dim)
    Str outname = ""
    int outN = 0
    int oi = 0
    while oi < m.decls.len() {
        Decl d = m.decls.get!(oi)
        if d.kind == KIND_OUT {
            outname = d.name.copy()
            Vec(int) od2 = d.ty.dims.copy()
            outN = dims_last(&od2)
        }
        oi = oi + 1
    }
    r.push_str(f"@print(f\"checksum=")
    r.push_str("{cks(")
    r.push_str(outname)
    r.push_str(", ")
    r.push_str(f"{outN}")
    r.push_str(")}\")\n")
    r.push_str("@print(\"BUILD-RUN OK\")\n")
    // free all buffers
    int fi = 0
    while fi < freelist.len() {
        r.push_str(f"sc.free({freelist.get!(fi)} as *u8)\n")
        fi = fi + 1
    }
    return r
}

// ---- Lowering pass (P4 v1): fusion + BN-fold + kernel selection ----

def is_gemm_like(&Str k) -> bool {
    return k.eq?("gemm") || k.eq?("matmul") || k.eq?("conv1d") || k.eq?("conv2d")
}
def is_act(&Str k) -> bool {
    return k.eq?("relu") || k.eq?("sigmoid") || k.eq?("tanh")
        || k.eq?("silu") || k.eq?("gelu") || k.eq?("leakyrelu")
}
def kernel_for(&Str kind) -> Str {
    if kind.eq?("gemm") || kind.eq?("matmul") { return "nn.sgemm" }
    if kind.eq?("conv1d") { return "nn.conv1d (im2col)" }
    if kind.eq?("conv2d") { return "nn.conv2d (im2col)" }
    if kind.eq?("relu") { return "nn.relu_inplace" }
    if kind.eq?("sigmoid") || kind.eq?("tanh") || kind.eq?("silu") || kind.eq?("gelu") { return "std.sci.simd" }
    // Tier B/C — kernels are P5-2 (not yet emitted by build)
    if kind.eq?("softmax") { return "nn.softmax (P5-2)" }
    if kind.eq?("layernorm") { return "nn.layernorm (P5-2)" }
    if kind.eq?("attention") { return "nn.attention (P5-2)" }
    if kind.eq?("pool") || kind.eq?("maxpool") || kind.eq?("avgpool") || kind.eq?("gap") { return "nn.pool (P5-2)" }
    return "(elementwise)"
}

// how many ops read `name`
def value_use_count(&LsModel m, &Str name) -> int {
    int c = 0
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        int a = 0
        while a < o.ins.len() {
            Str in_name = o.ins.get!(a)
            if in_name.eq?(name) { c = c + 1 }
            a = a + 1
        }
        k = k + 1
    }
    return c
}
// first op index > after that reads `name`, else -1
def value_consumer(&LsModel m, &Str name, int after) -> int {
    int k = after + 1
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        int a = 0
        while a < o.ins.len() {
            Str in_name = o.ins.get!(a)
            if in_name.eq?(name) { return k }
            a = a + 1
        }
        k = k + 1
    }
    return 0 - 1
}

// Peephole fusion: gemm/conv (+ batchnorm affine-fold) (+ activation epilogue),
// each producer consumed exactly once. Reports the lowered op list + kernel choices.
def plan_lowering(&LsModel m) -> Str {
    int N = m.ops.len()
    Vec(int) consumed = {}
    int z = 0
    while z < N { consumed.push(0); z = z + 1 }

    Vec(Str) lines = {}
    Vec(Str) folded = {}
    int fused_count = 0

    int k = 0
    while k < N {
        if consumed.get!(k) == 0 {
            Op o = m.ops.get!(k)
            Str kind = o.kind.copy()
            Str dst = o.dst.copy()
            Str kern = kernel_for(&kind)
            Str line = f"  [{fused_count}] {kind} {dst}"
            if is_gemm_like(&kind) {
                Str epi = ""
                int cur = k
                Str curout = dst.copy()
                // fold a single-use batchnorm into the gemm/conv
                int nxt = value_consumer(&m, curout, cur)
                if nxt >= 0 {
                    Op no = m.ops.get!(nxt)
                    Str nk = no.kind.copy()
                    if nk.eq?("batchnorm") && value_use_count(&m, curout) == 1 {
                        epi.push_str(" +bn")
                        consumed.set!(nxt, 1)
                        Str nd = no.dst.copy()
                        folded.push(f"batchnorm {nd} (affine fold into {dst})")
                        cur = nxt
                        curout = nd.copy()
                    }
                }
                // fuse a single-use activation as the epilogue
                int nxt2 = value_consumer(&m, curout, cur)
                if nxt2 >= 0 {
                    Op ao = m.ops.get!(nxt2)
                    Str ak = ao.kind.copy()
                    if is_act(&ak) && value_use_count(&m, curout) == 1 {
                        epi.push_str(" +")
                        epi.push_str(ak)
                        consumed.set!(nxt2, 1)
                        Str ad = ao.dst.copy()
                        folded.push(f"{ak} {ad} (epilogue of {dst})")
                    }
                }
                line.push_str(epi)
                line.push_str("  -> ")
                line.push_str(kern)
                if epi.len() > 0 { line.push_str(" (fused epilogue)") }
            } else {
                line.push_str("  -> ")
                line.push_str(kern)
            }
            lines.push(line)
            fused_count = fused_count + 1
        }
        k = k + 1
    }

    Str r = "=== lowering: "
    r.push_str(m.name)
    r.push_str(" ===\n\n")
    r.push_str(f"  fusion: {N} ops -> {fused_count} fused\n")
    int i = 0
    while i < lines.len() {
        r.push_str(lines.get!(i))
        r.push_byte('\n')
        i = i + 1
    }
    if folded.len() > 0 {
        r.push_str("\n  folded:\n")
        int j = 0
        while j < folded.len() {
            r.push_str("    - ")
            r.push_str(folded.get!(j))
            r.push_byte('\n')
            j = j + 1
        }
    }
    r.push_str("\n  (P4 v1: lowering decisions; run_model.ls emission = P4-b)\n")
    return r
}

// ---- Latency estimate (P3): roofline + per-platform machine models ----

struct Machine {
    Str name
    f64 freq        // heavy-AVX-512 all-core GHz
    int fma         // 512-bit FMA ports
    int lanes       // f32 lanes per 512-bit vector
    int l2_bytes    // per-core L2
    int l3_bytes    // effective L3 working-set ceiling
    f64 l2_bw       // single-core GB/s from L2
    f64 l3_bw
    f64 dram_bw
}

// Spec-derived from documented microarchitecture (references/gnr_isa.md + Intel
// datasheets) — NOT SDE-measured (SDE is a functional emulator, no timing).
// f32 always uses AVX-512 FMA (never AMX), so peak differs mainly by freq.
// KEY: single-core DRAM BW is *core-limited* (~16-19 GB/s, set by the core's
// outstanding L2 misses) and does NOT scale with socket aggregate BW — a common
// modelling error. L2 ~= 64 B/cyc (one line/cyc) -> ~64*freq GB/s; single-core
// L3 ~25-30 GB/s (gnr_isa.md). For true timing, calibrate via std.sim / real HW.
def machines() -> Vec(Machine) {
    Vec(Machine) v = {}
    v.push(Machine { name: "ICX", freq: 2.8, fma: 2, lanes: 16, l2_bytes: 1310720,  l3_bytes: 50331648,  l2_bw: 179.0, l3_bw: 25.0, dram_bw: 16.0 })
    v.push(Machine { name: "SPR", freq: 3.0, fma: 2, lanes: 16, l2_bytes: 2097152,  l3_bytes: 117440512, l2_bw: 192.0, l3_bw: 28.0, dram_bw: 18.0 })
    v.push(Machine { name: "GNR", freq: 3.2, fma: 2, lanes: 16, l2_bytes: 2097152,  l3_bytes: 343932928, l2_bw: 205.0, l3_bw: 30.0, dram_bw: 19.0 })
    return v
}

def peak_gflops(&Machine mm) -> f64 {
    return ((mm.fma * mm.lanes * 2) as f64) * mm.freq
}
def bw_for(&Machine mm, int params_bytes) -> f64 {
    if params_bytes <= mm.l2_bytes { return mm.l2_bw }
    if params_bytes <= mm.l3_bytes { return mm.l3_bw }
    return mm.dram_bw
}

// dims helpers on an owned Vec(int)
def dims_prod(&Vec(int) d) -> int {
    int p = 1
    int i = 0
    while i < d.len() {
        int x = d.get!(i)
        if x < 0 { x = 1 }
        p = p * x
        i = i + 1
    }
    return p
}
def dims_last(&Vec(int) d) -> int {
    if d.len() == 0 { return 1 }
    int x = d.get!(d.len() - 1)
    if x < 0 { return 1 }
    return x
}
def dims_first(&Vec(int) d) -> int {
    if d.len() == 0 { return 1 }
    int x = d.get!(0)
    if x < 0 { return 1 }
    return x
}

// dims of a named tensor (decl, group member, or op output); empty if absent.
def find_dims(&LsModel m, &Str name) -> Vec(int) {
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        Str dn = d.name.copy()
        if dn.eq?(name) { return d.ty.dims.copy() }
        if d.kind == KIND_GROUP {
            int j = 0
            while j < d.members.len() {
                Str mm = d.members.get!(j)
                if mm.eq?(name) { return d.ty.dims.copy() }
                j = j + 1
            }
        }
        i = i + 1
    }
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        Str od = o.dst.copy()
        if od.eq?(name) { return o.ty.dims.copy() }
        k = k + 1
    }
    Vec(int) empty = {}
    return empty
}

// True if `name` is a declared param (or a member of a param group). Used by the
// transformer emitter to tell weight inputs (need a filled buffer) from value
// inputs (an existing activation buffer).
def name_is_param(&LsModel m, &Str nm) -> bool {
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        if d.kind == KIND_PARAM {
            Str dn = d.name.copy()
            if dn.eq?(nm) { return true }
        } else if d.kind == KIND_GROUP {
            int j = 0
            while j < d.members.len() {
                Str mm = d.members.get!(j)
                if mm.eq?(nm) { return true }
                j = j + 1
            }
        }
        i = i + 1
    }
    return false
}

// bytes of a named tensor (group name -> all members).
def find_bytes(&LsModel m, &Str name) -> int {
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        Str dn = d.name.copy()
        if dn.eq?(name) {
            if d.kind == KIND_GROUP { return decl_bytes(&d) * d.members.len() }
            return decl_bytes(&d)
        }
        if d.kind == KIND_GROUP {
            int j = 0
            while j < d.members.len() {
                Str mm = d.members.get!(j)
                if mm.eq?(name) { return decl_bytes(&d) }
                j = j + 1
            }
        }
        i = i + 1
    }
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        Str od = o.dst.copy()
        if od.eq?(name) { return op_bytes(&o) }
        k = k + 1
    }
    return 0
}

// FLOPs for an op (2 MACs counted as 2 flops).
def op_flops(&LsModel m, &Op o) -> int {
    Str k = o.kind.copy()
    Vec(int) od = o.ty.dims.copy()
    int out_e = dims_prod(&od)
    if k.eq?("gemm") || k.eq?("matmul") {
        if o.ins.len() < 1 { return 0 }
        Str a0 = o.ins.get!(0)
        Vec(int) ad = find_dims(&m, a0)
        int kk = dims_last(&ad)
        return 2 * out_e * kk
    }
    if k.eq?("conv1d") || k.eq?("conv2d") {
        if o.ins.len() < 2 { return 0 }
        Str w = o.ins.get!(1)
        Vec(int) wd = find_dims(&m, w)
        int we = dims_prod(&wd)
        int cout = dims_first(&od)
        int spatial = out_e
        if cout > 0 { spatial = out_e / cout }
        return 2 * spatial * we
    }
    if k.eq?("attention") {
        // Multi-head scaled dot-product self-attention. out [T,D], x [T,K].
        // GEMM cost (heads H cancel since hd*H = D): 3 projections Q/K/V (6*T*D*K)
        // + per-head QKᵀ (2*T*T*D) + per-head A·V (2*T*T*D) + output proj Wo
        // (2*T*D*D). The scaled-softmax inside is f64-scalar epilogue, excluded
        // like the standalone softmax heuristic (SDE-verified in model_sde_mix).
        if o.ins.len() < 1 { return 0 }
        Str x0 = o.ins.get!(0)
        Vec(int) xd = find_dims(&m, x0)
        int T = dims_first(&od)
        int D = dims_last(&od)
        int Kx = dims_last(&xd)
        return 6 * T * D * Kx + 4 * T * T * D + 2 * T * D * D
    }
    int c = 1
    if k.eq?("sigmoid") || k.eq?("tanh") || k.eq?("silu") { c = 8 }
    else if k.eq?("gelu") || k.eq?("erf") { c = 10 }
    else if k.eq?("batchnorm") { c = 4 }
    else if k.eq?("softmax") { c = 5 }       // max + exp + sum + div, per element
    else if k.eq?("layernorm") { c = 6 }     // mean + var + normalize + affine
    else if k.eq?("pool") || k.eq?("maxpool") || k.eq?("avgpool") || k.eq?("gap") { c = 1 }
    else if k.eq?("reshape") || k.eq?("transpose") || k.eq?("concat")
         || k.eq?("split") || k.eq?("slice") || k.eq?("gather") || k.eq?("cast") { c = 0 }
    return out_e * c
}

// Bytes moved by an op = output + all inputs read.
def op_bytes_moved(&LsModel m, &Op o) -> int {
    Str dst = o.dst.copy()
    int total = find_bytes(&m, dst)
    int a = 0
    while a < o.ins.len() {
        Str in_name = o.ins.get!(a)
        total = total + find_bytes(&m, in_name)
        a = a + 1
    }
    return total
}

// Format an f64 to 2 decimals, e.g. 0.53.
def fmt2(f64 x) -> Str {
    int whole = x as int
    f64 frac = x - (whole as f64)
    int cents = (frac * 100.0 + 0.5) as int
    if cents >= 100 { whole = whole + 1; cents = 0 }
    Str r = f"{whole}."
    if cents < 10 { r.push_byte('0') }
    r.push_str(f"{cents}")
    return r
}

// Roofline latency per op = max(flops/peak, bytes/bw); bw tier from params fit.
def plan_latency(&LsModel m) -> Str {
    int params_bytes = 0
    int di = 0
    while di < m.decls.len() {
        Decl d = m.decls.get!(di)
        if d.kind == KIND_PARAM { params_bytes = params_bytes + decl_bytes(&d) }
        else if d.kind == KIND_GROUP { params_bytes = params_bytes + decl_bytes(&d) * d.members.len() }
        di = di + 1
    }

    Vec(Machine) ms = machines()
    Vec(f64) totals = {}
    int t0 = 0
    while t0 < ms.len() { totals.push(0.0); t0 = t0 + 1 }

    Str r = "=== latency estimate: "
    r.push_str(m.name)
    r.push_str(" (single-core, batch=1, f32 AVX-512) ===\n\n")
    r.push_str("  per-op ns  (ICX / SPR / GNR)\n")

    int membound = 0
    int N = m.ops.len()
    int k = 0
    while k < N {
        Op o = m.ops.get!(k)
        int fl = op_flops(&m, &o)
        int by = op_bytes_moved(&m, &o)
        Str line = f"  [{k}] {o.kind} {o.dst}: {fl} flop, {by} B  -> "
        int j = 0
        while j < ms.len() {
            Machine mm = ms.get!(j)
            f64 pk = peak_gflops(&mm)
            f64 bw = bw_for(&mm, params_bytes)
            f64 tc = (fl as f64) / pk
            f64 tm = (by as f64) / bw
            f64 top = tc
            if tm > tc { top = tm }
            if j == 0 {
                if tm >= tc { membound = membound + 1 }
            }
            int ns = (top + 0.5) as int
            if j > 0 { line.push_str(" / ") }
            line.push_str(f"{ns}")
            f64 cur = totals.get!(j)
            totals.set!(j, cur + top)
            j = j + 1
        }
        line.push_str(" ns")
        r.push_str(line)
        r.push_byte('\n')
        k = k + 1
    }

    r.push_str("  total: ")
    int t = 0
    while t < totals.len() {
        if t > 0 { r.push_str(" / ") }
        f64 us = totals.get!(t) / 1000.0
        r.push_str(fmt2(us))
        t = t + 1
    }
    r.push_str(" us  (ICX / SPR / GNR)\n")

    int pkb = params_bytes / 1024
    Machine icx = ms.get!(0)
    Str fit = "exceed"
    if params_bytes <= icx.l2_bytes { fit = "fit" }
    r.push_str(f"  [bottleneck] params {pkb} KB {fit} ICX L2; ")
    if membound * 2 >= N {
        r.push_str("memory-bound (batch=1 GEMV: weights read once per frame)\n")
    } else {
        r.push_str("compute-bound\n")
    }
    r.push_str("  (machine models spec-derived from references/gnr_isa.md; single-core\n")
    r.push_str("   DRAM is core-limited ~16-19 GB/s — for true timing calibrate via std.sim/HW)\n")
    return r
}

// ---- Visualization (P1) ----
// Shared op-line body: "dst = kind(ins) attrs : type @annos" (no leading idx/gutter).
def viz_op_body(&Op o) -> Str {
    Str s = o.dst.copy()
    s.push_str(" = ")
    s.push_str(o.kind)
    s.push_byte('(')
    int a = 0
    while a < o.ins.len() {
        if a > 0 { s.push_str(", ") }
        s.push_str(o.ins.get!(a))
        a = a + 1
    }
    s.push_byte(')')
    int at = 0
    while at < o.attr_keys.len() {
        s.push_byte(' ')
        s.push_str(o.attr_keys.get!(at))
        s.push_byte('=')
        s.push_str(o.attr_vals.get!(at))
        at = at + 1
    }
    s.push_str(" : ")
    s.push_str(reprint_type(&o.ty))
    int an = 0
    while an < o.annos.len() {
        s.push_str("  @")
        s.push_str(o.annos.get!(an))
        an = an + 1
    }
    return s
}

// ASCII layered DAG (default; monospace, pure ASCII for portable terminals).
// Vertical spine: textual order is topological; activation inputs shown inline.
def viz_ascii(&LsModel m) -> Str {
    Str s = "model "
    s.push_str(m.name)
    s.push_str("\n\n")
    // inputs
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        if d.kind == KIND_IN {
            s.push_str("  in   ")
            s.push_str(d.name)
            s.push_str(" : ")
            s.push_str(reprint_type(&d.ty))
            if d.layout.len() > 0 {
                s.push_str("  layout=")
                s.push_str(d.layout)
            }
            s.push_byte('\n')
        }
        i = i + 1
    }
    // ops
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        s.push_str("     |\n")
        s.push_str("     v  [")
        s.push_str(f"{k}")
        s.push_str("] ")
        s.push_str(viz_op_body(&o))
        s.push_byte('\n')
        k = k + 1
    }
    // outputs
    s.push_byte('\n')
    int j = 0
    while j < m.decls.len() {
        Decl d = m.decls.get!(j)
        if d.kind == KIND_OUT {
            s.push_str("  out  ")
            s.push_str(d.name)
            s.push_str(" : ")
            s.push_str(reprint_type(&d.ty))
            s.push_byte('\n')
        }
        j = j + 1
    }
    // params footer
    s.push_str("\n  params: ")
    int pc = 0
    int q = 0
    while q < m.decls.len() {
        Decl d = m.decls.get!(q)
        if d.kind == KIND_PARAM {
            if pc > 0 { s.push_str("  ") }
            s.push_str(d.name)
            s.push_byte(':')
            s.push_str(reprint_type(&d.ty))
            pc = pc + 1
        } else if d.kind == KIND_GROUP {
            if pc > 0 { s.push_str("  ") }
            s.push_str(d.name)
            s.push_byte(':')
            s.push_str(reprint_type(&d.ty))
            s.push_byte('x')
            s.push_str(f"{d.members.len()}")
            pc = pc + 1
        }
        q = q + 1
    }
    s.push_byte('\n')
    return s
}

// Build the list of activation producers (input names + op outputs) so DOT
// can draw data-flow edges and skip param/weight inputs. Vec (not Map) — see name_in.
def viz_producers(&LsModel m) -> Vec(Str) {
    Vec(Str) prod = {}
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        if d.kind == KIND_IN {
            Str nm = d.name.copy()
            prod.push(nm)
        }
        i = i + 1
    }
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        Str dn = o.dst.copy()
        prod.push(dn)
        k = k + 1
    }
    return prod
}

def dot_node(&!Str s, &Str id, &Str label, &Str shape) {
    s.push_str("  ")
    s.push_byte('"')
    s.push_str(id)
    s.push_byte('"')
    s.push_str(" [shape=")
    s.push_str(shape)
    s.push_str(", label=")
    s.push_byte('"')
    s.push_str(label)
    s.push_byte('"')
    s.push_str("];\n")
}

// Graphviz DOT: op nodes (box) + in/out nodes (ellipse) + activation edges.
// Param/weight inputs are omitted for a clean data-flow graph.
def viz_dot(&LsModel m) -> Str {
    Vec(Str) prod = viz_producers(&m)
    Str s = "digraph "
    s.push_str(m.name)
    s.push_str(" {\n  rankdir=TB;\n  node [fontname=\"monospace\"];\n")
    // input nodes
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        if d.kind == KIND_IN {
            Str idn = d.name.copy()
            Str lbl = d.name.copy()
            lbl.push_str("\\n")
            lbl.push_str(reprint_type(&d.ty))
            Str ell = "ellipse"
            dot_node(&!s, idn, lbl, ell)
        }
        i = i + 1
    }
    // op nodes + edges
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        Str lbl = o.kind.copy()
        lbl.push_str("\\n")
        lbl.push_str(o.dst)
        lbl.push_str(" : ")
        lbl.push_str(reprint_type(&o.ty))
        Str box = "box"
        Str odn = o.dst.copy()
        dot_node(&!s, odn, lbl, box)
        int a = 0
        while a < o.ins.len() {
            Str src = o.ins.get!(a)
            if name_in(&prod, src) {
                s.push_str("  ")
                s.push_byte('"')
                s.push_str(src)
                s.push_byte('"')
                s.push_str(" -> ")
                s.push_byte('"')
                s.push_str(o.dst)
                s.push_byte('"')
                s.push_str(";\n")
            }
            a = a + 1
        }
        k = k + 1
    }
    s.push_str("}\n")
    return s
}

// Find a row index by name in the render-order name list (-1 if absent).
def svg_find_row(&Vec(Str) names, &Str nm) -> int {
    int i = 0
    while i < names.len() {
        Str e = names.get!(i)
        if e.eq?(nm) { return i }
        i = i + 1
    }
    return 0 - 1
}

// reprint_type of a param/group decl named nm, else "".
def find_decl_type(&LsModel m, &Str nm) -> Str {
    int i = 0
    while i < m.decls.len() {
        Decl d = m.decls.get!(i)
        Str dn = d.name.copy()
        if dn.eq?(nm) { return reprint_type(&d.ty) }
        i = i + 1
    }
    return ""
}

// Native SVG renderer: single-column layered DAG (inputs then ops in topo order),
// data-dependency edges (straight spine + left-bowed skip/residual curves),
// weights annotated on the right. Self-contained — no graphviz dependency.
def viz_svg(&LsModel m) -> Str {
    Vec(Str) producers = viz_producers(&m)

    // outputs set (declared `out` names)
    Vec(Str) outs = {}
    int oo = 0
    while oo < m.decls.len() {
        Decl d = m.decls.get!(oo)
        if d.kind == KIND_OUT {
            Str on = d.name.copy()
            outs.push(on)
        }
        oo = oo + 1
    }

    // render-order row names: inputs first, then op outputs
    Vec(Str) rows = {}
    int nIn = 0
    int di = 0
    while di < m.decls.len() {
        Decl d = m.decls.get!(di)
        if d.kind == KIND_IN {
            Str nm = d.name.copy()
            rows.push(nm)
            nIn = nIn + 1
        }
        di = di + 1
    }
    int oi = 0
    while oi < m.ops.len() {
        Op o = m.ops.get!(oi)
        Str dn = o.dst.copy()
        rows.push(dn)
        oi = oi + 1
    }
    int n = rows.len()
    int height = 50 + n * 84 + 16

    Str s = ""
    s.push_str(f"<svg viewBox=\"0 0 720 {height}\" xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" aria-labelledby=\"t d\" font-family=\"ui-monospace, Menlo, Consolas, monospace\">\n")
    s.push_str(f"  <title id=\"t\">{m.name} computation graph</title>\n")
    s.push_str("  <desc id=\"d\">Single-column data-flow DAG: inputs then ops in topological order, weights on the right.</desc>\n")
    s.push_str("  <style>\n")
    s.push_str("    .op{fill:#EEEDFE;stroke:#7F77DD;stroke-width:1.5}\n")
    s.push_str("    .io{fill:#F1EFE8;stroke:#888780;stroke-width:1.5}\n")
    s.push_str("    .out{fill:#E1F5EE;stroke:#1D9E75;stroke-width:1.5}\n")
    s.push_str("    .nm{font-weight:700;font-size:15px;fill:#26215C}\n")
    s.push_str("    .io-nm{font-weight:700;font-size:15px;fill:#2C2C2A}\n")
    s.push_str("    .sh{font-size:12px;fill:#5F5E5A}\n")
    s.push_str("    .pm{font-size:11px;fill:#5F5E5A}\n")
    s.push_str("    .tag{font-size:10.5px;fill:#888780}\n")
    s.push_str("    .hd{font-size:13px;font-weight:700;fill:#26215C}\n")
    s.push_str("    .edge{stroke:#AFA9EC;stroke-width:2;fill:none}\n")
    s.push_str("  </style>\n")
    s.push_str("  <defs><marker id=\"ah\" markerWidth=\"9\" markerHeight=\"9\" refX=\"6\" refY=\"3\" orient=\"auto\"><path d=\"M0,0 L6,3 L0,6 Z\" fill=\"#AFA9EC\"/></marker></defs>\n")
    s.push_str(f"  <text x=\"160\" y=\"30\" class=\"hd\">model {m.name}</text>\n")

    // edges: data dependencies producer -> consumer
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        int crow = nIn + k
        int ctop = 50 + crow * 84
        int a = 0
        while a < o.ins.len() {
            Str src = o.ins.get!(a)
            int prow = svg_find_row(&rows, src)
            if prow >= 0 {
                int ptop = 50 + prow * 84
                if prow == crow - 1 {
                    int y1 = ptop + 54
                    s.push_str(f"  <line x1=\"280\" y1=\"{y1}\" x2=\"280\" y2=\"{ctop}\" class=\"edge\" marker-end=\"url(#ah)\"/>\n")
                } else {
                    int pmid = ptop + 27
                    int cmid = ctop + 27
                    int my = (pmid + cmid) / 2
                    s.push_str(f"  <path d=\"M 160 {pmid} Q 96 {my} 160 {cmid}\" class=\"edge\" marker-end=\"url(#ah)\"/>\n")
                }
            }
            a = a + 1
        }
        k = k + 1
    }

    // input nodes (rounded)
    int r = 0
    int di2 = 0
    while di2 < m.decls.len() {
        Decl d = m.decls.get!(di2)
        if d.kind == KIND_IN {
            int ty0 = 50 + r * 84
            int t1 = ty0 + 22
            int t2 = ty0 + 40
            Str tys = reprint_type(&d.ty)
            s.push_str(f"  <rect x=\"160\" y=\"{ty0}\" width=\"240\" height=\"54\" rx=\"27\" class=\"io\"/>\n")
            s.push_str(f"  <text x=\"280\" y=\"{t1}\" text-anchor=\"middle\" class=\"io-nm\">{d.name}</text>\n")
            s.push_str(f"  <text x=\"280\" y=\"{t2}\" text-anchor=\"middle\" class=\"sh\">{tys}  ·  input</text>\n")
            r = r + 1
        }
        di2 = di2 + 1
    }

    // op nodes (rect) + params on the right
    int k2 = 0
    while k2 < m.ops.len() {
        Op o = m.ops.get!(k2)
        int rr = nIn + k2
        int ty0 = 50 + rr * 84
        int t1 = ty0 + 22
        int t2 = ty0 + 40
        Str tys = reprint_type(&o.ty)
        // attrs appended to shape line
        int at = 0
        while at < o.attr_keys.len() {
            tys.push_byte(' ')
            tys.push_str(o.attr_keys.get!(at))
            tys.push_byte('=')
            tys.push_str(o.attr_vals.get!(at))
            at = at + 1
        }
        Str dstn = o.dst.copy()
        Str cls = "op"
        if name_in(&outs, dstn) { cls = "out" }
        s.push_str(f"  <rect x=\"160\" y=\"{ty0}\" width=\"240\" height=\"54\" rx=\"8\" class=\"{cls}\"/>\n")
        s.push_str(f"  <text x=\"176\" y=\"{t1}\" class=\"nm\">{o.kind}</text>\n")
        s.push_str(f"  <text x=\"384\" y=\"{t1}\" text-anchor=\"end\" class=\"sh\">{o.dst}</text>\n")
        s.push_str(f"  <text x=\"176\" y=\"{t2}\" class=\"sh\">{tys}</text>\n")
        // right-side: param inputs (name + shape), then annotations, then output tag
        int py = t1
        int a2 = 0
        while a2 < o.ins.len() {
            Str src = o.ins.get!(a2)
            if !name_in(&producers, src) {
                Str pt = find_decl_type(&m, src)
                s.push_str(f"  <text x=\"420\" y=\"{py}\" class=\"pm\">{src} {pt}</text>\n")
                py = py + 15
            }
            a2 = a2 + 1
        }
        int an = 0
        while an < o.annos.len() {
            s.push_str(f"  <text x=\"420\" y=\"{py}\" class=\"tag\">@{o.annos.get!(an)}</text>\n")
            py = py + 15
            an = an + 1
        }
        if name_in(&outs, dstn) {
            s.push_str(f"  <text x=\"420\" y=\"{py}\" class=\"tag\">→ output</text>\n")
        }
        k2 = k2 + 1
    }

    s.push_str("</svg>\n")
    return s
}

// Self-contained HTML page: model header + op table + params list.
// v1: no HTML-escaping (model identifiers/shapes are safe chars).
def viz_html(&LsModel m) -> Str {
    Str s = "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>model "
    s.push_str(m.name)
    s.push_str("</title>\n<style>body{font-family:monospace;margin:2em}")
    s.push_str("table{border-collapse:collapse;margin:1em 0}")
    s.push_str("td,th{border:1px solid #ccc;padding:4px 8px;text-align:left}")
    s.push_str("th{background:#f0f0f0}</style>\n</head><body>\n<h2>model ")
    s.push_str(m.name)
    s.push_str("</h2>\n<table>\n<tr><th>#</th><th>out</th><th>op</th><th>inputs</th><th>shape</th><th>attrs / annos</th></tr>\n")
    int k = 0
    while k < m.ops.len() {
        Op o = m.ops.get!(k)
        s.push_str("<tr><td>")
        s.push_str(f"{k}")
        s.push_str("</td><td>")
        s.push_str(o.dst)
        s.push_str("</td><td>")
        s.push_str(o.kind)
        s.push_str("</td><td>")
        int a = 0
        while a < o.ins.len() {
            if a > 0 { s.push_str(", ") }
            s.push_str(o.ins.get!(a))
            a = a + 1
        }
        s.push_str("</td><td>")
        s.push_str(reprint_type(&o.ty))
        s.push_str("</td><td>")
        int at = 0
        while at < o.attr_keys.len() {
            if at > 0 { s.push_byte(' ') }
            s.push_str(o.attr_keys.get!(at))
            s.push_byte('=')
            s.push_str(o.attr_vals.get!(at))
            at = at + 1
        }
        int an = 0
        while an < o.annos.len() {
            s.push_str(" @")
            s.push_str(o.annos.get!(an))
            an = an + 1
        }
        s.push_str("</td></tr>\n")
        k = k + 1
    }
    s.push_str("</table>\n<h3>params</h3>\n<ul>\n")
    int q = 0
    while q < m.decls.len() {
        Decl d = m.decls.get!(q)
        if d.kind == KIND_PARAM || d.kind == KIND_GROUP {
            s.push_str("<li>")
            s.push_str(d.name)
            s.push_str(" : ")
            s.push_str(reprint_type(&d.ty))
            if d.kind == KIND_GROUP {
                s.push_str(" (group of ")
                s.push_str(f"{d.members.len()}")
                s.push_str(")")
            }
            s.push_str("</li>\n")
        }
        q = q + 1
    }
    s.push_str("</ul>\n</body></html>\n")
    return s
}
