/* emit_c.c — C (Intel intrinsics) emitter for the numeric / SIMD kernel subset.
   See emit_c.h. The walk mirrors codegen but targets C source text instead of
   LLVM IR, and only accepts the subset (errors clearly otherwise). */
#include "emit_c.h"
#include "types.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char  *buf;
    size_t len, cap;
    bool   err;
    const char *path;
    /* names of the file's own top-level functions — the only calls allowed
       besides __simd_* intrinsics. */
    const char **fns;
    int fn_count;
    /* names of emittable (POD, in-subset) top-level structs — usable as C types. */
    const char **structs;
    int struct_count;
} CE;

/* ---- output buffer ---- */

static void ce_raw(CE *e, const char *s, size_t n)
{
    if (e->len + n + 1 > e->cap) {
        size_t nc = e->cap ? e->cap * 2 : 4096;
        while (nc < e->len + n + 1) nc *= 2;
        e->buf = realloc(e->buf, nc);
        e->cap = nc;
    }
    memcpy(e->buf + e->len, s, n);
    e->len += n;
    e->buf[e->len] = '\0';
}

static void ce(CE *e, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { ce_raw(e, tmp, (size_t)n); return; }
    char *big = malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    ce_raw(e, big, (size_t)n);
    free(big);
}

static void ce_indent(CE *e, int depth)
{
    for (int i = 0; i < depth; i++) ce_raw(e, "    ", 4);
}

static void ce_error(CE *e, AstNode *at, const char *fmt, ...)
{
    if (e->err) return;  /* report the first error only */
    e->err = true;
    int line = at ? at->line : 0, col = at ? at->column : 0;
    fprintf(stderr, "[emit-c] %s:%d:%d: ", e->path ? e->path : "?", line, col);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
}

/* ---- scalar / type mapping ---- */

static int scalar_bits(TypeKind k)
{
    switch (k) {
    case TYPE_I8: case TYPE_U8: case TYPE_CHAR: case TYPE_BOOL: return 8;
    case TYPE_I16: case TYPE_U16: case TYPE_F16: case TYPE_BF16: return 16;
    case TYPE_INT: case TYPE_I32: case TYPE_U32: case TYPE_F32: return 32;
    case TYPE_I64: case TYPE_U64: case TYPE_F64: return 64;
    default: return 0;
    }
}

static bool kind_is_unsigned(TypeKind k)
{
    return k == TYPE_U8 || k == TYPE_U16 || k == TYPE_U32 || k == TYPE_U64;
}

static bool kind_is_int(TypeKind k)
{
    switch (k) {
    case TYPE_INT: case TYPE_I8: case TYPE_I16: case TYPE_I32: case TYPE_I64:
    case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
    case TYPE_CHAR: case TYPE_BOOL: return true;
    default: return false;
    }
}

/* C name for a scalar prim. NULL if not a supported scalar. */
static const char *c_scalar_name(TypeKind k)
{
    switch (k) {
    case TYPE_INT: case TYPE_I32: return "int32_t";
    case TYPE_I8:  return "int8_t";
    case TYPE_I16: return "int16_t";
    case TYPE_I64: return "int64_t";
    case TYPE_U8:  return "uint8_t";
    case TYPE_U16: return "uint16_t";
    case TYPE_U32: return "uint32_t";
    case TYPE_U64: return "uint64_t";
    case TYPE_F32: return "float";
    case TYPE_F64: return "double";
    case TYPE_BOOL: return "bool";
    case TYPE_CHAR: return "char";
    default: return NULL;
    }
}

/* TokenType (parsed primitive keyword) -> TypeKind, for declared TypeNodes. */
static TypeKind token_to_kind(TokenType t)
{
    switch (t) {
    case TOKEN_TYPE_INT:  return TYPE_INT;
    case TOKEN_TYPE_I8:   return TYPE_I8;
    case TOKEN_TYPE_I16:  return TYPE_I16;
    case TOKEN_TYPE_I32:  return TYPE_I32;
    case TOKEN_TYPE_I64:  return TYPE_I64;
    case TOKEN_TYPE_U8:   return TYPE_U8;
    case TOKEN_TYPE_U16:  return TYPE_U16;
    case TOKEN_TYPE_U32:  return TYPE_U32;
    case TOKEN_TYPE_U64:  return TYPE_U64;
    case TOKEN_TYPE_F32:  return TYPE_F32;
    case TOKEN_TYPE_F64:  return TYPE_F64;
    case TOKEN_TYPE_F16:  return TYPE_F16;
    case TOKEN_TYPE_BOOL: return TYPE_BOOL;
    case TOKEN_TYPE_CHAR: return TYPE_CHAR;
    case TOKEN_TYPE_VOID: return TYPE_VOID;
    default: return TYPE_NIL;  /* unsupported */
    }
}

/* Intel register type for Simd(elem, lanes): __m128/256/512[d|i]. NULL if the
   total width is not 128/256/512 or the element is unsupported. */
static const char *simd_reg_name(TypeKind elem, int lanes)
{
    int bits = scalar_bits(elem) * lanes;
    if (kind_is_int(elem)) {
        if (bits == 128) return "__m128i";
        if (bits == 256) return "__m256i";
        if (bits == 512) return "__m512i";
        return NULL;
    }
    if (elem == TYPE_F32) {
        if (bits == 128) return "__m128";
        if (bits == 256) return "__m256";
        if (bits == 512) return "__m512";
        return NULL;
    }
    if (elem == TYPE_F64) {
        if (bits == 128) return "__m128d";
        if (bits == 256) return "__m256d";
        if (bits == 512) return "__m512d";
        return NULL;
    }
    return NULL;  /* f16/bf16 not supported by the C emitter yet */
}

/* _mm / _mm256 / _mm512 prefix by total width. NULL if unsupported. */
static const char *reg_prefix(int bits)
{
    if (bits == 128) return "_mm";
    if (bits == 256) return "_mm256";
    if (bits == 512) return "_mm512";
    return NULL;
}

/* float element suffix ps / pd. NULL if elem is not a supported float. */
static const char *flt_suffix(TypeKind elem)
{
    if (elem == TYPE_F32) return "ps";
    if (elem == TYPE_F64) return "pd";
    return NULL;
}

/* signed/unsigned integer element op suffix epiNN / epuNN. */
static const char *int_suffix(TypeKind elem, bool unsigned_op)
{
    int b = scalar_bits(elem);
    if (unsigned_op) {
        switch (b) { case 8: return "epu8"; case 16: return "epu16";
                     case 32: return "epu32"; case 64: return "epu64"; }
    } else {
        switch (b) { case 8: return "epi8"; case 16: return "epi16";
                     case 32: return "epi32"; case 64: return "epi64"; }
    }
    return NULL;
}

static bool ce_has_struct(CE *e, const char *name)
{
    for (int i = 0; i < e->struct_count; i++)
        if (strcmp(e->structs[i], name) == 0) return true;
    return false;
}

/* Emit the C type for a declared TypeNode. Returns false (and reports) if the
   type is out of subset. */
static bool emit_type_node(CE *e, TypeNode *tn, AstNode *loc)
{
    if (tn == NULL) { ce(e, "void"); return true; }
    switch (tn->kind) {
    case TYPE_NODE_PRIMITIVE: {
        TypeKind k = token_to_kind(tn->as.primitive);
        if (k == TYPE_VOID) { ce(e, "void"); return true; }
        const char *n = c_scalar_name(k);
        if (n == NULL) { ce_error(e, loc, "type not supported by emit-c"); return false; }
        ce(e, "%s", n);
        return true;
    }
    case TYPE_NODE_POINTER:
        if (!emit_type_node(e, tn->as.pointee, loc)) return false;
        ce(e, "*");
        return true;
    case TYPE_NODE_SIMD: {
        TypeNode *el = tn->as.array.elem;
        if (el == NULL || el->kind != TYPE_NODE_PRIMITIVE) {
            ce_error(e, loc, "Simd element must be a scalar"); return false; }
        const char *r = simd_reg_name(token_to_kind(el->as.primitive), tn->as.array.size);
        if (r == NULL) { ce_error(e, loc,
            "Simd(T,%d) has no 128/256/512-bit intrinsic register (or unsupported element)",
            tn->as.array.size); return false; }
        ce(e, "%s", r);
        return true;
    }
    case TYPE_NODE_NAMED:
        if (tn->as.named.arg_count > 0) {
            ce_error(e, loc, "generic type '%s(...)' not supported by emit-c", tn->as.named.name);
            return false; }
        if (!ce_has_struct(e, tn->as.named.name)) {
            ce_error(e, loc, "type '%s' not supported by emit-c "
                     "(not a plain in-subset struct)", tn->as.named.name);
            return false; }
        ce(e, "%s", tn->as.named.name);
        return true;
    case TYPE_NODE_ARRAY:
        /* a bare array type (e.g. as a return type) cannot be written in C;
           array declarations are handled by emit_decl_node (T name[N]). */
        ce_error(e, loc, "array type is only supported in a variable/field/param "
                         "declaration by emit-c, not here");
        return false;
    default:
        ce_error(e, loc, "type not supported by emit-c (only scalars, pointers, Simd, struct)");
        return false;
    }
}

/* Emit a full C declarator "TYPE name", handling fixed arrays specially
   (the name sits between the element type and [N]: `float buf[16]`). */
static bool emit_decl_node(CE *e, TypeNode *tn, const char *name, AstNode *loc)
{
    if (tn && tn->kind == TYPE_NODE_ARRAY) {
        if (!emit_type_node(e, tn->as.array.elem, loc)) return false;
        ce(e, " %s[%d]", name, tn->as.array.size);
        return true;
    }
    if (!emit_type_node(e, tn, loc)) return false;
    ce(e, " %s", name);
    return true;
}

/* Emit the C type for a resolved Type (used for inferred var decls). */
static bool emit_type(CE *e, Type *t, AstNode *loc)
{
    if (t == NULL) { ce_error(e, loc, "missing type"); return false; }
    switch (t->kind) {
    case TYPE_VOID: ce(e, "void"); return true;
    case TYPE_POINTER:
        if (!emit_type(e, t->as.pointer_to, loc)) return false;
        ce(e, "*");
        return true;
    case TYPE_SIMD: {
        const char *r = simd_reg_name(t->as.simd.elem->kind, t->as.simd.lanes);
        if (r == NULL) { ce_error(e, loc, "Simd has no intrinsic register here"); return false; }
        ce(e, "%s", r);
        return true;
    }
    case TYPE_STRUCT: {
        const char *nm = t->as.strukt.name;
        if (nm == NULL || !ce_has_struct(e, nm)) {
            ce_error(e, loc, "struct type not supported by emit-c"); return false; }
        ce(e, "%s", nm);
        return true;
    }
    default: {
        const char *n = c_scalar_name(t->kind);
        if (n == NULL) { ce_error(e, loc, "type not supported by emit-c"); return false; }
        ce(e, "%s", n);
        return true;
    }
    }
}

/* ---- expressions ---- */

static bool emit_expr(CE *e, AstNode *n);

static const char *c_binop(TokenType op)
{
    switch (op) {
    case TOKEN_PLUS: return "+";   case TOKEN_MINUS: return "-";
    case TOKEN_STAR: return "*";   case TOKEN_SLASH: return "/";
    case TOKEN_PERCENT: return "%";
    case TOKEN_EQ: return "==";    case TOKEN_NEQ: return "!=";
    case TOKEN_LT: return "<";     case TOKEN_GT: return ">";
    case TOKEN_LEQ: return "<=";   case TOKEN_GEQ: return ">=";
    case TOKEN_AND: return "&&";   case TOKEN_OR: return "||";
    case TOKEN_AMP: return "&";    case TOKEN_PIPE: return "|";
    case TOKEN_CARET: return "^";
    case TOKEN_LSHIFT: return "<<"; case TOKEN_RSHIFT: return ">>";
    default: return NULL;
    }
}

/* A Simd binary op (a OP b, both Simd same type) -> the intrinsic call. */
static bool emit_simd_binop(CE *e, AstNode *n, const char *opname)
{
    Type *t = n->resolved_type;  /* Simd(T,N) */
    TypeKind elem = t->as.simd.elem->kind;
    int bits = scalar_bits(elem) * t->as.simd.lanes;
    const char *P = reg_prefix(bits);
    if (P == NULL) { ce_error(e, n, "unsupported Simd width for '%s'", opname); return false; }
    const char *sfx;
    if (kind_is_int(elem)) {
        if (strcmp(opname, "div") == 0) {
            ce_error(e, n, "integer Simd division has no intrinsic"); return false; }
        sfx = int_suffix(elem, false);  /* add/sub/mul/... use epi bit-pattern */
    } else {
        sfx = flt_suffix(elem);
    }
    if (sfx == NULL) { ce_error(e, n, "unsupported Simd element for '%s'", opname); return false; }
    ce(e, "%s_%s_%s(", P, opname, sfx);
    if (!emit_expr(e, n->as.binary.left)) return false;
    ce(e, ", ");
    if (!emit_expr(e, n->as.binary.right)) return false;
    ce(e, ")");
    return true;
}

/* __simd_* intrinsic call -> Intel intrinsic. */
static bool emit_simd_call(CE *e, AstNode *n)
{
    const char *name = n->as.call.callee->as.ident.name;
    AstNode **a = n->as.call.args;
    int argc = n->as.call.arg_count;

    /* The driving Simd type: result for producers/ops, arg type for store. */
    Type *st = n->resolved_type;
    if (st == NULL || st->kind != TYPE_SIMD) {
        /* store / store_masked: void result — take the vector operand's type. */
        if (strcmp(name, "__simd_store") == 0 && argc == 3) st = a[2]->resolved_type;
        else if (strcmp(name, "__simd_store_masked") == 0 && argc == 4) st = a[2]->resolved_type;
        else if (strcmp(name, "__simd_lane") == 0 && argc >= 1) st = a[0]->resolved_type;
        else if (strcmp(name, "__simd_reduce_add") == 0 && argc >= 1) st = a[0]->resolved_type;
        else if ((strcmp(name, "__simd_reduce_max") == 0 ||
                  strcmp(name, "__simd_reduce_min") == 0) && argc >= 1) st = a[0]->resolved_type;
    }
    if (st == NULL || st->kind != TYPE_SIMD) {
        ce_error(e, n, "cannot determine Simd type for %s", name); return false; }
    TypeKind elem = st->as.simd.elem->kind;
    int lanes = st->as.simd.lanes;
    int bits = scalar_bits(elem) * lanes;
    const char *P = reg_prefix(bits);
    const char *fsfx = flt_suffix(elem);
    bool isf = (fsfx != NULL);
    if (P == NULL) { ce_error(e, n, "unsupported Simd width in %s", name); return false; }

    #define EMIT_ARG(i) do { if (!emit_expr(e, a[i])) return false; } while (0)

    if (strcmp(name, "__simd_splat") == 0) {
        if (!isf) { const char *s = int_suffix(elem, false);
            ce(e, "%s_set1_%s(", P, s); }
        else ce(e, "%s_set1_%s(", P, fsfx);
        EMIT_ARG(0); ce(e, ")"); return true;
    }
    if (strcmp(name, "__simd_zero") == 0) {
        if (isf) ce(e, "%s_setzero_%s()", P, fsfx);
        else ce(e, "%s_setzero_si%d()", P, bits);
        return true;
    }
    if (strcmp(name, "__simd_load") == 0) {
        if (isf) { ce(e, "%s_loadu_%s((", P, fsfx); EMIT_ARG(0); ce(e, ")+("); EMIT_ARG(1); ce(e, "))"); }
        else { ce(e, "%s_loadu_si%d((void const*)((", P, bits); EMIT_ARG(0); ce(e, ")+("); EMIT_ARG(1); ce(e, ")))"); }
        return true;
    }
    if (strcmp(name, "__simd_store") == 0) {
        if (isf) { ce(e, "%s_storeu_%s((", P, fsfx); EMIT_ARG(0); ce(e, ")+("); EMIT_ARG(1); ce(e, "), "); EMIT_ARG(2); ce(e, ")"); }
        else { ce(e, "%s_storeu_si%d((void*)((", P, bits); EMIT_ARG(0); ce(e, ")+("); EMIT_ARG(1); ce(e, ")), "); EMIT_ARG(2); ce(e, ")"); }
        return true;
    }
    if (strcmp(name, "__simd_fma") == 0) {
        if (!isf) { ce_error(e, n, "__simd_fma requires a float Simd"); return false; }
        ce(e, "%s_fmadd_%s(", P, fsfx); EMIT_ARG(0); ce(e, ", "); EMIT_ARG(1); ce(e, ", "); EMIT_ARG(2); ce(e, ")");
        return true;
    }
    if (strcmp(name, "__simd_max") == 0 || strcmp(name, "__simd_min") == 0) {
        const char *op = (name[7] == 'm' && name[8] == 'a') ? "max" : "min";
        const char *sfx = isf ? fsfx : int_suffix(elem, kind_is_unsigned(elem));
        if (sfx == NULL) { ce_error(e, n, "unsupported element for %s", name); return false; }
        ce(e, "%s_%s_%s(", P, op, sfx); EMIT_ARG(0); ce(e, ", "); EMIT_ARG(1); ce(e, ")");
        return true;
    }
    if (strcmp(name, "__simd_reduce_add") == 0 || strcmp(name, "__simd_reduce_max") == 0 ||
        strcmp(name, "__simd_reduce_min") == 0) {
        if (bits != 512) { ce_error(e, n,
            "%s on a non-512-bit Simd is not supported by emit-c yet", name); return false; }
        const char *op = strcmp(name, "__simd_reduce_add") == 0 ? "add"
                       : strcmp(name, "__simd_reduce_max") == 0 ? "max" : "min";
        const char *sfx = isf ? fsfx : int_suffix(elem, false);
        if (sfx == NULL) { ce_error(e, n, "unsupported element for %s", name); return false; }
        ce(e, "%s_reduce_%s_%s(", P, op, sfx); EMIT_ARG(0); ce(e, ")");
        return true;
    }
    if (strcmp(name, "__simd_lane") == 0) {
        ce(e, "("); EMIT_ARG(0); ce(e, ")["); EMIT_ARG(1); ce(e, "]");
        return true;
    }
    if (strcmp(name, "__simd_load_masked") == 0) {
        if (!isf) { ce_error(e, n, "masked load supports float Simd only in emit-c"); return false; }
        ce(e, "%s_maskz_loadu_%s((__mmask%d)((1u<<(", P, fsfx, lanes); EMIT_ARG(2);
        ce(e, "))-1u), ("); EMIT_ARG(0); ce(e, ")+("); EMIT_ARG(1); ce(e, "))");
        return true;
    }
    if (strcmp(name, "__simd_store_masked") == 0) {
        if (!isf) { ce_error(e, n, "masked store supports float Simd only in emit-c"); return false; }
        ce(e, "%s_mask_storeu_%s((", P, fsfx); EMIT_ARG(0); ce(e, ")+("); EMIT_ARG(1);
        ce(e, "), (__mmask%d)((1u<<(", lanes); EMIT_ARG(3); ce(e, "))-1u), "); EMIT_ARG(2); ce(e, ")");
        return true;
    }
    ce_error(e, n, "intrinsic %s not supported by emit-c yet", name);
    return false;
    #undef EMIT_ARG
}

static bool emit_expr(CE *e, AstNode *n)
{
    if (n == NULL) { ce(e, "/*null*/0"); return true; }
    switch (n->kind) {
    case AST_INT_LIT:
        ce(e, "%lld", n->as.int_lit.value);
        return true;
    case AST_FLOAT_LIT: {
        /* keep it a C floating literal: %.17g of 2.0 is "2" (an int), which would
           turn a float context into integer arithmetic — append ".0" if needed. */
        char nb[64];
        snprintf(nb, sizeof nb, "%.17g", n->as.float_lit.value);
        ce(e, "%s", nb);
        if (!strpbrk(nb, ".eEnN")) ce(e, ".0");
        return true;
    }
    case AST_BOOL_LIT:
        ce(e, "%s", n->as.bool_lit.value ? "true" : "false");
        return true;
    case AST_IDENT:
        ce(e, "%s", n->as.ident.name);
        return true;
    case AST_UNARY: {
        if (n->resolved_type && n->resolved_type->kind == TYPE_SIMD) {
            ce_error(e, n, "unary op on Simd not supported by emit-c"); return false; }
        const char *op = n->as.unary.op == TOKEN_MINUS ? "-"
                       : n->as.unary.op == TOKEN_BANG ? "!" : NULL;
        if (op == NULL) { ce_error(e, n, "unsupported unary operator"); return false; }
        ce(e, "(%s", op);
        if (!emit_expr(e, n->as.unary.operand)) return false;
        ce(e, ")");
        return true;
    }
    case AST_BINARY: {
        if (n->as.binary.lowered) {  /* operator overload on a user type */
            ce_error(e, n, "operator overloading not supported by emit-c"); return false; }
        bool simd = n->resolved_type && n->resolved_type->kind == TYPE_SIMD;
        if (simd) {
            switch (n->as.binary.op) {
            case TOKEN_PLUS:  return emit_simd_binop(e, n, "add");
            case TOKEN_MINUS: return emit_simd_binop(e, n, "sub");
            case TOKEN_STAR:  return emit_simd_binop(e, n, "mul");
            case TOKEN_SLASH: return emit_simd_binop(e, n, "div");
            default: ce_error(e, n, "operator not supported on Simd by emit-c"); return false;
            }
        }
        const char *op = c_binop(n->as.binary.op);
        if (op == NULL) { ce_error(e, n, "unsupported binary operator"); return false; }
        ce(e, "(");
        if (!emit_expr(e, n->as.binary.left)) return false;
        ce(e, " %s ", op);
        if (!emit_expr(e, n->as.binary.right)) return false;
        ce(e, ")");
        return true;
    }
    case AST_CALL: {
        AstNode *callee = n->as.call.callee;
        if (callee->kind != AST_IDENT) {
            ce_error(e, n, "only direct function / intrinsic calls are supported by emit-c");
            return false;
        }
        const char *name = callee->as.ident.name;
        if (strncmp(name, "__simd_", 7) == 0) return emit_simd_call(e, n);
        /* must be one of the file's own functions */
        bool local = false;
        for (int i = 0; i < e->fn_count; i++)
            if (strcmp(e->fns[i], name) == 0) { local = true; break; }
        if (!local) {
            ce_error(e, n, "call to '%s' not supported by emit-c (only same-file "
                           "functions and __simd_* intrinsics)", name);
            return false;
        }
        ce(e, "%s(", name);
        for (int i = 0; i < n->as.call.arg_count; i++) {
            if (i) ce(e, ", ");
            if (!emit_expr(e, n->as.call.args[i])) return false;
        }
        ce(e, ")");
        return true;
    }
    case AST_INDEX: {
        if (n->as.index_expr.index == NULL) {
            ce_error(e, n, "multi-subscript indexing not supported by emit-c"); return false; }
        ce(e, "(");
        if (!emit_expr(e, n->as.index_expr.object)) return false;
        ce(e, ")[");
        if (!emit_expr(e, n->as.index_expr.index)) return false;
        ce(e, "]");
        return true;
    }
    case AST_CAST: {
        if (n->resolved_type && n->resolved_type->kind == TYPE_SIMD) {
            ce_error(e, n, "Simd cast: use __simd_cast (not supported by emit-c yet)"); return false; }
        ce(e, "((");
        if (!emit_type(e, n->resolved_type, n)) return false;
        ce(e, ")(");
        if (!emit_expr(e, n->as.cast.expr)) return false;
        ce(e, "))");
        return true;
    }
    case AST_FIELD: {
        AstNode *obj = n->as.field_access.object;
        bool is_ptr = obj->resolved_type && obj->resolved_type->kind == TYPE_POINTER;
        ce(e, "(");
        if (!emit_expr(e, obj)) return false;
        ce(e, is_ptr ? ")->%s" : ").%s", n->as.field_access.field);
        return true;
    }
    case AST_NEW_EXPR: {
        if (!n->as.new_expr.on_stack) {
            ce_error(e, n, "heap `new` is not supported by emit-c (no allocator)"); return false; }
        if (n->as.new_expr.struct_name == NULL || !ce_has_struct(e, n->as.new_expr.struct_name)) {
            ce_error(e, n, "struct literal of an unsupported type"); return false; }
        ce(e, "(%s){", n->as.new_expr.struct_name);
        int fc = n->as.new_expr.field_init_count;
        if (fc == 0) {
            ce(e, "0");   /* zero-initializer */
        } else {
            for (int i = 0; i < fc; i++) {
                if (i) ce(e, ", ");
                ce(e, ".%s = ", n->as.new_expr.field_inits[i].name);
                if (!emit_expr(e, n->as.new_expr.field_inits[i].value)) return false;
            }
        }
        ce(e, "}");
        return true;
    }
    case AST_SIZEOF: {
        ce(e, "sizeof(");
        Type *t = n->as.sizeof_expr.sized_type;
        if (t) { if (!emit_type(e, t, n)) return false; }
        else if (!emit_type_node(e, n->as.sizeof_expr.type_node, n)) return false;
        ce(e, ")");
        return true;
    }
    default:
        ce_error(e, n, "expression not supported by emit-c");
        return false;
    }
}

/* ---- statements ---- */

static bool emit_block(CE *e, AstNode *blk, int depth);

static bool emit_stmt(CE *e, AstNode *n, int depth)
{
    if (n == NULL) return true;
    switch (n->kind) {
    case AST_VAR_DECL: {
        ce_indent(e, depth);
        if (n->as.var_decl.var_type) {
            if (!emit_decl_node(e, n->as.var_decl.var_type, n->as.var_decl.name, n)) return false;
        } else if (n->as.var_decl.init) {
            if (!emit_type(e, n->as.var_decl.init->resolved_type, n)) return false;
            ce(e, " %s", n->as.var_decl.name);
        } else {
            ce_error(e, n, "var decl needs a type or initializer"); return false;
        }
        if (n->as.var_decl.init) {
            ce(e, " = ");
            if (!emit_expr(e, n->as.var_decl.init)) return false;
        }
        ce(e, ";\n");
        return true;
    }
    case AST_ASSIGN: {
        ce_indent(e, depth);
        if (!emit_expr(e, n->as.assign.target)) return false;
        const char *op;
        switch (n->as.assign.op) {
        case TOKEN_ASSIGN: op = "="; break;
        case TOKEN_PLUS_ASSIGN: op = "+="; break;
        case TOKEN_MINUS_ASSIGN: op = "-="; break;
        case TOKEN_STAR_ASSIGN: op = "*="; break;
        case TOKEN_SLASH_ASSIGN: op = "/="; break;
        default: ce_error(e, n, "unsupported assignment operator"); return false;
        }
        ce(e, " %s ", op);
        if (!emit_expr(e, n->as.assign.value)) return false;
        ce(e, ";\n");
        return true;
    }
    case AST_RETURN:
        ce_indent(e, depth);
        if (n->as.return_stmt.value) {
            ce(e, "return ");
            if (!emit_expr(e, n->as.return_stmt.value)) return false;
            ce(e, ";\n");
        } else {
            ce(e, "return;\n");
        }
        return true;
    case AST_EXPR_STMT:
        ce_indent(e, depth);
        if (!emit_expr(e, n->as.expr_stmt.expr)) return false;
        ce(e, ";\n");
        return true;
    case AST_IF:
        ce_indent(e, depth);
        ce(e, "if (");
        if (!emit_expr(e, n->as.if_stmt.cond)) return false;
        ce(e, ") ");
        if (!emit_block(e, n->as.if_stmt.then_block, depth)) return false;
        if (n->as.if_stmt.else_block) {
            ce(e, " else ");
            if (n->as.if_stmt.else_block->kind == AST_IF) {
                /* else if: emit inline without a wrapping block */
                if (!emit_stmt(e, n->as.if_stmt.else_block, 0)) return false;
            } else {
                if (!emit_block(e, n->as.if_stmt.else_block, depth)) return false;
                ce(e, "\n");
            }
        } else {
            ce(e, "\n");
        }
        return true;
    case AST_WHILE:
        ce_indent(e, depth);
        ce(e, "while (");
        if (!emit_expr(e, n->as.while_stmt.cond)) return false;
        ce(e, ") ");
        if (!emit_block(e, n->as.while_stmt.body, depth)) return false;
        ce(e, "\n");
        return true;
    case AST_FOR: {
        /* for x in a..b  ->  for (int x = a; x < b; x++) */
        if (n->as.for_stmt.desugared) {
            ce_error(e, n, "for-in over a container is not supported by emit-c "
                           "(use a range or a C for-loop)"); return false; }
        AstNode *it = n->as.for_stmt.iter;
        if (it == NULL || it->kind != AST_RANGE) {
            ce_error(e, n, "for-in supports only integer ranges (a..b) in emit-c"); return false; }
        const char *v = n->as.for_stmt.var;
        ce_indent(e, depth);
        ce(e, "for (int32_t %s = ", v);
        if (!emit_expr(e, it->as.range.start)) return false;
        ce(e, "; %s < ", v);
        if (!emit_expr(e, it->as.range.end)) return false;
        ce(e, "; %s++) ", v);
        if (!emit_block(e, n->as.for_stmt.body, depth)) return false;
        ce(e, "\n");
        return true;
    }
    case AST_FOR_C:
        ce_indent(e, depth);
        ce(e, "for (");
        /* init is a stmt (var decl / assign / expr); emit without indent/newline */
        if (n->as.for_c_stmt.init) {
            /* reuse expr/decl emit but inline — only simple forms */
            AstNode *ini = n->as.for_c_stmt.init;
            if (ini->kind == AST_VAR_DECL) {
                if (ini->as.var_decl.var_type) {
                    if (!emit_decl_node(e, ini->as.var_decl.var_type, ini->as.var_decl.name, ini)) return false;
                } else {
                    if (ini->as.var_decl.init && !emit_type(e, ini->as.var_decl.init->resolved_type, ini)) return false;
                    ce(e, " %s", ini->as.var_decl.name);
                }
                if (ini->as.var_decl.init) { ce(e, " = "); if (!emit_expr(e, ini->as.var_decl.init)) return false; }
            } else if (ini->kind == AST_ASSIGN) {
                if (!emit_expr(e, ini->as.assign.target)) return false;
                ce(e, " = ");
                if (!emit_expr(e, ini->as.assign.value)) return false;
            } else if (ini->kind == AST_EXPR_STMT) {
                if (!emit_expr(e, ini->as.expr_stmt.expr)) return false;
            }
        }
        ce(e, "; ");
        if (n->as.for_c_stmt.cond && !emit_expr(e, n->as.for_c_stmt.cond)) return false;
        ce(e, "; ");
        if (n->as.for_c_stmt.update) {
            AstNode *u = n->as.for_c_stmt.update;
            if (u->kind == AST_ASSIGN) {
                if (!emit_expr(e, u->as.assign.target)) return false;
                ce(e, " = ");
                if (!emit_expr(e, u->as.assign.value)) return false;
            } else if (u->kind == AST_EXPR_STMT) {
                if (!emit_expr(e, u->as.expr_stmt.expr)) return false;
            }
        }
        ce(e, ") ");
        if (!emit_block(e, n->as.for_c_stmt.body, depth)) return false;
        ce(e, "\n");
        return true;
    case AST_BREAK:    ce_indent(e, depth); ce(e, "break;\n"); return true;
    case AST_CONTINUE: ce_indent(e, depth); ce(e, "continue;\n"); return true;
    case AST_BLOCK:
        ce_indent(e, depth);
        return emit_block(e, n, depth);
    default:
        ce_error(e, n, "statement not supported by emit-c");
        return false;
    }
}

static bool emit_block(CE *e, AstNode *blk, int depth)
{
    if (blk == NULL) { ce(e, "{ }"); return true; }
    if (blk->kind != AST_BLOCK) return emit_stmt(e, blk, depth);
    ce(e, "{\n");
    for (int i = 0; i < blk->as.block.stmt_count; i++)
        if (!emit_stmt(e, blk->as.block.stmts[i], depth + 1)) return false;
    ce_indent(e, depth);
    ce(e, "}");
    return true;
}

/* ---- function ---- */

static bool emit_fn(CE *e, AstNode *fn)
{
    if (fn->as.fn_decl.impl_struct_name) {
        ce_error(e, fn, "methods are not supported by emit-c (free functions only)"); return false; }
    if (fn->as.fn_decl.type_param_count > 0) {
        ce_error(e, fn, "generic functions are not supported by emit-c"); return false; }
    /* return type */
    if (!emit_type_node(e, fn->as.fn_decl.return_type, fn)) return false;
    ce(e, " %s(", fn->as.fn_decl.name);
    int pc = fn->as.fn_decl.param_count;
    if (pc == 0) ce(e, "void");
    for (int i = 0; i < pc; i++) {
        if (i) ce(e, ", ");
        if (!emit_decl_node(e, fn->as.fn_decl.param_types[i],
                            fn->as.fn_decl.param_names[i], fn)) return false;
    }
    ce(e, ") ");
    if (!emit_block(e, fn->as.fn_decl.body, 0)) return false;
    ce(e, "\n\n");
    return true;
}

/* ---- entry ---- */

static bool name_in_list(const char *name, const char **list, int count)
{
    for (int i = 0; i < count; i++) if (strcmp(list[i], name) == 0) return true;
    return false;
}

/* Is this declared type in-subset (no emission/error)? Used to decide whether a
   struct is POD/emittable. `names` is the set of all top-level struct names so a
   field whose type is another struct resolves. */
static bool tn_type_ok(TypeNode *tn, const char **names, int ncount)
{
    if (tn == NULL) return false;
    switch (tn->kind) {
    case TYPE_NODE_PRIMITIVE: return c_scalar_name(token_to_kind(tn->as.primitive)) != NULL;
    case TYPE_NODE_POINTER:   return tn_type_ok(tn->as.pointee, names, ncount);
    case TYPE_NODE_ARRAY:     return tn_type_ok(tn->as.array.elem, names, ncount);
    case TYPE_NODE_SIMD: {
        TypeNode *el = tn->as.array.elem;
        return el && el->kind == TYPE_NODE_PRIMITIVE &&
               simd_reg_name(token_to_kind(el->as.primitive), tn->as.array.size) != NULL;
    }
    case TYPE_NODE_NAMED:
        return tn->as.named.arg_count == 0 && name_in_list(tn->as.named.name, names, ncount);
    default: return false;
    }
}

int emit_c(AstNode *program, const char *out_path, const char *src_path,
           const EmitCOpts *opts)
{
    if (program == NULL || program->kind != AST_PROGRAM) {
        fprintf(stderr, "[emit-c] internal: not a program\n");
        return 1;
    }
    EmitCOpts none = {0};
    if (opts == NULL) opts = &none;
    bool selective = (opts->only != NULL && opts->only_count > 0);
    /* skip mode applies only to "emit all"; an explicitly-named fn must emit. */
    bool skip = opts->skip_unsupported && !selective;

    CE e = {0};
    e.path = src_path;  /* diagnostics reference the source file */

    /* collect this file's top-level function names (allowed call targets). */
    int n = program->as.program.decl_count;
    e.fns = malloc(sizeof(char *) * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d->kind == AST_FN_DECL && d->as.fn_decl.impl_struct_name == NULL)
            e.fns[e.fn_count++] = d->as.fn_decl.name;
    }

    /* collect emittable (POD, in-subset) top-level struct names: usable as C
       types. First gather all struct names (so a struct field of struct type
       resolves), then keep those whose every field is in-subset. */
    const char **allstructs = malloc(sizeof(char *) * (n > 0 ? n : 1));
    int allc = 0;
    for (int i = 0; i < n; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d->kind == AST_STRUCT_DECL && d->as.struct_decl.type_param_count == 0)
            allstructs[allc++] = d->as.struct_decl.name;
    }
    e.structs = malloc(sizeof(char *) * (allc > 0 ? allc : 1));
    for (int i = 0; i < n; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d->kind != AST_STRUCT_DECL || d->as.struct_decl.type_param_count != 0) continue;
        bool ok = true;
        for (int f = 0; f < d->as.struct_decl.field_count; f++)
            if (!tn_type_ok(d->as.struct_decl.field_types[f], allstructs, allc)) { ok = false; break; }
        if (ok) e.structs[e.struct_count++] = d->as.struct_decl.name;
    }

    /* with --only, verify every requested name exists in the file. */
    if (selective) {
        for (int i = 0; i < opts->only_count; i++) {
            if (!name_in_list(opts->only[i], e.fns, e.fn_count)) {
                fprintf(stderr, "[emit-c] --only: no function '%s' in %s\n",
                        opts->only[i], src_path ? src_path : "?");
                free(e.fns);
                free(e.structs);
                free(allstructs);
                free(e.buf);
                return 1;
            }
        }
    }

    ce(&e, "/* Generated by `ls emit-c` — Intel intrinsics for the LS numeric/SIMD\n"
           "   kernel subset. Compile with e.g. clang -march=graniterapids -O3. */\n");
    ce(&e, "#include <immintrin.h>\n#include <stdint.h>\n#include <stdbool.h>\n\n");

    /* struct typedefs (in declaration order) for the emittable POD structs. */
    for (int i = 0; i < n && !e.err; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d->kind != AST_STRUCT_DECL) continue;
        if (!ce_has_struct(&e, d->as.struct_decl.name)) continue;
        ce(&e, "typedef struct %s {\n", d->as.struct_decl.name);
        for (int f = 0; f < d->as.struct_decl.field_count; f++) {
            ce_indent(&e, 1);
            if (!emit_decl_node(&e, d->as.struct_decl.field_types[f],
                                d->as.struct_decl.field_names[f], d)) break;
            ce(&e, ";\n");
        }
        ce(&e, "} %s;\n\n", d->as.struct_decl.name);
    }

    int emitted = 0, skipped = 0;
    for (int i = 0; i < n; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d->kind != AST_FN_DECL) continue;  /* skip imports / type decls */
        if (selective && !name_in_list(d->as.fn_decl.name, opts->only, opts->only_count))
            continue;
        size_t mark = e.len;
        e.err = false;                 /* fresh attempt per function */
        if (!emit_fn(&e, d) || e.err) {
            if (skip) {
                /* roll back this function's partial output and continue. */
                e.len = mark; e.buf[e.len] = '\0'; e.err = false;
                fprintf(stderr, "[emit-c] skipped '%s' (out of subset)\n",
                        d->as.fn_decl.name);
                skipped++;
                continue;
            }
            break;                     /* default / --only: hard fail */
        }
        emitted++;
    }

    free(e.fns);
    free(e.structs);
    free(allstructs);
    if (e.err) { free(e.buf); return 1; }
    if (emitted == 0) {
        fprintf(stderr, "[emit-c] no emittable functions in %s%s\n",
                src_path ? src_path : "the file",
                skipped ? " (all candidates were out of subset)" : "");
        free(e.buf);
        return 1;
    }

    FILE *f = fopen(out_path, "wb");
    if (f == NULL) { fprintf(stderr, "[emit-c] cannot open %s for writing\n", out_path); free(e.buf); return 1; }
    fwrite(e.buf, 1, e.len, f);
    fclose(f);
    free(e.buf);
    if (skipped)
        fprintf(stderr, "[emit-c] wrote %d function(s) to %s (%d skipped)\n",
                emitted, out_path, skipped);
    else
        fprintf(stderr, "[emit-c] wrote %d function(s) to %s\n", emitted, out_path);
    return 0;
}
