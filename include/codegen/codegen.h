#pragma once

#include "ast.h"
#include "type.h"
#include "core/module_loader.h"

// Forward declare the context to hide LLVM types from the header if possible,
// but since we return it or use it, we can just define a struct.
typedef struct CodegenContext CodegenContext;

CodegenContext* codegen_context_create(TypeStore *store, const char *module_name, int opt_level, ModuleLoader *loader);
void codegen_context_destroy(CodegenContext *ctx);

// Global initialization of LLVM targets. Should be called once at startup.
void codegen_initialize(void);

// Generates LLVM IR for the program. Returns 0 on success.
int codegen_program(CodegenContext *ctx);

// Prints the LLVM IR to stdout
void codegen_dump_module(CodegenContext *ctx);

// Emits the LLVM IR to a file
void codegen_emit_object(CodegenContext *ctx, const char *filename);

// Runs the main function in the module using LLVM JIT and returns the exit code.
int codegen_run_jit(CodegenContext *ctx);
