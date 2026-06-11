#pragma once

#include "codegen/codegen.h"
#include "hash_map.h"
#include "parsing/ast.h"
#include "sema/type.h"
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
    AstNode *program;
    TypeStore *store;
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    HashMap *globals;
    CodegenMap *locals;
    LLVMBasicBlockRef loop_cond_bb;
    LLVMBasicBlockRef loop_end_bb;
};

/* --- Internal Helpers --- */

CodegenMap   *codegen_map_create(CodegenMap *parent);
void          codegen_map_destroy(CodegenMap *m);
void          codegen_map_put(CodegenMap *m, void *key, LLVMValueRef val);
LLVMValueRef  codegen_map_get(CodegenMap *m, void *key);

LLVMTypeRef  get_llvm_type(CodegenContext *ctx, Type *t);
LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_lvalue(CodegenContext *ctx, AstNode *expr);

/* --- Sub-dispatchers for codegen_expr --- */

LLVMValueRef codegen_expr_literal(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_ident(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_ops(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_flow(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_stmt(CodegenContext *ctx, AstNode *expr);
LLVMValueRef codegen_expr_call(CodegenContext *ctx, AstNode *expr);

/* --- Decl logic --- */

void codegen_decl_proto(CodegenContext *ctx, AstNode *decl);
void codegen_decl_body(CodegenContext *ctx, AstNode *decl);
