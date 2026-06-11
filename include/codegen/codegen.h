#pragma once

#include "ast.h"
#include "type.h"

// Forward declare the context to hide LLVM types from the header if possible,
// but since we return it or use it, we can just define a struct.
typedef struct CodegenContext CodegenContext;

CodegenContext* codegen_context_create(AstNode *program, TypeStore *store, const char *module_name, int opt_level);
void codegen_context_destroy(CodegenContext *ctx);

// Generates LLVM IR for the program. Returns 0 on success.
int codegen_program(CodegenContext *ctx);

// Prints the LLVM IR to stdout
void codegen_dump_module(CodegenContext *ctx);

// Emits the LLVM IR to a file
void codegen_emit_object(CodegenContext *ctx, const char *filename);
