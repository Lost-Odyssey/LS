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
        type_node_free(type->as.pointee);
        break;
    case TYPE_NODE_ARRAY:
        type_node_free(type->as.array.elem);
        break;
    case TYPE_NODE_VECTOR:
        type_node_free(type->as.vec.elem);
        break;
    case TYPE_NODE_FN:
        for (int i = 0; i < type->as.fn.param_count; i++) {
            type_node_free(type->as.fn.params[i]);
        }
        free(type->as.fn.params);
        type_node_free(type->as.fn.ret);
        break;
    case TYPE_NODE_NAMED:
        free(type->as.named.name);
        break;
    }
    free(type);
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
    case AST_IDENT:
        free(node->as.ident.name);
        break;
    case AST_UNARY:
        ast_free(node->as.unary.operand);
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
        break;
    case AST_MATCH:
        ast_free(node->as.match.subject);
        for (int i = 0; i < node->as.match.arm_count; i++) {
            ast_free(node->as.match.arms[i].pattern);
            ast_free(node->as.match.arms[i].body);
        }
        free(node->as.match.arms);
        break;
    case AST_CAST:
        ast_free(node->as.cast.expr);
        type_node_free(node->as.cast.target_type);
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
        for (int i = 0; i < node->as.struct_decl.field_count; i++) {
            type_node_free(node->as.struct_decl.field_types[i]);
            free(node->as.struct_decl.field_names[i]);
        }
        free(node->as.struct_decl.field_types);
        free(node->as.struct_decl.field_names);
        break;
    case AST_IMPL_DECL:
        free(node->as.impl_decl.name);
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
        break;
    case AST_NEW_EXPR:
        free(node->as.new_expr.struct_name);
        for (int i = 0; i < node->as.new_expr.field_init_count; i++) {
            free(node->as.new_expr.field_inits[i].name);
            ast_free(node->as.new_expr.field_inits[i].value);
        }
        free(node->as.new_expr.field_inits);
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
    case AST_BOOL_LIT:     return "BOOL_LIT";
    case AST_NIL_LIT:      return "NIL_LIT";
    case AST_IDENT:        return "IDENT";
    case AST_UNARY:        return "UNARY";
    case AST_BINARY:       return "BINARY";
    case AST_CALL:         return "CALL";
    case AST_INDEX:        return "INDEX";
    case AST_FIELD:        return "FIELD";
    case AST_CLOSURE:      return "CLOSURE";
    case AST_MATCH:        return "MATCH";
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
    case AST_IMPL_DECL:    return "IMPL_DECL";
    case AST_MODULE_DECL:  return "MODULE_DECL";
    case AST_IMPORT_DECL:  return "IMPORT_DECL";
    case AST_NEW_EXPR:     return "NEW_EXPR";
    case AST_LOAD_LIB:     return "LOAD_LIB";
    case AST_FFI_CALL:     return "FFI_CALL";
    case AST_EXTERN_FN:    return "EXTERN_FN";
    case AST_PROGRAM:      return "PROGRAM";
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
    case TYPE_NODE_FN:
        printf("fn(");
        for (int i = 0; i < type->as.fn.param_count; i++) {
            if (i > 0) printf(", ");
            type_node_print(type->as.fn.params[i]);
        }
        printf(") -> ");
        type_node_print(type->as.fn.ret);
        break;
    case TYPE_NODE_NAMED:
        printf("%s", type->as.named.name);
        break;
    }
}

/* ---- ast_print ---- */

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
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
    case AST_NEW_EXPR:
        printf("NEW_EXPR(%s, %d fields)\n",
               node->as.new_expr.struct_name, node->as.new_expr.field_init_count);
        for (int i = 0; i < node->as.new_expr.field_init_count; i++) {
            print_indent(indent + 1);
            printf(".%s =\n", node->as.new_expr.field_inits[i].name);
            ast_print(node->as.new_expr.field_inits[i].value, indent + 2);
        }
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
        printf("EXTERN_FN(%s from %s)\n",
               node->as.extern_fn.name,
               node->as.extern_fn.lib_name ? node->as.extern_fn.lib_name : "?");
        break;
    case AST_PROGRAM:
        printf("PROGRAM(%d decls)\n", node->as.program.decl_count);
        for (int i = 0; i < node->as.program.decl_count; i++) {
            ast_print(node->as.program.decls[i], indent + 1);
        }
        break;
    }
}
