#include "codegen_internal.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Support.h>

void codegen_initialize(void) {
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();
    LLVMLinkInMCJIT();
    LLVMLoadLibraryPermanently(NULL);
}

static void run_optimizations(CodegenContext *ctx) {
    if (ctx->opt_level <= 0) return;

    char passes[32];
    snprintf(passes, sizeof(passes), "default<O%d>", ctx->opt_level);

    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(ctx->module, passes, ctx->machine, opts);
    LLVMDisposePassBuilderOptions(opts);
}

int codegen_program(CodegenContext *ctx) {
    if (!ctx || !ctx->loader || !ctx->loader->units_ordered) return -1;

    // Pass 1: Protos (All units)
    for (size_t i = 0; i < ctx->loader->units_ordered->count; i++) {
        CompilationUnit *unit = *(CompilationUnit**)dynarray_get(ctx->loader->units_ordered, i);
        if (!unit->ast_root) continue;
        
        AstProgram *prog = &unit->ast_root->data.program;
        if (!prog->decls) continue;

        for (size_t j = 0; j < prog->decls->count; j++) {
            AstNode *decl = *(AstNode**)dynarray_get(prog->decls, j);
            if (decl->node_type != AST_IMPORT_DECLARATION) {
                codegen_decl_proto(ctx, decl);
            }
        }
    }
    
    // Pass 2: Bodies (All units)
    for (size_t i = 0; i < ctx->loader->units_ordered->count; i++) {
        CompilationUnit *unit = *(CompilationUnit**)dynarray_get(ctx->loader->units_ordered, i);
        if (!unit->ast_root) continue;

        AstProgram *prog = &unit->ast_root->data.program;
        if (!prog->decls) continue;

        for (size_t j = 0; j < prog->decls->count; j++) {
            AstNode *decl = *(AstNode**)dynarray_get(prog->decls, j);
            if (decl->node_type != AST_IMPORT_DECLARATION) {
                codegen_decl_body(ctx, decl);
            }
        }
    }

    // Run optimizations
    run_optimizations(ctx);

    char *v_error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMPrintMessageAction, &v_error) == 1) {
        fprintf(stderr, "LLVM Verification Error: %s\n", v_error ? v_error : "Unknown");
        if (v_error) LLVMDisposeMessage(v_error);
        return -1;
    }
    if (v_error) LLVMDisposeMessage(v_error);
    return 0;
}

void codegen_dump_module(CodegenContext *ctx) {
    LLVMDumpModule(ctx->module);
}

void codegen_emit_object(CodegenContext *ctx, const char *filename) {
    char *error = NULL;
    if (LLVMTargetMachineEmitToFile(ctx->machine, ctx->module, (char *)filename,
                                    LLVMObjectFile, &error)) {
        fprintf(stderr, "Error emitting object file: %s\n", error);
        LLVMDisposeMessage(error);
    }
}

int codegen_run_jit(CodegenContext *ctx) {
    LLVMExecutionEngineRef engine;
    char *error = NULL;

    if (LLVMCreateExecutionEngineForModule(&engine, ctx->module, &error) != 0) {
        fprintf(stderr, "Failed to create execution engine: %s\n", error);
        LLVMDisposeMessage(error);
        return -1;
    }

    LLVMValueRef main_func = LLVMGetNamedFunction(engine ? ctx->module : NULL, "main");
    
    if (!main_func) {
        fprintf(stderr, "Could not find main function\n");
        LLVMRemoveModule(engine, ctx->module, &ctx->module, &error);
        LLVMDisposeExecutionEngine(engine);
        return -1;
    }

    uint64_t addr = LLVMGetFunctionAddress(engine, "main");
    int (*main_ptr)(void) = (int (*)(void))addr;
    int result = main_ptr();

    LLVMModuleRef removed_module;
    if (LLVMRemoveModule(engine, ctx->module, &removed_module, &error) != 0) {
        fprintf(stderr, "Failed to remove module from JIT: %s\n", error);
        LLVMDisposeMessage(error);
    }

    LLVMDisposeExecutionEngine(engine);
    return result;
}
