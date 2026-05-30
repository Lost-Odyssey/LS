/* ast.c — AST node printing and memory management */
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>

/* ---- type_node_free ---- */

/* Recursively free a TypeNode and its children */
void type_node_free(TypeNode *type) {
    if (type == NULL) return;
    switch (type->kind) {
    case TYPE_NODE_PRIMITIVE:
        break;
    case TYPE_NODE_POINTER:
    case TYPE_NODE_REFERENCE:
        type_node_free(type->as.pointee);
        break;
    case TYPE_NODE_ARRAY:
        type_node_free(type->as.array.elem);
        break;
    case TYPE_NODE_VECTOR:
        type_node_free(type->as.vec.elem);
        break;
    case TYPE_NODE_MAP:
        type_node_free(type->as.map.key);
        type_node_free(type->as.map.val);
        break;
    case TYPE_NODE_FN:
    case TYPE_NODE_BLOCK:
        for (int i = 0; i < type->as.fn.param_count; i++) {
            type_node_free(type->as.fn.params[i]);
        }
        free(type->as.fn.params);
        type_node_free(type->as.fn.ret);
        break;
    case TYPE_NODE_NAMED:
        free(type->as.named.name);
        free(type->as.named.module);  /* B-4: module qualifier (may be NULL) */
        for (int i = 0; i < type->as.named.arg_count; i++) {
            type_node_free(type->as.named.args[i]);
        }
        free(type->as.named.args);
        break;
    }
    free(type);
}

/* ---- type_node_clone ---- */

/* Portable strdup helper (ast.c-local) */
static char *ast_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc_safe(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

/* Deep-clone a TypeNode tree.  Every heap pointer (char*, child TypeNode*)
   is independently allocated so that type_node_free(clone) and
   type_node_free(original) are both safe. */
TypeNode *type_node_clone(const TypeNode *src) {
    if (src == NULL) return NULL;

    TypeNode *dst = (TypeNode *)malloc_safe(sizeof(TypeNode));
    /* Shallow-copy all scalar fields: kind, line, column, is_mut, primitive */
    *dst = *src;

    switch (src->kind) {
    case TYPE_NODE_PRIMITIVE:
        /* No heap fields — shallow copy is sufficient */
        break;

    case TYPE_NODE_POINTER:
    case TYPE_NODE_REFERENCE:
        dst->as.pointee = type_node_clone(src->as.pointee);
        break;

    case TYPE_NODE_ARRAY:
        dst->as.array.elem = type_node_clone(src->as.array.elem);
        /* .size is int, already copied by shallow copy */
        break;

    case TYPE_NODE_VECTOR:
        dst->as.vec.elem = type_node_clone(src->as.vec.elem);
        break;

    case TYPE_NODE_MAP:
        dst->as.map.key = type_node_clone(src->as.map.key);
        dst->as.map.val = type_node_clone(src->as.map.val);
        break;

    case TYPE_NODE_FN:
    case TYPE_NODE_BLOCK: {
        int n = src->as.fn.param_count;
        if (n > 0) {
            dst->as.fn.params = (TypeNode **)malloc_safe((size_t)n * sizeof(TypeNode *));
            for (int i = 0; i < n; i++)
                dst->as.fn.params[i] = type_node_clone(src->as.fn.params[i]);
        } else {
            dst->as.fn.params = NULL;
        }
        dst->as.fn.ret = type_node_clone(src->as.fn.ret);
        break;
    }

    case TYPE_NODE_NAMED: {
        dst->as.named.name = ast_strdup(src->as.named.name);
        dst->as.named.module = src->as.named.module    /* B-4: clone qualifier (may be NULL) */
            ? ast_strdup(src->as.named.module) : NULL;
        int n = src->as.named.arg_count;
        if (n > 0) {
            dst->as.named.args = (TypeNode **)malloc_safe((size_t)n * sizeof(TypeNode *));
            for (int i = 0; i < n; i++)
                dst->as.named.args[i] = type_node_clone(src->as.named.args[i]);
        } else {
            dst->as.named.args = NULL;
        }
        break;
    }
    }

    return dst;
}

/* ---- ast_free ---- */

/* Recursively free an AstNode and all its children */
void ast_free(AstNode *node) {
    if (node == NULL) return;
    switch (node->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_BOOL_LIT:
    case AST_NIL_LIT:
        break;
    case AST_STRING_LIT:
        free(node->as.string_lit.value);
        break;
    case AST_FORMAT_STRING:
        for (int i = 0; i < node->as.format_string.part_count; i++) {
            free(node->as.format_string.parts[i]);
        }
        free(node->as.format_string.parts);
        for (int i = 0; i < node->as.format_string.expr_count; i++) {
            ast_free(node->as.format_string.exprs[i]);
        }
        free(node->as.format_string.exprs);
        break;
    case AST_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++) {
            ast_free(node->as.array_lit.elements[i]);
        }
        free(node->as.array_lit.elements);
        break;
    case AST_MAP_LIT:
        for (int i = 0; i < node->as.map_lit.pair_count; i++) {
            ast_free(node->as.map_lit.keys[i]);
            ast_free(node->as.map_lit.vals[i]);
        }
        free(node->as.map_lit.keys);
        free(node->as.map_lit.vals);
        break;
    case AST_IDENT:
        free(node->as.ident.name);
        break;
    case AST_UNARY:
        ast_free(node->as.unary.operand);
        break;
    case AST_MUT_BORROW:
        ast_free(node->as.mut_borrow.operand);
        break;
    case AST_BINARY:
        ast_free(node->as.binary.left);
        ast_free(node->as.binary.right);
        break;
    case AST_CALL:
        ast_free(node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++) {
            ast_free(node->as.call.args[i]);
        }
        free(node->as.call.args);
        /* G2: free type_args */
        for (int i = 0; i < node->as.call.type_arg_count; i++)
            type_node_free(node->as.call.type_args[i]);
        free(node->as.call.type_args);
        break;
    case AST_INDEX:
        ast_free(node->as.index_expr.object);
        ast_free(node->as.index_expr.index);
        break;
    case AST_FIELD:
        ast_free(node->as.field_access.object);
        free(node->as.field_access.field);
        break;
    case AST_CLOSURE:
        for (int i = 0; i < node->as.closure.param_count; i++) {
            type_node_free(node->as.closure.param_types[i]);
            free(node->as.closure.param_names[i]);
        }
        free(node->as.closure.param_types);
        free(node->as.closure.param_names);
        type_node_free(node->as.closure.return_type);
        ast_free(node->as.closure.body);
        for (int i = 0; i < node->as.closure.capture_count; i++) {
            free(node->as.closure.captures[i].name);
        }
        free(node->as.closure.captures);
        for (int i = 0; i < node->as.closure.move_count; i++)
            free(node->as.closure.move_names[i]);
        free(node->as.closure.move_names);
        break;
    case AST_MATCH:
        ast_free(node->as.match.subject);
        for (int i = 0; i < node->as.match.arm_count; i++) {
            ast_free(node->as.match.arms[i].pattern);
            ast_free(node->as.match.arms[i].body);
        }
        free(node->as.match.arms);
        break;
    case AST_MATCH_OR_PATTERN:
        ast_free(node->as.or_pattern.left);
        ast_free(node->as.or_pattern.right);
        break;
    case AST_CAST:
        ast_free(node->as.cast.expr);
        type_node_free(node->as.cast.target_type);
        break;
    case AST_TRY:
        ast_free(node->as.try_expr.expr);
        break;
    case AST_RANGE:
        ast_free(node->as.range.start);
        ast_free(node->as.range.end);
        break;
    case AST_VAR_DECL:
        type_node_free(node->as.var_decl.var_type);
        free(node->as.var_decl.name);
        ast_free(node->as.var_decl.init);
        break;
    case AST_ASSIGN:
        ast_free(node->as.assign.target);
        ast_free(node->as.assign.value);
        break;
    case AST_RETURN:
        ast_free(node->as.return_stmt.value);
        break;
    case AST_BREAK:
    case AST_CONTINUE:
        break;
    case AST_IF:
        ast_free(node->as.if_stmt.cond);
        ast_free(node->as.if_stmt.then_block);
        ast_free(node->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        ast_free(node->as.while_stmt.cond);
        ast_free(node->as.while_stmt.body);
        break;
    case AST_FOR:
        free(node->as.for_stmt.var);
        ast_free(node->as.for_stmt.iter);
        ast_free(node->as.for_stmt.body);
        break;
    case AST_FOR_C:
        ast_free(node->as.for_c_stmt.init);
        ast_free(node->as.for_c_stmt.cond);
        ast_free(node->as.for_c_stmt.update);
        ast_free(node->as.for_c_stmt.body);
        break;
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            ast_free(node->as.block.stmts[i]);
        }
        free(node->as.block.stmts);
        break;
    case AST_EXPR_STMT:
        ast_free(node->as.expr_stmt.expr);
        break;
    case AST_FN_DECL:
        free(node->as.fn_decl.name);
        /* G2: free type_params + bounds */
        for (int i = 0; i < node->as.fn_decl.type_param_count; i++)
            free(node->as.fn_decl.type_params[i]);
        free(node->as.fn_decl.type_params);
        if (node->as.fn_decl.type_param_bounds) {
            for (int i = 0; i < node->as.fn_decl.type_param_count; i++) {
                for (int j = 0; j < node->as.fn_decl.type_param_bounds[i].count; j++)
                    free(node->as.fn_decl.type_param_bounds[i].trait_names[j]);
                free(node->as.fn_decl.type_param_bounds[i].trait_names);
            }
            free(node->as.fn_decl.type_param_bounds);
        }
        for (int i = 0; i < node->as.fn_decl.param_count; i++) {
            type_node_free(node->as.fn_decl.param_types[i]);
            free(node->as.fn_decl.param_names[i]);
        }
        free(node->as.fn_decl.param_types);
        free(node->as.fn_decl.param_names);
        type_node_free(node->as.fn_decl.return_type);
        ast_free(node->as.fn_decl.body);
        break;
    case AST_STRUCT_DECL:
        free(node->as.struct_decl.name);
        /* G1: free type_params + bounds */
        for (int i = 0; i < node->as.struct_decl.type_param_count; i++)
            free(node->as.struct_decl.type_params[i]);
        free(node->as.struct_decl.type_params);
        if (node->as.struct_decl.type_param_bounds) {
            for (int i = 0; i < node->as.struct_decl.type_param_count; i++) {
                for (int j = 0; j < node->as.struct_decl.type_param_bounds[i].count; j++)
                    free(node->as.struct_decl.type_param_bounds[i].trait_names[j]);
                free(node->as.struct_decl.type_param_bounds[i].trait_names);
            }
            free(node->as.struct_decl.type_param_bounds);
        }
        for (int i = 0; i < node->as.struct_decl.field_count; i++) {
            type_node_free(node->as.struct_decl.field_types[i]);
            free(node->as.struct_decl.field_names[i]);
        }
        free(node->as.struct_decl.field_types);
        free(node->as.struct_decl.field_names);
        break;
    case AST_ENUM_DECL:
        free(node->as.enum_decl.name);
        for (int i = 0; i < node->as.enum_decl.variant_count; i++) {
            free(node->as.enum_decl.variants[i].name);
            for (int j = 0; j < node->as.enum_decl.variants[i].payload_count; j++) {
                type_node_free(node->as.enum_decl.variants[i].payload_types[j]);
                if (node->as.enum_decl.variants[i].payload_names)
                    free(node->as.enum_decl.variants[i].payload_names[j]);
            }
            free(node->as.enum_decl.variants[i].payload_types);
            free(node->as.enum_decl.variants[i].payload_names);
        }
        free(node->as.enum_decl.variants);
        break;
    case AST_IMPL_DECL:
        free(node->as.impl_decl.name);
        /* G1.5: free type_params */
        for (int i = 0; i < node->as.impl_decl.type_param_count; i++)
            free(node->as.impl_decl.type_params[i]);
        free(node->as.impl_decl.type_params);
        for (int i = 0; i < node->as.impl_decl.method_count; i++) {
            ast_free(node->as.impl_decl.methods[i]);
        }
        free(node->as.impl_decl.methods);
        break;
    case AST_MODULE_DECL:
        free(node->as.module_decl.name);
        break;
    case AST_IMPORT_DECL:
        free(node->as.import_decl.path);
        free(node->as.import_decl.alias);
        break;
    case AST_TYPE_ALIAS_DECL:
        free(node->as.type_alias_decl.name);
        type_node_free(node->as.type_alias_decl.target);
        break;
    case AST_TRAIT_DECL:
        free(node->as.trait_decl.name);
        for (int i = 0; i < node->as.trait_decl.method_sig_count; i++) {
            ast_free(node->as.trait_decl.method_sigs[i]);
        }
        free(node->as.trait_decl.method_sigs);
        break;
    case AST_IMPL_TRAIT_DECL:
        free(node->as.impl_trait_decl.trait_name);
        free(node->as.impl_trait_decl.struct_name);
        for (int i = 0; i < node->as.impl_trait_decl.method_count; i++) {
            ast_free(node->as.impl_trait_decl.methods[i]);
        }
        free(node->as.impl_trait_decl.methods);
        break;
    case AST_NEW_EXPR:
        free(node->as.new_expr.struct_name);
        for (int i = 0; i < node->as.new_expr.field_init_count; i++) {
            free(node->as.new_expr.field_inits[i].name);
            ast_free(node->as.new_expr.field_inits[i].value);
        }
        free(node->as.new_expr.field_inits);
        /* G1: free type args */
        for (int i = 0; i < node->as.new_expr.type_arg_count; i++)
            type_node_free(node->as.new_expr.type_args[i]);
        free(node->as.new_expr.type_args);
        break;
    case AST_AT_TIME:
        ast_free(node->as.at_time.expr);
        break;
    case AST_AT_BENCH:
        ast_free(node->as.at_bench.expr);
        break;
    case AST_LOAD_LIB:
        free(node->as.load_lib.var_name);
        free(node->as.load_lib.lib_path);
        break;
    case AST_FFI_CALL:
        ast_free(node->as.ffi_call.lib_expr);
        free(node->as.ffi_call.fn_name);
        for (int i = 0; i < node->as.ffi_call.arg_count; i++) {
            ast_free(node->as.ffi_call.args[i]);
        }
        free(node->as.ffi_call.args);
        break;
    case AST_EXTERN_FN:
        free(node->as.extern_fn.name);
        for (int i = 0; i < node->as.extern_fn.param_count; i++) {
            type_node_free(node->as.extern_fn.param_types[i]);
            free(node->as.extern_fn.param_names[i]);
        }
        free(node->as.extern_fn.param_types);
        free(node->as.extern_fn.param_names);
        type_node_free(node->as.extern_fn.return_type);
        free(node->as.extern_fn.lib_name);
        break;
    case AST_EXTERN_STRUCT_DECL:
        free(node->as.extern_struct_decl.name);
        for (int i = 0; i < node->as.extern_struct_decl.field_count; i++) {
            type_node_free(node->as.extern_struct_decl.field_types[i]);
            free(node->as.extern_struct_decl.field_names[i]);
        }
        free(node->as.extern_struct_decl.field_types);
        free(node->as.extern_struct_decl.field_names);
        break;
    case AST_EXTERN_BLOCK:
        for (int i = 0; i < node->as.extern_block.decl_count; i++) {
            ast_free(node->as.extern_block.decls[i]);
        }
        free(node->as.extern_block.decls);
        break;
    case AST_PROGRAM:
        for (int i = 0; i < node->as.program.decl_count; i++) {
            ast_free(node->as.program.decls[i]);
        }
        free(node->as.program.decls);
        break;
    }
    free(node);
}

/* ---- ast_kind_name ---- */

/* Return the string name of an AstNodeType */
const char *ast_kind_name(AstNodeType kind) {
    switch (kind) {
    case AST_INT_LIT:      return "INT_LIT";
    case AST_FLOAT_LIT:    return "FLOAT_LIT";
    case AST_STRING_LIT:   return "STRING_LIT";
    case AST_FORMAT_STRING:return "FORMAT_STRING";
    case AST_ARRAY_LIT:   return "ARRAY_LIT";
    case AST_MAP_LIT:     return "MAP_LIT";
    case AST_BOOL_LIT:     return "BOOL_LIT";
    case AST_NIL_LIT:      return "NIL_LIT";
    case AST_IDENT:        return "IDENT";
    case AST_UNARY:        return "UNARY";
    case AST_MUT_BORROW:   return "MUT_BORROW";
    case AST_BINARY:       return "BINARY";
    case AST_CALL:         return "CALL";
    case AST_INDEX:        return "INDEX";
    case AST_FIELD:        return "FIELD";
    case AST_CLOSURE:      return "CLOSURE";
    case AST_MATCH:        return "MATCH";
    case AST_TRY:          return "TRY";
    case AST_CAST:         return "CAST";
    case AST_RANGE:        return "RANGE";
    case AST_VAR_DECL:     return "VAR_DECL";
    case AST_ASSIGN:       return "ASSIGN";
    case AST_RETURN:       return "RETURN";
    case AST_BREAK:        return "BREAK";
    case AST_CONTINUE:     return "CONTINUE";
    case AST_IF:           return "IF";
    case AST_WHILE:        return "WHILE";
    case AST_FOR:          return "FOR";
    case AST_FOR_C:        return "FOR_C";
    case AST_BLOCK:        return "BLOCK";
    case AST_EXPR_STMT:    return "EXPR_STMT";
    case AST_FN_DECL:      return "FN_DECL";
    case AST_STRUCT_DECL:  return "STRUCT_DECL";
    case AST_ENUM_DECL:    return "ENUM_DECL";
    case AST_IMPL_DECL:    return "IMPL_DECL";
    case AST_MODULE_DECL:  return "MODULE_DECL";
    case AST_IMPORT_DECL:  return "IMPORT_DECL";
    case AST_TYPE_ALIAS_DECL: return "TYPE_ALIAS_DECL";
    case AST_TRAIT_DECL:     return "TRAIT_DECL";
    case AST_IMPL_TRAIT_DECL: return "IMPL_TRAIT_DECL";
    case AST_NEW_EXPR:     return "NEW_EXPR";
    case AST_AT_TIME:      return "AT_TIME";
    case AST_AT_BENCH:     return "AT_BENCH";
    case AST_LOAD_LIB:     return "LOAD_LIB";
    case AST_FFI_CALL:     return "FFI_CALL";
    case AST_EXTERN_FN:          return "EXTERN_FN";
    case AST_EXTERN_STRUCT_DECL: return "EXTERN_STRUCT_DECL";
    case AST_EXTERN_BLOCK:       return "EXTERN_BLOCK";
    case AST_PROGRAM:            return "PROGRAM";
    }
    return "UNKNOWN";
}

/* ---- type_node_print ---- */

/* Print a TypeNode inline */
void type_node_print(TypeNode *type) {
    if (type == NULL) {
        printf("void");
        return;
    }
    switch (type->kind) {
    case TYPE_NODE_PRIMITIVE:
        printf("%s", token_type_name(type->as.primitive));
        break;
    case TYPE_NODE_POINTER:
        printf("*");
        type_node_print(type->as.pointee);
        break;
    case TYPE_NODE_REFERENCE:
        printf(type->is_mut ? "&!" : "&");
        type_node_print(type->as.pointee);
        break;
    case TYPE_NODE_ARRAY:
        printf("array(");
        type_node_print(type->as.array.elem);
        printf(", %d)", type->as.array.size);
        break;
    case TYPE_NODE_VECTOR:
        printf("vec(");
        type_node_print(type->as.vec.elem);
        printf(")");
        break;
    case TYPE_NODE_MAP:
        printf("map(");
        type_node_print(type->as.map.key);
        printf(", ");
        type_node_print(type->as.map.val);
        printf(")");
        break;
    case TYPE_NODE_FN:
    case TYPE_NODE_BLOCK:
        printf("%s(", type->kind == TYPE_NODE_BLOCK ? "Block" : "fn");
        for (int i = 0; i < type->as.fn.param_count; i++) {
            if (i > 0) printf(", ");
            type_node_print(type->as.fn.params[i]);
        }
        printf(") -> ");
        type_node_print(type->as.fn.ret);
        break;
    case TYPE_NODE_NAMED:
        printf("%s", type->as.named.name);
        if (type->as.named.arg_count > 0) {
            printf("(");
            for (int i = 0; i < type->as.named.arg_count; i++) {
                if (i > 0) printf(", ");
                type_node_print(type->as.named.args[i]);
            }
            printf(")");
        }
        break;
    }
}

/* ---- ast_print ---- */

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

/* G1.5: Deep-clone an AST subtree.  resolved_type is NOT copied. */
AstNode *ast_clone_deep(const AstNode *src) {
    if (src == NULL) return NULL;

    AstNode *n = (AstNode *)malloc_safe(sizeof(AstNode));
    *n = *src;                  /* shallow copy first */
    n->resolved_type = NULL;    /* must be re-checked */

    switch (src->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_BOOL_LIT:
    case AST_NIL_LIT:
    case AST_BREAK:
    case AST_CONTINUE:
        break;
    case AST_STRING_LIT:
        n->as.string_lit.value = ast_strdup(src->as.string_lit.value);
        break;
    case AST_IDENT:
        n->as.ident.name = ast_strdup(src->as.ident.name);
        break;
    case AST_UNARY:
        n->as.unary.operand = ast_clone_deep(src->as.unary.operand);
        break;
    case AST_MUT_BORROW:
        n->as.mut_borrow.operand = ast_clone_deep(src->as.mut_borrow.operand);
        break;
    case AST_BINARY:
        n->as.binary.left  = ast_clone_deep(src->as.binary.left);
        n->as.binary.right = ast_clone_deep(src->as.binary.right);
        break;
    case AST_CALL:
        n->as.call.callee = ast_clone_deep(src->as.call.callee);
        if (src->as.call.arg_count > 0) {
            n->as.call.args = (AstNode **)malloc_safe((size_t)src->as.call.arg_count * sizeof(AstNode *));
            for (int i = 0; i < src->as.call.arg_count; i++)
                n->as.call.args[i] = ast_clone_deep(src->as.call.args[i]);
        }
        /* G2: clone type_args */
        if (src->as.call.type_arg_count > 0) {
            int tc = src->as.call.type_arg_count;
            n->as.call.type_args = (TypeNode **)malloc_safe((size_t)tc * sizeof(TypeNode *));
            for (int i = 0; i < tc; i++)
                n->as.call.type_args[i] = type_node_clone(src->as.call.type_args[i]);
            n->as.call.type_arg_count = tc;
        }
        break;
    case AST_INDEX:
        n->as.index_expr.object = ast_clone_deep(src->as.index_expr.object);
        n->as.index_expr.index  = ast_clone_deep(src->as.index_expr.index);
        break;
    case AST_FIELD:
        n->as.field_access.object = ast_clone_deep(src->as.field_access.object);
        n->as.field_access.field  = ast_strdup(src->as.field_access.field);
        break;
    case AST_RETURN:
        n->as.return_stmt.value = ast_clone_deep(src->as.return_stmt.value);
        break;
    case AST_EXPR_STMT:
        n->as.expr_stmt.expr = ast_clone_deep(src->as.expr_stmt.expr);
        break;
    case AST_BLOCK: {
        int sc = src->as.block.stmt_count;
        n->as.block.stmts = sc > 0 ? (AstNode **)malloc_safe((size_t)sc * sizeof(AstNode *)) : NULL;
        for (int i = 0; i < sc; i++)
            n->as.block.stmts[i] = ast_clone_deep(src->as.block.stmts[i]);
        break;
    }
    case AST_VAR_DECL:
        n->as.var_decl.name = ast_strdup(src->as.var_decl.name);
        n->as.var_decl.var_type = type_node_clone(src->as.var_decl.var_type);
        n->as.var_decl.init = ast_clone_deep(src->as.var_decl.init);
        break;
    case AST_ASSIGN:
        n->as.assign.target = ast_clone_deep(src->as.assign.target);
        n->as.assign.value  = ast_clone_deep(src->as.assign.value);
        break;
    case AST_IF:
        n->as.if_stmt.cond       = ast_clone_deep(src->as.if_stmt.cond);
        n->as.if_stmt.then_block = ast_clone_deep(src->as.if_stmt.then_block);
        n->as.if_stmt.else_block = ast_clone_deep(src->as.if_stmt.else_block);
        break;
    case AST_WHILE:
        n->as.while_stmt.cond = ast_clone_deep(src->as.while_stmt.cond);
        n->as.while_stmt.body = ast_clone_deep(src->as.while_stmt.body);
        break;
    case AST_FOR:
        n->as.for_stmt.var  = ast_strdup(src->as.for_stmt.var);
        n->as.for_stmt.iter = ast_clone_deep(src->as.for_stmt.iter);
        n->as.for_stmt.body = ast_clone_deep(src->as.for_stmt.body);
        break;
    case AST_FOR_C:
        n->as.for_c_stmt.init   = ast_clone_deep(src->as.for_c_stmt.init);
        n->as.for_c_stmt.cond   = ast_clone_deep(src->as.for_c_stmt.cond);
        n->as.for_c_stmt.update = ast_clone_deep(src->as.for_c_stmt.update);
        n->as.for_c_stmt.body   = ast_clone_deep(src->as.for_c_stmt.body);
        break;
    case AST_CAST:
        n->as.cast.expr        = ast_clone_deep(src->as.cast.expr);
        n->as.cast.target_type = type_node_clone(src->as.cast.target_type);
        break;
    case AST_TRY:
        n->as.try_expr.expr = ast_clone_deep(src->as.try_expr.expr);
        break;
    case AST_RANGE:
        n->as.range.start = ast_clone_deep(src->as.range.start);
        n->as.range.end   = ast_clone_deep(src->as.range.end);
        break;
    case AST_FORMAT_STRING: {
        int pc = src->as.format_string.part_count;
        int ec = src->as.format_string.expr_count;
        n->as.format_string.parts = pc > 0 ? (char **)malloc_safe((size_t)pc * sizeof(char *)) : NULL;
        for (int i = 0; i < pc; i++)
            n->as.format_string.parts[i] = ast_strdup(src->as.format_string.parts[i]);
        n->as.format_string.exprs = ec > 0 ? (AstNode **)malloc_safe((size_t)ec * sizeof(AstNode *)) : NULL;
        for (int i = 0; i < ec; i++)
            n->as.format_string.exprs[i] = ast_clone_deep(src->as.format_string.exprs[i]);
        break;
    }
    case AST_ARRAY_LIT: {
        int c = src->as.array_lit.count;
        n->as.array_lit.elements = c > 0 ? (AstNode **)malloc_safe((size_t)c * sizeof(AstNode *)) : NULL;
        for (int i = 0; i < c; i++)
            n->as.array_lit.elements[i] = ast_clone_deep(src->as.array_lit.elements[i]);
        break;
    }
    case AST_MAP_LIT: {
        int c = src->as.map_lit.pair_count;
        n->as.map_lit.keys = c > 0 ? (AstNode **)malloc_safe((size_t)c * sizeof(AstNode *)) : NULL;
        n->as.map_lit.vals = c > 0 ? (AstNode **)malloc_safe((size_t)c * sizeof(AstNode *)) : NULL;
        for (int i = 0; i < c; i++) {
            n->as.map_lit.keys[i] = ast_clone_deep(src->as.map_lit.keys[i]);
            n->as.map_lit.vals[i] = ast_clone_deep(src->as.map_lit.vals[i]);
        }
        break;
    }
    case AST_NEW_EXPR: {
        n->as.new_expr.struct_name = ast_strdup(src->as.new_expr.struct_name);
        int fc = src->as.new_expr.field_init_count;
        if (fc > 0) {
            size_t sz = (size_t)fc * sizeof(src->as.new_expr.field_inits[0]);
            n->as.new_expr.field_inits = (void *)malloc_safe(sz);
            for (int i = 0; i < fc; i++) {
                n->as.new_expr.field_inits[i].name  = ast_strdup(src->as.new_expr.field_inits[i].name);
                n->as.new_expr.field_inits[i].value = ast_clone_deep(src->as.new_expr.field_inits[i].value);
            }
        }
        int tac = src->as.new_expr.type_arg_count;
        if (tac > 0) {
            n->as.new_expr.type_args = (TypeNode **)malloc_safe((size_t)tac * sizeof(TypeNode *));
            for (int i = 0; i < tac; i++)
                n->as.new_expr.type_args[i] = type_node_clone(src->as.new_expr.type_args[i]);
        }
        break;
    }
    case AST_MATCH: {
        n->as.match.subject = ast_clone_deep(src->as.match.subject);
        int ac = src->as.match.arm_count;
        if (ac > 0) {
            size_t sz = (size_t)ac * sizeof(src->as.match.arms[0]);
            n->as.match.arms = (void *)malloc_safe(sz);
            for (int i = 0; i < ac; i++) {
                n->as.match.arms[i] = src->as.match.arms[i]; /* shallow copy flags */
                n->as.match.arms[i].pattern = ast_clone_deep(src->as.match.arms[i].pattern);
                n->as.match.arms[i].body    = ast_clone_deep(src->as.match.arms[i].body);
            }
        }
        break;
    }
    case AST_FN_DECL: {
        n->as.fn_decl.name = ast_strdup(src->as.fn_decl.name);
        /* G2: clone type_params + bounds */
        int tpc = src->as.fn_decl.type_param_count;
        n->as.fn_decl.type_param_count = tpc;
        if (tpc > 0) {
            n->as.fn_decl.type_params = (char **)malloc_safe((size_t)tpc * sizeof(char *));
            for (int i = 0; i < tpc; i++)
                n->as.fn_decl.type_params[i] = ast_strdup(src->as.fn_decl.type_params[i]);
        }
        /* Deep-clone type_param_bounds (or NULL if absent) */
        if (src->as.fn_decl.type_param_bounds && tpc > 0) {
            n->as.fn_decl.type_param_bounds = (TypeParamBound *)
                malloc_safe((size_t)tpc * sizeof(TypeParamBound));
            for (int i = 0; i < tpc; i++) {
                int bc = src->as.fn_decl.type_param_bounds[i].count;
                n->as.fn_decl.type_param_bounds[i].count = bc;
                if (bc > 0) {
                    n->as.fn_decl.type_param_bounds[i].trait_names =
                        (char **)malloc_safe((size_t)bc * sizeof(char *));
                    for (int j = 0; j < bc; j++)
                        n->as.fn_decl.type_param_bounds[i].trait_names[j] =
                            ast_strdup(src->as.fn_decl.type_param_bounds[i].trait_names[j]);
                } else {
                    n->as.fn_decl.type_param_bounds[i].trait_names = NULL;
                }
            }
        } else {
            n->as.fn_decl.type_param_bounds = NULL;
        }
        int pc = src->as.fn_decl.param_count;
        if (pc > 0) {
            n->as.fn_decl.param_types = (TypeNode **)malloc_safe((size_t)pc * sizeof(TypeNode *));
            n->as.fn_decl.param_names = (char **)malloc_safe((size_t)pc * sizeof(char *));
            for (int i = 0; i < pc; i++) {
                n->as.fn_decl.param_types[i] = type_node_clone(src->as.fn_decl.param_types[i]);
                n->as.fn_decl.param_names[i] = ast_strdup(src->as.fn_decl.param_names[i]);
            }
        }
        n->as.fn_decl.return_type = type_node_clone(src->as.fn_decl.return_type);
        n->as.fn_decl.body = ast_clone_deep(src->as.fn_decl.body);
        /* impl_struct_name points into the impl_decl; don't strdup — it
           will be overridden by the caller for generic instantiations. */
        break;
    }
    case AST_CLOSURE: {
        int pc = src->as.closure.param_count;
        if (pc > 0) {
            n->as.closure.param_types = (TypeNode **)malloc_safe((size_t)pc * sizeof(TypeNode *));
            n->as.closure.param_names = (char **)malloc_safe((size_t)pc * sizeof(char *));
            for (int i = 0; i < pc; i++) {
                n->as.closure.param_types[i] = type_node_clone(src->as.closure.param_types[i]);
                n->as.closure.param_names[i] = ast_strdup(src->as.closure.param_names[i]);
            }
        }
        n->as.closure.return_type = type_node_clone(src->as.closure.return_type);
        n->as.closure.body = ast_clone_deep(src->as.closure.body);
        /* Captures are filled by checker, not cloned here */
        n->as.closure.captures = NULL;
        n->as.closure.capture_count = 0;
        n->as.closure.move_names = NULL;
        n->as.closure.move_count = 0;
        break;
    }
    default:
        /* For any unhandled node types, the shallow copy is kept.
           This should not happen in method bodies. */
        break;
    }
    return n;
}

/* Print a human-readable indented tree of the AST */
void ast_print(AstNode *node, int indent) {
    if (node == NULL) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);

    switch (node->kind) {
    case AST_INT_LIT:
        printf("INT_LIT(%lld)\n", node->as.int_lit.value);
        break;
    case AST_FLOAT_LIT:
        printf("FLOAT_LIT(%g)\n", node->as.float_lit.value);
        break;
    case AST_STRING_LIT:
        printf("STRING_LIT(\"%s\")\n", node->as.string_lit.value);
        break;
    case AST_FORMAT_STRING:
        printf("FORMAT_STRING(%d parts, %d exprs)\n",
               node->as.format_string.part_count, node->as.format_string.expr_count);
        for (int i = 0; i < node->as.format_string.expr_count; i++) {
            print_indent(indent + 1);
            printf("text: \"%s\"\n", node->as.format_string.parts[i]);
            print_indent(indent + 1);
            printf("expr[%d]:\n", i);
            ast_print(node->as.format_string.exprs[i], indent + 2);
        }
        if (node->as.format_string.part_count > node->as.format_string.expr_count) {
            print_indent(indent + 1);
            printf("text: \"%s\"\n",
                   node->as.format_string.parts[node->as.format_string.expr_count]);
        }
        break;
    case AST_ARRAY_LIT:
        printf("ARRAY_LIT(%d elements)\n", node->as.array_lit.count);
        for (int i = 0; i < node->as.array_lit.count; i++) {
            ast_print(node->as.array_lit.elements[i], indent + 1);
        }
        break;
    case AST_MAP_LIT:
        printf("MAP_LIT(%d pairs)\n", node->as.map_lit.pair_count);
        for (int i = 0; i < node->as.map_lit.pair_count; i++) {
            print_indent(indent + 1); printf("key:\n");
            ast_print(node->as.map_lit.keys[i], indent + 2);
            print_indent(indent + 1); printf("val:\n");
            ast_print(node->as.map_lit.vals[i], indent + 2);
        }
        break;
    case AST_BOOL_LIT:
        printf("BOOL_LIT(%s)\n", node->as.bool_lit.value ? "true" : "false");
        break;
    case AST_NIL_LIT:
        printf("NIL_LIT\n");
        break;
    case AST_IDENT:
        printf("IDENT(%s)\n", node->as.ident.name);
        break;
    case AST_UNARY:
        printf("UNARY(%s)\n", token_type_name(node->as.unary.op));
        ast_print(node->as.unary.operand, indent + 1);
        break;
    case AST_MUT_BORROW:
        printf("MUT_BORROW\n");
        ast_print(node->as.mut_borrow.operand, indent + 1);
        break;
    case AST_BINARY:
        printf("BINARY(%s)\n", token_type_name(node->as.binary.op));
        ast_print(node->as.binary.left, indent + 1);
        ast_print(node->as.binary.right, indent + 1);
        break;
    case AST_CALL:
        printf("CALL\n");
        print_indent(indent + 1);
        printf("callee:\n");
        ast_print(node->as.call.callee, indent + 2);
        print_indent(indent + 1);
        printf("args(%d):\n", node->as.call.arg_count);
        for (int i = 0; i < node->as.call.arg_count; i++) {
            ast_print(node->as.call.args[i], indent + 2);
        }
        break;
    case AST_INDEX:
        printf("INDEX\n");
        ast_print(node->as.index_expr.object, indent + 1);
        ast_print(node->as.index_expr.index, indent + 1);
        break;
    case AST_FIELD:
        printf("FIELD(.%s)\n", node->as.field_access.field);
        ast_print(node->as.field_access.object, indent + 1);
        break;
    case AST_CLOSURE:
        printf("CLOSURE(");
        for (int i = 0; i < node->as.closure.param_count; i++) {
            if (i > 0) printf(", ");
            type_node_print(node->as.closure.param_types[i]);
            printf(" %s", node->as.closure.param_names[i]);
        }
        printf(") -> ");
        type_node_print(node->as.closure.return_type);
        printf("\n");
        ast_print(node->as.closure.body, indent + 1);
        break;
    case AST_MATCH:
        printf("MATCH\n");
        print_indent(indent + 1);
        printf("subject:\n");
        ast_print(node->as.match.subject, indent + 2);
        print_indent(indent + 1);
        printf("arms(%d):\n", node->as.match.arm_count);
        for (int i = 0; i < node->as.match.arm_count; i++) {
            print_indent(indent + 2);
            printf("arm[%d] pattern:\n", i);
            ast_print(node->as.match.arms[i].pattern, indent + 3);
            print_indent(indent + 2);
            printf("arm[%d] body:\n", i);
            ast_print(node->as.match.arms[i].body, indent + 3);
        }
        break;
    case AST_CAST:
        printf("CAST as ");
        type_node_print(node->as.cast.target_type);
        printf("\n");
        ast_print(node->as.cast.expr, indent + 1);
        break;
    case AST_TRY:
        printf("TRY\n");
        ast_print(node->as.try_expr.expr, indent + 1);
        break;
    case AST_RANGE:
        printf("RANGE(..)\n");
        print_indent(indent + 1);
        printf("start:\n");
        ast_print(node->as.range.start, indent + 2);
        print_indent(indent + 1);
        printf("end:\n");
        ast_print(node->as.range.end, indent + 2);
        break;
    case AST_VAR_DECL:
        printf("VAR_DECL(");
        type_node_print(node->as.var_decl.var_type);
        printf(" %s)\n", node->as.var_decl.name);
        if (node->as.var_decl.init) {
            ast_print(node->as.var_decl.init, indent + 1);
        }
        break;
    case AST_ASSIGN:
        printf("ASSIGN(%s)\n", token_type_name(node->as.assign.op));
        ast_print(node->as.assign.target, indent + 1);
        ast_print(node->as.assign.value, indent + 1);
        break;
    case AST_RETURN:
        printf("RETURN\n");
        if (node->as.return_stmt.value) {
            ast_print(node->as.return_stmt.value, indent + 1);
        }
        break;
    case AST_BREAK:
        printf("BREAK\n");
        break;
    case AST_CONTINUE:
        printf("CONTINUE\n");
        break;
    case AST_IF:
        printf("IF\n");
        print_indent(indent + 1);
        printf("cond:\n");
        ast_print(node->as.if_stmt.cond, indent + 2);
        print_indent(indent + 1);
        printf("then:\n");
        ast_print(node->as.if_stmt.then_block, indent + 2);
        if (node->as.if_stmt.else_block) {
            print_indent(indent + 1);
            printf("else:\n");
            ast_print(node->as.if_stmt.else_block, indent + 2);
        }
        break;
    case AST_WHILE:
        printf("WHILE\n");
        print_indent(indent + 1);
        printf("cond:\n");
        ast_print(node->as.while_stmt.cond, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_print(node->as.while_stmt.body, indent + 2);
        break;
    case AST_FOR:
        printf("FOR(%s in)\n", node->as.for_stmt.var);
        print_indent(indent + 1);
        printf("iter:\n");
        ast_print(node->as.for_stmt.iter, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_print(node->as.for_stmt.body, indent + 2);
        break;
    case AST_FOR_C:
        printf("FOR_C\n");
        print_indent(indent + 1);
        printf("init:\n");
        ast_print(node->as.for_c_stmt.init, indent + 2);
        print_indent(indent + 1);
        printf("cond:\n");
        ast_print(node->as.for_c_stmt.cond, indent + 2);
        print_indent(indent + 1);
        printf("update:\n");
        ast_print(node->as.for_c_stmt.update, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_print(node->as.for_c_stmt.body, indent + 2);
        break;
    case AST_BLOCK:
        printf("BLOCK(%d stmts)\n", node->as.block.stmt_count);
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            ast_print(node->as.block.stmts[i], indent + 1);
        }
        break;
    case AST_EXPR_STMT:
        printf("EXPR_STMT\n");
        ast_print(node->as.expr_stmt.expr, indent + 1);
        break;
    case AST_FN_DECL:
        printf("FN_DECL(%s)(", node->as.fn_decl.name);
        for (int i = 0; i < node->as.fn_decl.param_count; i++) {
            if (i > 0) printf(", ");
            type_node_print(node->as.fn_decl.param_types[i]);
            printf(" %s", node->as.fn_decl.param_names[i]);
        }
        printf(") -> ");
        type_node_print(node->as.fn_decl.return_type);
        printf("\n");
        ast_print(node->as.fn_decl.body, indent + 1);
        break;
    case AST_STRUCT_DECL:
        printf("STRUCT_DECL(%s)\n", node->as.struct_decl.name);
        for (int i = 0; i < node->as.struct_decl.field_count; i++) {
            print_indent(indent + 1);
            type_node_print(node->as.struct_decl.field_types[i]);
            printf(" %s\n", node->as.struct_decl.field_names[i]);
        }
        break;
    case AST_ENUM_DECL:
        printf("ENUM_DECL(%s)\n", node->as.enum_decl.name);
        for (int i = 0; i < node->as.enum_decl.variant_count; i++) {
            print_indent(indent + 1);
            printf("VARIANT(%s)", node->as.enum_decl.variants[i].name);
            for (int j = 0; j < node->as.enum_decl.variants[i].payload_count; j++) {
                printf(j == 0 ? "(" : ", ");
                type_node_print(node->as.enum_decl.variants[i].payload_types[j]);
                if (node->as.enum_decl.variants[i].payload_names && node->as.enum_decl.variants[i].payload_names[j])
                    printf(" %s", node->as.enum_decl.variants[i].payload_names[j]);
            }
            if (node->as.enum_decl.variants[i].payload_count > 0) printf(")");
            printf("\n");
        }
        break;
    case AST_IMPL_DECL:
        printf("IMPL_DECL(%s)\n", node->as.impl_decl.name);
        for (int i = 0; i < node->as.impl_decl.method_count; i++) {
            ast_print(node->as.impl_decl.methods[i], indent + 1);
        }
        break;
    case AST_MODULE_DECL:
        printf("MODULE_DECL(%s)\n", node->as.module_decl.name);
        break;
    case AST_IMPORT_DECL:
        printf("IMPORT_DECL(%s)\n", node->as.import_decl.path);
        break;
    case AST_TYPE_ALIAS_DECL:
        printf("TYPE_ALIAS_DECL(%s = ", node->as.type_alias_decl.name);
        type_node_print(node->as.type_alias_decl.target);
        printf(")\n");
        break;
    case AST_TRAIT_DECL:
        printf("TRAIT_DECL(%s, %d sigs)\n",
               node->as.trait_decl.name, node->as.trait_decl.method_sig_count);
        for (int i = 0; i < node->as.trait_decl.method_sig_count; i++) {
            ast_print(node->as.trait_decl.method_sigs[i], indent + 1);
        }
        break;
    case AST_IMPL_TRAIT_DECL:
        printf("IMPL_TRAIT_DECL(%s for %s, %d methods)\n",
               node->as.impl_trait_decl.trait_name,
               node->as.impl_trait_decl.struct_name,
               node->as.impl_trait_decl.method_count);
        for (int i = 0; i < node->as.impl_trait_decl.method_count; i++) {
            ast_print(node->as.impl_trait_decl.methods[i], indent + 1);
        }
        break;
    case AST_NEW_EXPR:
        printf("NEW_EXPR(%s, %d fields)\n",
               node->as.new_expr.struct_name, node->as.new_expr.field_init_count);
        for (int i = 0; i < node->as.new_expr.field_init_count; i++) {
            print_indent(indent + 1);
            printf(".%s =\n", node->as.new_expr.field_inits[i].name);
            ast_print(node->as.new_expr.field_inits[i].value, indent + 2);
        }
        break;
    case AST_AT_TIME:
        printf("AT_TIME\n");
        ast_print(node->as.at_time.expr, indent + 1);
        break;
    case AST_AT_BENCH:
        printf("AT_BENCH(iterations=%d)\n", node->as.at_bench.iterations);
        ast_print(node->as.at_bench.expr, indent + 1);
        break;
    case AST_LOAD_LIB:
        printf("LOAD_LIB(%s = load(\"%s\"))\n",
               node->as.load_lib.var_name, node->as.load_lib.lib_path);
        break;
    case AST_FFI_CALL:
        printf("FFI_CALL(.%s)\n", node->as.ffi_call.fn_name);
        ast_print(node->as.ffi_call.lib_expr, indent + 1);
        for (int i = 0; i < node->as.ffi_call.arg_count; i++) {
            ast_print(node->as.ffi_call.args[i], indent + 1);
        }
        break;
    case AST_EXTERN_FN:
        printf("EXTERN_FN(%s%s%s)\n",
               node->as.extern_fn.name,
               node->as.extern_fn.lib_name ? " from " : "",
               node->as.extern_fn.lib_name ? node->as.extern_fn.lib_name : "");
        break;
    case AST_EXTERN_STRUCT_DECL:
        printf("EXTERN_STRUCT_DECL(%s, %d fields)\n",
               node->as.extern_struct_decl.name,
               node->as.extern_struct_decl.field_count);
        break;
    case AST_EXTERN_BLOCK:
        printf("EXTERN_BLOCK(%d decls)\n", node->as.extern_block.decl_count);
        for (int i = 0; i < node->as.extern_block.decl_count; i++) {
            ast_print(node->as.extern_block.decls[i], indent + 1);
        }
        break;
    case AST_PROGRAM:
        printf("PROGRAM(%d decls)\n", node->as.program.decl_count);
        for (int i = 0; i < node->as.program.decl_count; i++) {
            ast_print(node->as.program.decls[i], indent + 1);
        }
        break;
    }
}
