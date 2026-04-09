/* ast.h — AST node definitions, constructors, and interface */
#ifndef LS_AST_H
#define LS_AST_H

#include "token.h"
#include "common.h"

typedef struct AstNode AstNode;
typedef struct TypeNode TypeNode;
typedef struct Type Type;

/* ---- TypeNode ---- */

typedef enum {
    TYPE_NODE_PRIMITIVE,  /* built-in keyword type */
    TYPE_NODE_POINTER,    /* *T */
    TYPE_NODE_ARRAY,      /* array(T, N) — fixed-size */
    TYPE_NODE_VECTOR,     /* vec(T)      — dynamic array */
    TYPE_NODE_FN,         /* fn(A, B) -> R */
    TYPE_NODE_NAMED,      /* user-defined struct name */
} TypeNodeKind;

struct TypeNode {
    TypeNodeKind kind;
    int line, column;
    union {
        TokenType primitive;              /* TYPE_NODE_PRIMITIVE: which keyword */
        TypeNode *pointee;                /* TYPE_NODE_POINTER */
        struct { TypeNode *elem; int size; } array; /* TYPE_NODE_ARRAY — size > 0 for fixed */
        struct { TypeNode *elem; }        vec;       /* TYPE_NODE_VECTOR */
        struct {
            TypeNode **params;
            int param_count;
            TypeNode *ret;  /* NULL == void */
        } fn;
        struct { char *name; } named;     /* TYPE_NODE_NAMED */
    } as;
};

/* ---- MatchArm ---- */

typedef struct {
    AstNode *pattern;  /* literal, identifier, or wildcard */
    AstNode *body;     /* expression or block */
} MatchArm;

/* ---- AstNodeType ---- */

typedef enum {
    /* Literals */
    AST_INT_LIT, AST_FLOAT_LIT, AST_STRING_LIT, AST_BOOL_LIT, AST_NIL_LIT,
    AST_FORMAT_STRING,  /* f"text {expr} text" — interpolated string */
    AST_ARRAY_LIT,      /* [expr, expr, ...] — array literal */
    /* Expressions */
    AST_IDENT, AST_UNARY, AST_BINARY, AST_CALL, AST_INDEX, AST_FIELD,
    AST_CLOSURE, AST_MATCH, AST_CAST, AST_RANGE,
    /* Statements */
    AST_VAR_DECL, AST_ASSIGN, AST_RETURN, AST_BREAK, AST_CONTINUE,
    AST_IF, AST_WHILE, AST_FOR, AST_FOR_C, AST_BLOCK, AST_EXPR_STMT,
    /* Declarations */
    AST_FN_DECL, AST_STRUCT_DECL, AST_IMPL_DECL, AST_MODULE_DECL, AST_IMPORT_DECL,
    /* Heap allocation */
    AST_NEW_EXPR,           /* new StructName { field: val, ... } */
    /* FFI */
    AST_LOAD_LIB, AST_FFI_CALL, AST_EXTERN_FN,
    /* Root */
    AST_PROGRAM,
} AstNodeType;

/* ---- AstNode ---- */

struct AstNode {
    AstNodeType kind;
    int line, column;
    Type *resolved_type;    /* Filled by type checker (not owned by AST) */
    union {
        struct { long long value; }             int_lit;
        struct { double value; }                float_lit;
        struct { char *value; int length; }     string_lit;
        struct { bool value; }                  bool_lit;
        /* nil has no data */
        struct {
            /* Alternating: parts[0] is text, exprs[0] is first {expr},
               parts[1] is text after first expr, etc.
               part_count == expr_count + 1 always. */
            char **parts;       /* text segments (part_count items) */
            AstNode **exprs;    /* interpolated expressions (expr_count items) */
            int part_count;
            int expr_count;
        } format_string;
        struct { AstNode **elements; int count; }    array_lit;
        struct { char *name; }                  ident;
        struct { TokenType op; AstNode *operand; }                          unary;
        struct { TokenType op; AstNode *left; AstNode *right; }             binary;
        struct { AstNode *callee; AstNode **args; int arg_count; }          call;
        struct { AstNode *object; AstNode *index; }                         index_expr;
        struct { AstNode *object; char *field; }                            field_access;
        struct {
            TypeNode **param_types; char **param_names; int param_count;
            TypeNode *return_type; AstNode *body;
        } closure;
        struct { AstNode *subject; MatchArm *arms; int arm_count; }         match;
        struct { AstNode *expr; TypeNode *target_type; }                    cast;
        struct { AstNode *start; AstNode *end; }                           range;
        struct { TypeNode *var_type; char *name; AstNode *init; }           var_decl;
        struct { AstNode *target; TokenType op; AstNode *value; }           assign;
        struct { AstNode *value; }                                          return_stmt;
        struct { AstNode *cond; AstNode *then_block; AstNode *else_block; } if_stmt;
        struct { AstNode *cond; AstNode *body; }                            while_stmt;
        struct { char *var; AstNode *iter; AstNode *body; }                 for_stmt;
        struct { AstNode *init; AstNode *cond; AstNode *update; AstNode *body; } for_c_stmt;
        struct { AstNode **stmts; int stmt_count; }                         block;
        struct { AstNode *expr; }                                           expr_stmt;
        struct {
            char *name;
            TypeNode **param_types; char **param_names; int param_count;
            TypeNode *return_type; AstNode *body;
            bool is_static;              /* true for 'static fn' in impl block */
            const char *impl_struct_name; /* non-NULL if this fn is inside an impl block */
        } fn_decl;
        struct {
            char *name;
            TypeNode **field_types; char **field_names; int field_count;
        } struct_decl;
        struct { char *name; AstNode **methods; int method_count; }         impl_decl;
        struct { char *name; }                                              module_decl;
        struct { char *path; }                                              import_decl;
        struct {
            char *struct_name;
            struct { char *name; AstNode *value; } *field_inits;
            int field_init_count;
            bool on_stack;  /* true = struct value literal (StructName{...}), false = new (heap) */
        } new_expr;
        struct { char *var_name; char *lib_path; }                          load_lib;
        struct {
            AstNode *lib_expr; char *fn_name;
            AstNode **args; int arg_count;
        } ffi_call;
        struct {
            char *name;
            TypeNode **param_types; char **param_names; int param_count;
            TypeNode *return_type; bool is_vararg; char *lib_name;
        } extern_fn;
        struct { AstNode **decls; int decl_count; }                         program;
    } as;
};

/* ---- Public API ---- */

/* Recursively free an AST node and all its children */
void ast_free(AstNode *node);

/* Recursively free a TypeNode */
void type_node_free(TypeNode *type);

/* Print a human-readable indented tree of the AST */
void ast_print(AstNode *node, int indent);

/* Print a type node inline */
void type_node_print(TypeNode *type);

/* Return the string name of an AstNodeType */
const char *ast_kind_name(AstNodeType kind);

#endif /* LS_AST_H */
