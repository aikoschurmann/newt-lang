#pragma once

#include "codegen/codegen.h"
#include "hash_map.h"
#include "parsing/ast.h"
#include "sema/type.h"
#include "sema/type_utils.h"
#include "core/error.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Context Internals --- */

typedef struct CodegenMap {
    HashMap *map;
    struct CodegenMap *parent;
} CodegenMap;

struct CodegenContext {
    TypeStore *store;
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetRef target;
    LLVMTargetMachineRef machine;
    LLVMTargetDataRef target_data;
    HashMap *globals;
    HashMap *type_cache;
    CodegenMap *locals;
    LLVMBasicBlockRef loop_cond_bb;
    LLVMBasicBlockRef loop_end_bb;
    int opt_level;
    
    ModuleLoader *loader; // Added for module name mangling
    
    // For sret
    Type *current_func_type;
    LLVMValueRef sret_ptr;
    
    // For defer
    DynArray *deferred_actions;
    size_t loop_defer_count;

    // Centralized defer cleanup tracking
    LLVMBasicBlockRef current_cleanup_bb; 
    LLVMValueRef exit_dest_var;
    LLVMValueRef ret_val_var;
};

/* --- Internal Helpers --- */

CodegenMap   *codegen_map_create(CodegenContext *ctx, CodegenMap *parent);
void          codegen_map_destroy(CodegenMap *m);
void          codegen_map_put(CodegenMap *m, void *key, LLVMValueRef val);
LLVMValueRef  codegen_map_get(CodegenMap *m, void *key);

LLVMTypeRef  get_llvm_type(CodegenContext *ctx, Type *t);
LLVMTypeRef  get_llvm_function_type(CodegenContext *ctx, Type *t);
bool         type_is_address_only(Type *t);
bool         type_is_indirect(CodegenContext *ctx, Type *t);
LLVMValueRef codegen_load_value(CodegenContext *ctx, LLVMValueRef ptr, Type *type);
LLVMValueRef codegen_materialize_slice(CodegenContext *ctx, LLVMValueRef val, Type *src_type, Type *dst_type);
LLVMValueRef create_entry_block_alloca(CodegenContext *ctx, LLVMTypeRef ty, const char *name);
char*        mangle_name(CodegenContext *ctx, CompilationUnit *unit, InternResult *symbol_name, Type *fn_type);
bool         struct_field_index(Type *struct_type, const char *field_name, size_t *out_index);
LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_lvalue(CodegenContext *ctx, AstNode *expr);
void         codegen_statement(CodegenContext *ctx, AstNode *stmt);

/* --- Sub-dispatchers for codegen_expr --- */

LLVMValueRef codegen_expr_literal(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_ident(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_ops(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_call(CodegenContext *ctx, AstNode *expr);

/* --- Decl logic --- */

void codegen_decl_proto(CodegenContext *ctx, AstNode *decl);
void codegen_decl_body(CodegenContext *ctx, AstNode *decl);
