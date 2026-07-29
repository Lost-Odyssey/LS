/* checker_derive.c — @derive(...) source-text generator
   (docs/plan_static_reflection.md Stage 1).

   Moved verbatim out of checker.c (Batch 4, Task 4.2,
   docs/plan_arch_round2_backlog.md): a self-contained subsystem that expands
   `@derive(Trait, ...)` on a struct/enum into synthesized `methods Type:
   Trait { ... }` impls by generating LS source text (DeriveBuf + db_* string
   builders + derive_emit_*) and re-parsing it (no manual AST construction),
   then appending the resulting impl decls to the program (expand_derives).
   The normal check + codegen pipeline handles the synthesized impls after
   that — zero new codegen.

   External call sites (both remain in checker.c): checker_inspect_ex and
   checker_check call expand_derives once per top-level check, before
   forward_pass. The export surface in checker_internal.h is exactly
   expand_derives plus method_display_name (a small shared string-mapping
   helper also used by `ls inspect`'s method listing, still defined in
   checker.c). */
#include "checker_internal.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Stage 1 (docs/plan_static_reflection.md): @derive(...) expansion ----
   Expand @derive(Trait, ...) on a struct/enum into synthesized
   `methods Type: Trait { ... }` impls by generating source text and re-parsing
   it (no manual AST construction), then appending the impl decls to the program.
   The normal check + codegen pipeline handles them — zero new codegen.
   v1: Equal for structs. Other traits / enum targets report a clear error. */

typedef struct { char *data; size_t len, cap; } DeriveBuf;

static void db_init(DeriveBuf *b) {
    b->cap = 256; b->len = 0;
    b->data = malloc_safe(b->cap); b->data[0] = '\0';
}
static void db_puts(DeriveBuf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->data = realloc_safe(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
}
static void db_putint(DeriveBuf *b, int v) {
    char t[24]; snprintf(t, sizeof t, "%d", v); db_puts(b, t);
}
/* Emit a binder list "(p0pfx, p1pfx, ...)" for `n` payload slots; nothing if n==0. */
static void db_binders(DeriveBuf *b, const char *pfx, int n) {
    if (n <= 0) return;
    db_puts(b, "(");
    for (int i = 0; i < n; i++) {
        if (i) db_puts(b, ", ");
        db_puts(b, pfx); db_putint(b, i);
    }
    db_puts(b, ")");
}

/* Emit one field's Value-constructing expression for @derive(Serialize):
   Int/Float/Bool leaves for primitives, Text for Str, recurse via .to_value()
   for nested struct/enum fields. */
static void derive_emit_serialize_field(DeriveBuf *sb, const char *fname, TypeNode *ft) {
    if (ft != NULL && ft->kind == TYPE_NODE_PRIMITIVE) {
        TokenType p = ft->as.primitive;
        if (p == TOKEN_TYPE_F32 || p == TOKEN_TYPE_F64) {
            db_puts(sb, "VFloat(self."); db_puts(sb, fname); db_puts(sb, " as f64)");
        } else if (p == TOKEN_TYPE_BOOL) {
            db_puts(sb, "VBool(self."); db_puts(sb, fname); db_puts(sb, ")");
        } else {   /* int family + char */
            db_puts(sb, "VInt(self."); db_puts(sb, fname); db_puts(sb, " as i64)");
        }
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name &&
               strcmp(ft->as.named.name, "Str") == 0) {
        db_puts(sb, "VStr(self."); db_puts(sb, fname); db_puts(sb, ".copy())");
    } else {   /* nested struct/enum: recurse (requires its own Serialize) */
        db_puts(sb, "self."); db_puts(sb, fname); db_puts(sb, ".to_value()");
    }
}

/* Primitive TypeNode token -> LS type-name keyword (for casts in Deserialize). */
static const char *prim_type_name(TokenType p) {
    switch (p) {
        case TOKEN_TYPE_I8:  return "i8";
        case TOKEN_TYPE_I16: return "i16";
        case TOKEN_TYPE_I32: return "i32";
        case TOKEN_TYPE_I64: return "i64";
        case TOKEN_TYPE_U8:  return "u8";
        case TOKEN_TYPE_U16: return "u16";
        case TOKEN_TYPE_U32: return "u32";
        case TOKEN_TYPE_U64: return "u64";
        case TOKEN_TYPE_CHAR: return "char";
        default: return "int";
    }
}

/* Emit one field's extraction expression for @derive(Deserialize):
   as_int/as_f64/as_bool/as_str(obj_get(v, "f")) for leaves (cast to the field's
   exact type), or <Type>.from_value(obj_get(v, "f")) for nested struct fields. */
static void derive_emit_deserialize_field(DeriveBuf *sb, const char *fname, TypeNode *ft) {
    /* value.ls helpers are reached by canonical path (free functions aren't
       visible unqualified across modules, same as std.core.hash.fx_mix). */
    const char *G = "std.core.value.obj_get(v, \"";
    if (ft != NULL && ft->kind == TYPE_NODE_PRIMITIVE) {
        TokenType p = ft->as.primitive;
        if (p == TOKEN_TYPE_F32 || p == TOKEN_TYPE_F64) {
            db_puts(sb, "std.core.value.as_f64("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
            if (p == TOKEN_TYPE_F32) db_puts(sb, " as f32");
        } else if (p == TOKEN_TYPE_BOOL) {
            db_puts(sb, "std.core.value.as_bool("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
        } else {
            db_puts(sb, "std.core.value.as_int("); db_puts(sb, G); db_puts(sb, fname);
            db_puts(sb, "\")) as "); db_puts(sb, prim_type_name(p));
        }
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name &&
               strcmp(ft->as.named.name, "Str") == 0) {
        db_puts(sb, "std.core.value.as_str("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name) {
        db_puts(sb, ft->as.named.name); db_puts(sb, ".from_value(");
        db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    } else {
        /* unsupported (Vec/Map/array/...) — v1 covers primitive/Str/nested-struct. */
        db_puts(sb, "std.core.value.as_int("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    }
}

/* Emit one field's STRICT extraction for @derive(Deserialize)'s try_from_value:
   `(try std.core.value.try_as_int(obj_get(v,"f"))) as <type>` for primitive leaves,
   `try <Type>.try_from_value(obj_get(v,"f"))` for Str / nested struct / type-param
   fields — `try` propagates the first Err (missing field via VNull, or type mismatch)
   out of try_from_value. */
static void derive_emit_try_deserialize_field(DeriveBuf *sb, const char *fname, TypeNode *ft) {
    const char *G = "std.core.value.obj_get(v, \"";
    if (ft != NULL && ft->kind == TYPE_NODE_PRIMITIVE) {
        TokenType p = ft->as.primitive;
        if (p == TOKEN_TYPE_F32 || p == TOKEN_TYPE_F64) {
            db_puts(sb, "(try std.core.value.try_as_f64("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
            if (p == TOKEN_TYPE_F32) db_puts(sb, " as f32");
        } else if (p == TOKEN_TYPE_BOOL) {
            db_puts(sb, "(try std.core.value.try_as_bool("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
        } else {
            db_puts(sb, "(try std.core.value.try_as_int("); db_puts(sb, G); db_puts(sb, fname);
            db_puts(sb, "\"))) as "); db_puts(sb, prim_type_name(p));
        }
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name &&
               strcmp(ft->as.named.name, "Str") == 0) {
        db_puts(sb, "(try std.core.value.try_as_str("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
    } else if (ft != NULL && ft->kind == TYPE_NODE_NAMED && ft->as.named.name) {
        db_puts(sb, "try "); db_puts(sb, ft->as.named.name); db_puts(sb, ".try_from_value(");
        db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\"))");
    } else {
        db_puts(sb, "(try std.core.value.try_as_int("); db_puts(sb, G); db_puts(sb, fname); db_puts(sb, "\")))");
    }
}

/* Append a TypeNode's display string (for @derive(Reflect) field/param types). */
static void derive_emit_typename(DeriveBuf *sb, TypeNode *ft) {
    if (ft == NULL) { db_puts(sb, "void"); return; }
    switch (ft->kind) {
        case TYPE_NODE_PRIMITIVE: {
            TokenType p = ft->as.primitive;
            switch (p) {
                case TOKEN_TYPE_INT:  db_puts(sb, "int"); break;
                case TOKEN_TYPE_I8:   db_puts(sb, "i8"); break;
                case TOKEN_TYPE_I16:  db_puts(sb, "i16"); break;
                case TOKEN_TYPE_I32:  db_puts(sb, "i32"); break;
                case TOKEN_TYPE_I64:  db_puts(sb, "i64"); break;
                case TOKEN_TYPE_U8:   db_puts(sb, "u8"); break;
                case TOKEN_TYPE_U16:  db_puts(sb, "u16"); break;
                case TOKEN_TYPE_U32:  db_puts(sb, "u32"); break;
                case TOKEN_TYPE_U64:  db_puts(sb, "u64"); break;
                case TOKEN_TYPE_F32:  db_puts(sb, "f32"); break;
                case TOKEN_TYPE_F64:  db_puts(sb, "f64"); break;
                case TOKEN_TYPE_BOOL: db_puts(sb, "bool"); break;
                case TOKEN_TYPE_CHAR: db_puts(sb, "char"); break;
                default: db_puts(sb, "?"); break;
            }
            break;
        }
        case TYPE_NODE_NAMED:
            db_puts(sb, ft->as.named.name ? ft->as.named.name : "?");
            if (ft->as.named.arg_count > 0) {
                db_puts(sb, "(");
                for (int i = 0; i < ft->as.named.arg_count; i++) {
                    if (i) db_puts(sb, ", ");
                    derive_emit_typename(sb, ft->as.named.args[i]);
                }
                db_puts(sb, ")");
            }
            break;
        case TYPE_NODE_POINTER:
            db_puts(sb, "*"); derive_emit_typename(sb, ft->as.pointee);
            break;
        case TYPE_NODE_REFERENCE:
            db_puts(sb, ft->is_mut ? "&!" : "&");
            derive_emit_typename(sb, ft->as.pointee);
            break;
        default:
            db_puts(sb, "?");
            break;
    }
}

/* Append a method's signature string for @derive(Reflect), e.g.
   "def area(&self) -> int". Built from the AST_FN_DECL (no quotes/backslashes
   appear in type/method names, so it is safe inside a "..." source literal). */
static void derive_emit_method_sig(DeriveBuf *sb, AstNode *fn) {
    db_puts(sb, "def "); db_puts(sb, method_display_name(fn->as.fn_decl.name)); db_puts(sb, "(");
    bool first = true;
    if (!fn->as.fn_decl.is_static) {
        int sbk = fn->as.fn_decl.self_borrow_kind;
        db_puts(sb, sbk == 2 ? "&!self" : (sbk == 1 ? "&self" : "self"));
        first = false;
    }
    for (int i = 0; i < fn->as.fn_decl.param_count; i++) {
        if (!first) db_puts(sb, ", ");
        derive_emit_typename(sb, fn->as.fn_decl.param_types[i]);
        first = false;
    }
    db_puts(sb, ")");
    if (fn->as.fn_decl.return_type != NULL) {
        db_puts(sb, " -> ");
        derive_emit_typename(sb, fn->as.fn_decl.return_type);
    }
}

/* Emit the synthesized impl(s) for one struct's derives into sb. `program` is
   used by @derive(Reflect) to scan for the struct's methods. */
/* Append the type-param list "(T, U)" when the struct is generic, else nothing. */
static void db_tparams(DeriveBuf *sb, AstNode *d) {
    int n = d->as.struct_decl.type_param_count;
    if (n <= 0) return;
    db_puts(sb, "(");
    for (int i = 0; i < n; i++) {
        if (i) db_puts(sb, ", ");
        db_puts(sb, d->as.struct_decl.type_params[i]);
    }
    db_puts(sb, ")");
}
/* Append the (possibly parameterized) self type: "Box" or "Box(T, U)". */
static void db_selfty(DeriveBuf *sb, AstNode *d) {
    db_puts(sb, d->as.struct_decl.name);
    db_tparams(sb, d);
}
/* Open a synthesized impl: `methods[(T,U)] Name[(T,U)]: Trait {`.
   For a generic struct, emit the type-param list. No `where` clause — the
   `methods Type: Interface` grammar doesn't accept one; the per-field operations
   in the body (e.g. `self.f == rhs.f`) enforce the bound at monomorphization, so
   instantiating with a T that lacks the trait fails there with a clear error.
   (`bound` is retained for call-site readability but currently unused.) */
static void db_methods_open(DeriveBuf *sb, AstNode *d, const char *trait, bool bound) {
    (void)bound;
    /* New syntax: type params ride on the receiver — `methods Name(T): Trait`
       (db_selfty already emits `Name(T)`), not a leading `methods(T)` list. */
    db_puts(sb, "methods ");
    db_selfty(sb, d);
    db_puts(sb, ": ");
    db_puts(sb, trait);
    db_puts(sb, " {\n");
}

static void derive_emit_struct(Checker *c, DeriveBuf *sb, AstNode *d, AstNode *program) {
    const char *T = d->as.struct_decl.name;
    int fc = d->as.struct_decl.field_count;
    char **fname = d->as.struct_decl.field_names;

    /* A generic interface-impl folds into the struct's inherent `methods Name(T)`
       block, which must appear BEFORE it in decl order. expand_derives inserts
       these synthesized impls AFTER any user inherent block (look-ahead), so the
       fold anchor exists when the user wrote one. Only when the user wrote NO
       inherent block do we synthesize an empty one here (emitting one when the
       user has one would be a duplicate that breaks registration).
       (Non-generic impls have no such requirement.) */
    if (d->as.struct_decl.type_param_count > 0) {
        bool has_inherent = false;
        if (program != NULL && program->kind == AST_PROGRAM) {
            for (int di = 0; di < program->as.program.decl_count; di++) {
                AstNode *pd = program->as.program.decls[di];
                if (pd && pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                    pd->as.impl_decl.type_param_count > 0 &&
                    strcmp(pd->as.impl_decl.name, T) == 0) { has_inherent = true; break; }
            }
        }
        if (!has_inherent) {
            db_puts(sb, "methods "); db_selfty(sb, d); db_puts(sb, " {\n}\n");
        }
    }

    for (int k = 0; k < d->as.struct_decl.derive_count; k++) {
        const char *tr = d->as.struct_decl.derives[k];
        /* All seven struct derives work on generics. Equal/Hash/Order lower to
           uniform operators / .hash(). Show/Serialize/Deserialize dispatch a
           type-parameter field T to `self.f.show()` / `self.f.to_value()` /
           `T.from_value(...)`; the monomorphized call resolves on the concrete T
           because std.core.{show,value,str} provide .show()/.to_value()/from_value()
           for every primitive + Str (the concrete-struct path still formats
           primitives inline and never calls those). The generic derive adds no
           `where T: Trait` bound — instantiating with a T that lacks the operation
           fails at monomorphization with a clear missing-method error. */
        if (strcmp(tr, "Equal") == 0) {
            db_methods_open(sb, d, "Equal", true);
            db_puts(sb, "  def ==(&self, &"); db_selfty(sb, d);
            db_puts(sb, " rhs) -> bool {\n    return ");
            if (fc == 0) {
                db_puts(sb, "true");
            } else {
                for (int i = 0; i < fc; i++) {
                    if (i) db_puts(sb, " && ");
                    db_puts(sb, "self."); db_puts(sb, fname[i]);
                    db_puts(sb, " == rhs."); db_puts(sb, fname[i]);
                }
            }
            db_puts(sb, "\n  }\n}\n");
        } else if (strcmp(tr, "Hash") == 0) {
            /* fold each field's .hash() through FxHash's fx_mix (canonical path
               so no import alias is needed; .hash() resolves via std.core.hash). */
            db_methods_open(sb, d, "Hash", true);
            db_puts(sb, "  def hash(&self) -> u64 {\n    u64 h = 0 as u64\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    h = std.core.hash.fx_mix(h, self.");
                db_puts(sb, fname[i]); db_puts(sb, ".hash())\n");
            }
            db_puts(sb, "    return h\n  }\n}\n");
        } else if (strcmp(tr, "Order") == 0) {
            /* lexicographic: first differing field decides; `> <= >=` are
               auto-derived from `<` by the operator-overload machinery. */
            db_methods_open(sb, d, "Order", true);
            db_puts(sb, "  def <(&self, &"); db_selfty(sb, d);
            db_puts(sb, " rhs) -> bool {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    if self."); db_puts(sb, fname[i]);
                db_puts(sb, " != rhs."); db_puts(sb, fname[i]);
                db_puts(sb, " { return self."); db_puts(sb, fname[i]);
                db_puts(sb, " < rhs."); db_puts(sb, fname[i]); db_puts(sb, " }\n");
            }
            db_puts(sb, "    return false\n  }\n}\n");
        } else if (strcmp(tr, "Show") == 0) {
            /* def show(&self, &!Sink out) { out.write("T {"); out.write(" f: ");
               self.f.show(&!out); ...; out.write(" }") } — every field renders via
               its OWN Show impl (primitives/Str/nested all impl Show), writing into
               the shared sink with no intermediate Str. Output is byte-identical to
               the old `-> Str` form. Stage 3, docs/plan_print_sink.md. */
            db_methods_open(sb, d, "Show", true);
            db_puts(sb, "  def show(&self, &!Sink out) {\n    out.write(\"");
            db_puts(sb, T); db_puts(sb, " {\")\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    out.write(\"");
                db_puts(sb, (i == 0) ? " " : ", ");
                db_puts(sb, fname[i]); db_puts(sb, ": \")\n");
                db_puts(sb, "    self."); db_puts(sb, fname[i]);
                db_puts(sb, ".show(&!out)\n");
            }
            db_puts(sb, (fc > 0) ? "    out.write(\" }\")\n"
                                 : "    out.write(\"}\")\n");
            db_puts(sb, "  }\n}\n");
        } else if (strcmp(tr, "Serialize") == 0) {
            /* def to_value(&self) -> Value { build parallel keys/vals vecs, then
               return VObj(__k, __v) } — a neutral value tree (std.core.value).
               Per-field via VInt/VFloat/VBool/VStr leaves or recursive .to_value(). */
            db_methods_open(sb, d, "Serialize", true);
            db_puts(sb, "  def to_value(&self) -> Value {\n");
            db_puts(sb, "    Vec(Str) __k = []\n    Vec(Value) __v = []\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    __k.push(\""); db_puts(sb, fname[i]); db_puts(sb, "\")\n");
                db_puts(sb, "    __v.push(");
                derive_emit_serialize_field(sb, fname[i], d->as.struct_decl.field_types[i]);
                db_puts(sb, ")\n");
            }
            db_puts(sb, "    return VObj(__k, __v)\n  }\n}\n");
        } else if (strcmp(tr, "Deserialize") == 0) {
            /* static def from_value(Value v) -> T { return T { f: <extract>, ... } }
               — best-effort rebuild from the neutral tree. Nested/T fields recurse
               via <Type>.from_value (so they must derive Deserialize too). */
            db_methods_open(sb, d, "Deserialize", true);
            db_puts(sb, "  static def from_value(Value v) -> "); db_selfty(sb, d);
            db_puts(sb, " {\n    return "); db_selfty(sb, d); db_puts(sb, " {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "      "); db_puts(sb, fname[i]); db_puts(sb, ": ");
                derive_emit_deserialize_field(sb, fname[i], d->as.struct_decl.field_types[i]);
                if (i + 1 < fc) db_puts(sb, ",");
                db_puts(sb, "\n");
            }
            db_puts(sb, "    }\n  }\n}\n");

            /* STRICT variant: static def try_from_value(Value v) -> Result(T, Str).
               Each field is extracted into a local with `try` (missing field / type
               mismatch -> the leaf's Err propagates out); the struct is then built
               from the validated locals. Locals (not inline `try` in the literal) so
               an early-return Err drops the already-extracted fields cleanly. */
            db_methods_open(sb, d, "TryDeserialize", true);
            db_puts(sb, "  static def try_from_value(Value v) -> Result(");
            db_selfty(sb, d); db_puts(sb, ", Str) {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    ");
                derive_emit_typename(sb, d->as.struct_decl.field_types[i]);
                db_puts(sb, " __d"); db_putint(sb, i); db_puts(sb, " = ");
                derive_emit_try_deserialize_field(sb, fname[i], d->as.struct_decl.field_types[i]);
                db_puts(sb, "\n");
            }
            db_puts(sb, "    return Ok("); db_selfty(sb, d); db_puts(sb, " {\n");
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "      "); db_puts(sb, fname[i]); db_puts(sb, ": __d"); db_putint(sb, i);
                if (i + 1 < fc) db_puts(sb, ",");
                db_puts(sb, "\n");
            }
            db_puts(sb, "    })\n  }\n}\n");
        } else if (strcmp(tr, "Reflect") == 0) {
            /* static def reflect() -> TypeInfo { build FieldInfo + MethodInfo
               vectors }. Fields come from the struct decl; methods are found by
               scanning `program` for impl blocks targeting this struct (their
               AST is available here even though impl_registry isn't yet). */
            db_methods_open(sb, d, "Reflect", false);
            db_puts(sb, "  static def reflect() -> TypeInfo {\n");
            db_puts(sb, "    Vec(FieldInfo) __f = []\n");
            for (int i = 0; i < fc; i++) {
                TypeNode *ft = d->as.struct_decl.field_types[i];
                /* A bare type-parameter field (`T value`) reflects its CONCRETE
                   instantiated type via __type_name(T) — resolved per
                   monomorphization (Box(int).reflect() -> "int") — instead of the
                   literal parameter name "T". Concrete fields keep a literal name. */
                bool is_tparam = false;
                if (ft != NULL && ft->kind == TYPE_NODE_NAMED &&
                    ft->as.named.arg_count == 0 && ft->as.named.name != NULL) {
                    for (int tp = 0; tp < d->as.struct_decl.type_param_count; tp++) {
                        if (d->as.struct_decl.type_params[tp] != NULL &&
                            strcmp(ft->as.named.name,
                                   d->as.struct_decl.type_params[tp]) == 0) {
                            is_tparam = true; break;
                        }
                    }
                }
                db_puts(sb, "    __f.push(FieldInfo { name: \"");
                db_puts(sb, fname[i]); db_puts(sb, "\", type_name: ");
                if (is_tparam) {
                    db_puts(sb, "__type_name("); db_puts(sb, ft->as.named.name);
                    db_puts(sb, ")");
                } else {
                    db_puts(sb, "\""); derive_emit_typename(sb, ft); db_puts(sb, "\"");
                }
                db_puts(sb, " })\n");
            }
            db_puts(sb, "    Vec(MethodInfo) __m = []\n");
            if (program != NULL && program->kind == AST_PROGRAM) {
                for (int di = 0; di < program->as.program.decl_count; di++) {
                    AstNode *pd = program->as.program.decls[di];
                    if (pd == NULL) continue;
                    AstNode **methods = NULL; int mcount = 0;
                    if (pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                        strcmp(pd->as.impl_decl.name, T) == 0) {
                        methods = pd->as.impl_decl.methods;
                        mcount = pd->as.impl_decl.method_count;
                    } else if (pd->kind == AST_IMPL_TRAIT_DECL &&
                               pd->as.impl_trait_decl.struct_name &&
                               strcmp(pd->as.impl_trait_decl.struct_name, T) == 0) {
                        methods = pd->as.impl_trait_decl.methods;
                        mcount = pd->as.impl_trait_decl.method_count;
                    }
                    for (int mi = 0; mi < mcount; mi++) {
                        AstNode *fn = methods[mi];
                        if (fn == NULL || fn->kind != AST_FN_DECL) continue;
                        db_puts(sb, "    __m.push(MethodInfo { name: \"");
                        db_puts(sb, method_display_name(fn->as.fn_decl.name));
                        db_puts(sb, "\", signature: \"");
                        derive_emit_method_sig(sb, fn);
                        db_puts(sb, "\", is_static: ");
                        db_puts(sb, fn->as.fn_decl.is_static ? "true" : "false");
                        db_puts(sb, " })\n");
                    }
                }
            }
            db_puts(sb, "    return TypeInfo { name: \""); db_puts(sb, T);
            db_puts(sb, "\", fields: __f, funcs: __m }\n  }\n}\n");
        } else if (strcmp(tr, "ReflectRaw") == 0) {
            /* The substrate counterpart of Reflect for foundational types (Str/Vec)
               that sit below the str/vec layer and so cannot import std.core.reflect.
               Emits `static def reflect_raw() -> RawType` building raw (*u8) metadata
               via std.core.reflect_core; std.core.reflect.from_raw bridges it to a
               friendly TypeInfo. Field/method scanning mirrors the Reflect branch;
               only the OUTPUT shape differs (RawType.make/set_field/set_method +
               __rawstr literals instead of Vec<FieldInfo>). RawType.make needs the
               method count up front, so methods are scanned twice (count, then emit). */
            char numbuf[32];
            int nm = 0;
            if (program != NULL && program->kind == AST_PROGRAM) {
                for (int di = 0; di < program->as.program.decl_count; di++) {
                    AstNode *pd = program->as.program.decls[di];
                    if (pd == NULL) continue;
                    AstNode **methods = NULL; int mcount = 0;
                    if (pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                        strcmp(pd->as.impl_decl.name, T) == 0) {
                        methods = pd->as.impl_decl.methods; mcount = pd->as.impl_decl.method_count;
                    } else if (pd->kind == AST_IMPL_TRAIT_DECL && pd->as.impl_trait_decl.struct_name &&
                               strcmp(pd->as.impl_trait_decl.struct_name, T) == 0) {
                        methods = pd->as.impl_trait_decl.methods; mcount = pd->as.impl_trait_decl.method_count;
                    }
                    for (int mi = 0; mi < mcount; mi++)
                        if (methods[mi] && methods[mi]->kind == AST_FN_DECL) nm++;
                }
            }
            db_methods_open(sb, d, "ReflectRaw", false);
            db_puts(sb, "  static def reflect_raw() -> RawType {\n");
            db_puts(sb, "    RawType __rt = RawType.make(__rawstr(\"");
            db_puts(sb, T); db_puts(sb, "\"), ");
            snprintf(numbuf, sizeof(numbuf), "%d, %d)\n", fc, nm);
            db_puts(sb, numbuf);
            for (int i = 0; i < fc; i++) {
                db_puts(sb, "    __rt.set_field(");
                snprintf(numbuf, sizeof(numbuf), "%d", i); db_puts(sb, numbuf);
                db_puts(sb, ", __rawstr(\""); db_puts(sb, fname[i]);
                db_puts(sb, "\"), __rawstr(\"");
                derive_emit_typename(sb, d->as.struct_decl.field_types[i]);
                db_puts(sb, "\"))\n");
            }
            int midx = 0;
            if (program != NULL && program->kind == AST_PROGRAM) {
                for (int di = 0; di < program->as.program.decl_count; di++) {
                    AstNode *pd = program->as.program.decls[di];
                    if (pd == NULL) continue;
                    AstNode **methods = NULL; int mcount = 0;
                    if (pd->kind == AST_IMPL_DECL && pd->as.impl_decl.name &&
                        strcmp(pd->as.impl_decl.name, T) == 0) {
                        methods = pd->as.impl_decl.methods; mcount = pd->as.impl_decl.method_count;
                    } else if (pd->kind == AST_IMPL_TRAIT_DECL && pd->as.impl_trait_decl.struct_name &&
                               strcmp(pd->as.impl_trait_decl.struct_name, T) == 0) {
                        methods = pd->as.impl_trait_decl.methods; mcount = pd->as.impl_trait_decl.method_count;
                    }
                    for (int mi = 0; mi < mcount; mi++) {
                        AstNode *fn = methods[mi];
                        if (fn == NULL || fn->kind != AST_FN_DECL) continue;
                        db_puts(sb, "    __rt.set_method(");
                        snprintf(numbuf, sizeof(numbuf), "%d", midx++); db_puts(sb, numbuf);
                        db_puts(sb, ", __rawstr(\"");
                        db_puts(sb, method_display_name(fn->as.fn_decl.name));
                        db_puts(sb, "\"), __rawstr(\"");
                        derive_emit_method_sig(sb, fn);
                        db_puts(sb, "\"), ");
                        db_puts(sb, fn->as.fn_decl.is_static ? "true" : "false");
                        db_puts(sb, ")\n");
                    }
                }
            }
            db_puts(sb, "    return __rt\n  }\n}\n");
        } else if (strcmp(tr, "Clone") == 0) {
            checker_error(c, d->line, d->column,
                "@derive(Clone) is unnecessary: has_drop structs are deep-copied "
                "automatically at clone points, and LS has no user-callable "
                ".clone() (the Clone interface is the compiler's internal hook)");
        } else {
            checker_error(c, d->line, d->column,
                "@derive(%s) is not supported (struct derives: Equal, Hash, "
                "Order, Show, Serialize, Deserialize, Reflect)", tr);
        }
    }
}

/* Emit the synthesized impl(s) for one enum's derives into sb. Equal/Hash use
   nested match over variants; Order on enums is deferred. */
static void derive_emit_enum(Checker *c, DeriveBuf *sb, AstNode *d) {
    const char *T = d->as.enum_decl.name;
    int vc = d->as.enum_decl.variant_count;
    for (int k = 0; k < d->as.enum_decl.derive_count; k++) {
        const char *tr = d->as.enum_decl.derives[k];
        if (strcmp(tr, "Equal") == 0) {
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Equal {\n  def ==(&self, &"); db_puts(sb, T);
            db_puts(sb, " rhs) -> bool {\n    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn);
                db_binders(sb, "x", pc);
                db_puts(sb, " => match rhs { "); db_puts(sb, vn);
                db_binders(sb, "y", pc);
                db_puts(sb, " => ");
                if (pc == 0) {
                    db_puts(sb, "true");
                } else {
                    for (int p = 0; p < pc; p++) {
                        if (p) db_puts(sb, " && ");
                        db_puts(sb, "x"); db_putint(sb, p);
                        db_puts(sb, " == y"); db_putint(sb, p);
                    }
                }
                db_puts(sb, " _ => false }\n");
            }
            db_puts(sb, "    }\n  }\n}\n");
        } else if (strcmp(tr, "Hash") == 0) {
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Hash {\n  def hash(&self) -> u64 {\n    u64 h = 0 as u64\n");
            db_puts(sb, "    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn);
                db_binders(sb, "x", pc);
                db_puts(sb, " => { h = std.core.hash.fx_mix(h, ");
                db_putint(sb, v + 1); db_puts(sb, " as u64)");
                for (int p = 0; p < pc; p++) {
                    db_puts(sb, " h = std.core.hash.fx_mix(h, x");
                    db_putint(sb, p); db_puts(sb, ".hash())");
                }
                db_puts(sb, " }\n");
            }
            db_puts(sb, "    }\n    return h\n  }\n}\n");
        } else if (strcmp(tr, "Show") == 0) {
            /* def show(&self, &!Sink out) { match self {
                 V        => { out.write("V") }
                 V(x0,..) => { out.write("V("); x0.show(&!out); out.write(", "); ..
                               out.write(")") } } } — variant name, then each payload
               via its own Show impl into the shared sink. Byte-identical to the old
               `-> Str` form. Stage 3, docs/plan_print_sink.md. */
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Show {\n  def show(&self, &!Sink out) {\n    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn);
                db_binders(sb, "x", pc);
                if (pc == 0) {
                    db_puts(sb, " => { out.write(\""); db_puts(sb, vn);
                    db_puts(sb, "\") }\n");
                } else {
                    db_puts(sb, " => { out.write(\""); db_puts(sb, vn);
                    db_puts(sb, "(\")\n");
                    for (int p = 0; p < pc; p++) {
                        if (p) db_puts(sb, "        out.write(\", \")\n");
                        db_puts(sb, "        x"); db_putint(sb, p);
                        db_puts(sb, ".show(&!out)\n");
                    }
                    db_puts(sb, "        out.write(\")\")\n      }\n");
                }
            }
            db_puts(sb, "    }\n  }\n}\n");
        } else if (strcmp(tr, "Order") == 0) {
            /* def <(&self, &E rhs) -> bool — lexicographic: compare variant
               declaration order first (via an index match on each side), and only
               when equal compare payloads field-by-field. `> <= >=` derive from
               `<` / `==`. */
            db_puts(sb, "methods "); db_puts(sb, T);
            db_puts(sb, ": Order {\n  def <(&self, &"); db_puts(sb, T);
            db_puts(sb, " rhs) -> bool {\n");
            db_puts(sb, "    int __si = match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn); db_binders(sb, "x", pc);
                db_puts(sb, " => "); db_putint(sb, v); db_puts(sb, "\n");
            }
            db_puts(sb, "    }\n    int __ri = match rhs {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn); db_binders(sb, "y", pc);
                db_puts(sb, " => "); db_putint(sb, v); db_puts(sb, "\n");
            }
            db_puts(sb, "    }\n    if __si != __ri { return __si < __ri }\n");
            db_puts(sb, "    match self {\n");
            for (int v = 0; v < vc; v++) {
                const char *vn = d->as.enum_decl.variants[v].name;
                int pc = d->as.enum_decl.variants[v].payload_count;
                db_puts(sb, "      "); db_puts(sb, vn); db_binders(sb, "x", pc);
                db_puts(sb, " => match rhs { "); db_puts(sb, vn);
                db_binders(sb, "y", pc); db_puts(sb, " => {\n");
                for (int p = 0; p < pc; p++) {
                    db_puts(sb, "        if x"); db_putint(sb, p);
                    db_puts(sb, " != y"); db_putint(sb, p);
                    db_puts(sb, " { return x"); db_putint(sb, p);
                    db_puts(sb, " < y"); db_putint(sb, p); db_puts(sb, " }\n");
                }
                db_puts(sb, "        return false\n      } _ => { return false } }\n");
            }
            db_puts(sb, "    }\n    return false\n  }\n}\n");
        } else {
            checker_error(c, d->line, d->column,
                "@derive(%s) on enums is not supported yet (enum derives Equal, "
                "Hash, Order, Show)", tr);
        }
    }
}

/* Parse a one-line `import ...` source and append its decl(s) to out[]. */
static void push_import_src(AstNode ***out, int *nc, int *ncap, int oldn,
                            const char *src) {
    AstNode *imp = parse(src, "<derive>");
    if (imp == NULL || imp->kind != AST_PROGRAM) return;
    for (int j = 0; j < imp->as.program.decl_count; j++) {
        if (*nc >= *ncap) { *ncap = *ncap ? *ncap * 2 : (oldn + 8);
            *out = realloc_safe(*out, (size_t)(*ncap) * sizeof(AstNode *)); }
        (*out)[(*nc)++] = imp->as.program.decls[j];
    }
    imp->as.program.decl_count = 0;
    ast_free(imp);
}

void expand_derives(Checker *c, AstNode *program) {
    if (program == NULL || program->kind != AST_PROGRAM) return;
    bool any = false, need_hash = false, need_show = false, need_value = false;
    bool need_reflect = false;
    bool has_hash_import = false, has_show_import = false;
    bool has_value_import = false, has_str_import = false, has_reflect_import = false;
    bool has_sink_import = false;   /* @derive(Show) synthesizes show(&!Sink) */
    int oldn = program->as.program.decl_count;
    for (int i = 0; i < oldn; i++) {
        AstNode *d = program->as.program.decls[i];
        if (d == NULL) continue;
        if (d->kind == AST_STRUCT_DECL && d->as.struct_decl.derive_count > 0) {
            any = true;
            for (int k = 0; k < d->as.struct_decl.derive_count; k++) {
                if (strcmp(d->as.struct_decl.derives[k], "Hash") == 0) need_hash = true;
                if (strcmp(d->as.struct_decl.derives[k], "Show") == 0) need_show = true;
                if (strcmp(d->as.struct_decl.derives[k], "Serialize") == 0) need_value = true;
                if (strcmp(d->as.struct_decl.derives[k], "Deserialize") == 0) need_value = true;
                if (strcmp(d->as.struct_decl.derives[k], "Reflect") == 0) need_reflect = true;
            }
        } else if (d->kind == AST_ENUM_DECL && d->as.enum_decl.derive_count > 0) {
            any = true;
            for (int k = 0; k < d->as.enum_decl.derive_count; k++) {
                if (strcmp(d->as.enum_decl.derives[k], "Hash") == 0) need_hash = true;
                if (strcmp(d->as.enum_decl.derives[k], "Show") == 0) need_show = true;
            }
        } else if (d->kind == AST_IMPORT_DECL && d->as.import_decl.path) {
            if (strcmp(d->as.import_decl.path, "std.core.hash") == 0)    has_hash_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.show") == 0)    has_show_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.value") == 0)   has_value_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.reflect") == 0) has_reflect_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.str") == 0)     has_str_import = true;
            if (strcmp(d->as.import_decl.path, "std.core.sink") == 0)    has_sink_import = true;
        }
    }
    if (!any) return;

    /* Rebuild the decl list, inserting each struct's synthesized impl(s)
       immediately after that struct — so trait-impl satisfaction is recorded
       (in check_pass, which is decl-order) before any later use. This mirrors
       the conventional hand-written layout: struct, then impl, then code. */
    AstNode **out = NULL; int nc = 0, ncap = 0;

    /* @derive(Hash) needs std.core.hash (no str dependency → front-inject is fine).
       @derive(Show) needs std.core.show, which imports std.core.str → it MUST come
       AFTER the prelude `import std.core.str` (injecting it before re-imports str
       and clobbers Str for later modules). Inject show right after the str import
       in the copy loop below; only front-inject as a fallback when there is no
       str prelude (str-default off), where there is nothing to clobber. */
    if (need_hash && !has_hash_import)
        push_import_src(&out, &nc, &ncap, oldn, "import std.core.hash\n");

    /* show.ls and value.ls both import std.core.str, so their injected imports
       MUST land after the prelude str import (front would re-import str first and
       clobber Str for later modules). Collect them; inject after str below. */
    const char *after_str[4]; int after_str_n = 0;
    /* @derive(Show) synthesizes `def show(&self, &!Sink out)` — the Sink type
       must be in scope. sink.ls imports str, so it also lands after the str
       prelude. */
    if (need_show && !has_sink_import)       after_str[after_str_n++] = "import std.core.sink\n";
    if (need_show && !has_show_import)       after_str[after_str_n++] = "import std.core.show\n";
    if (need_value && !has_value_import)     after_str[after_str_n++] = "import std.core.value\n";
    if (need_reflect && !has_reflect_import) after_str[after_str_n++] = "import std.core.reflect\n";
    bool after_str_injected = false;
    if (after_str_n > 0 && !has_str_import) {   /* no str prelude → front is safe */
        for (int k = 0; k < after_str_n; k++)
            push_import_src(&out, &nc, &ncap, oldn, after_str[k]);
        after_str_injected = true;
    }

    for (int i = 0; i < oldn; i++) {
        AstNode *d = program->as.program.decls[i];
        /* A slot the derive-splice below already relocated earlier in the
           array (see the inherent-impl look-ahead a few lines down) --
           without this the node would be copied into `out` a SECOND time
           here, in its original position, and every downstream consumer
           that walks the rebuilt array unconditionally derefs each entry
           (nothing here was ever built to expect a NULL slot; the old
           adjacency-splice avoided this by advancing the shared `i` past
           consumed nodes instead of nulling them). Missing this guard
           segfaults on EVERY generic @derive, including the previously
           passing derive_generic.lls -- caught by rerunning it after this
           fix, not by the new regression sample alone. */
        if (d == NULL) continue;
        if (nc >= ncap) { ncap = ncap ? ncap * 2 : (oldn + 8);
            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); }
        out[nc++] = d;
        if (after_str_n > 0 && !after_str_injected && d &&
            d->kind == AST_IMPORT_DECL && d->as.import_decl.path &&
            strcmp(d->as.import_decl.path, "std.core.str") == 0) {
            for (int k = 0; k < after_str_n; k++)
                push_import_src(&out, &nc, &ncap, oldn, after_str[k]);
            after_str_injected = true;
        }
        bool is_struct_der = d && d->kind == AST_STRUCT_DECL &&
                             d->as.struct_decl.derive_count > 0;
        bool is_enum_der   = d && d->kind == AST_ENUM_DECL &&
                             d->as.enum_decl.derive_count > 0;
        if (is_struct_der || is_enum_der) {
            /* Generic structs: derived interface-impls must follow the struct's
               inherent methods Name(T) block (the fold anchor). Push any impl blocks
               for this struct first (advancing i), so the synth lands after them. */
            int found_inherent_idx = -1;
            if (is_struct_der && d->as.struct_decl.type_param_count > 0) {
                const char *sname = d->as.struct_decl.name;
                for (int j = i + 1; j < oldn; j++) {
                    AstNode *nx = program->as.program.decls[j];
                    if (nx && nx->kind == AST_IMPL_DECL && nx->as.impl_decl.name &&
                        strcmp(nx->as.impl_decl.name, sname) == 0)
                    {
                        if (nc >= ncap) { ncap *= 2;
                            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); }
                        out[nc++] = nx;
                        found_inherent_idx = j;   /* NULL deferred -- see below */
                        break;
                    }
                }
            }
            DeriveBuf sb; db_init(&sb);
            if (is_struct_der) derive_emit_struct(c, &sb, d, program);
            else               derive_emit_enum(c, &sb, d);
            /* Only NULL the relocated slot now, AFTER derive_emit_struct's own
               has_inherent scan (which reads this same array) has run. Nulling
               it before that scan makes has_inherent wrongly conclude "no
               inherent block" and synthesize a SECOND, EMPTY `methods T(T) {}`
               -- which then overwrites impl_node (unconditional assignment in
               check_impl_decl, no NULL guard) and erases every method the real
               block declared. Caught by derive_generic.lls regressing to
               "struct has no field or method" after this fix, not by the new
               sample alone. */
            if (found_inherent_idx >= 0)
                program->as.program.decls[found_inherent_idx] = NULL;
            if (sb.len > 0) {
                AstNode *synth = parse(sb.data, "<derive>");
                if (synth != NULL && synth->kind == AST_PROGRAM) {
                    for (int j = 0; j < synth->as.program.decl_count; j++) {
                        if (nc >= ncap) { ncap *= 2;
                            out = realloc_safe(out, (size_t)ncap * sizeof(AstNode *)); }
                        out[nc++] = synth->as.program.decls[j];
                    }
                    synth->as.program.decl_count = 0;  /* detach transferred nodes */
                    ast_free(synth);
                }
            }
            free(sb.data);
            if (is_struct_der) d->as.struct_decl.derive_count = 0;  /* prevent re-expansion */
            else               d->as.enum_decl.derive_count = 0;
        }
    }
    free(program->as.program.decls);
    program->as.program.decls = out;
    program->as.program.decl_count = nc;
}
