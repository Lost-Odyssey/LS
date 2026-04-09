/* codegen.h — LLVM IR code generation interface */
#ifndef LS_CODEGEN_H
#define LS_CODEGEN_H

#include "ast.h"
#include "types.h"
#include "symtable.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>

/* Codegen symbol: associates a name with an LLVM alloca/global and its type */
typedef struct {
    const char *name;
    LLVMValueRef value;     /* alloca or global */
    Type *type;
    LLVMValueRef moved_flag; /* i1 flag: true if value has been moved (for struct) */
    bool is_borrowed;        /* true for vec/struct params passed by ptr — no cleanup ownership */
} CgSymbol;

/* Codegen scope: mirrors the checker's scope chain */
typedef struct CgScope {
    CgSymbol *symbols;
    int count;
    int capacity;
    struct CgScope *parent;
} CgScope;

/* Main code generation context */
typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetMachineRef target_machine;

    CgScope *current_scope;
    LLVMValueRef current_fn;        /* Function currently being compiled */
    LLVMBasicBlockRef break_bb;     /* break target (while/for) */
    LLVMBasicBlockRef continue_bb;  /* continue target (while/for) */
    CgScope *loop_scope;            /* scope at loop entry (for break/continue cleanup) */

    /* Struct type registry (name -> LLVMTypeRef) */
    struct { const char *name; LLVMTypeRef llvm_type; Type *ls_type; } *struct_types;
    int struct_type_count;
    int struct_type_cap;

    bool had_error;
    bool extern_builtins;   /* JIT mode: declare builtins without bodies (defined elsewhere) */

    /* Temporary string slot tracking for sub-expression cleanup.
       Each string-producing expression (upper/lower/+/f-string etc.) registers its
       result alloca here. At statement boundaries, intermediates are freed and the
       top-level result is either moved to a variable or freed (for expr-stmts). */
    LLVMValueRef *temp_string_slots;
    int temp_string_count;
    int temp_string_cap;
} CodegenContext;

/* Initialize the codegen context (creates LLVM module, target, etc.) */
void codegen_init(CodegenContext *ctx, const char *module_name);

/* Destroy the codegen context and free all LLVM resources */
void codegen_destroy(CodegenContext *ctx);

/* Forward declaration (full definition in module.h) */
struct ModuleRegistry;

/* Generate LLVM IR from a type-checked AST_PROGRAM node.
   registry may be NULL (no module support). */
int codegen_compile(CodegenContext *ctx, AstNode *ast,
                    struct ModuleRegistry *registry);

/* Emit an object file (.obj / .o) from the current module */
int codegen_emit_object(CodegenContext *ctx, const char *output_path);

/* Dump LLVM IR to stderr (for debugging) */
void codegen_dump_ir(CodegenContext *ctx);

/* Get the LLVM IR as a string (caller must LLVMDisposeMessage) */
char *codegen_get_ir(CodegenContext *ctx);

#endif /* LS_CODEGEN_H */
