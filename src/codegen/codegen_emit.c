#include "codegen_internal.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Support.h>

void codegen_initialize() {
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();
    LLVMLinkInMCJIT();
    LLVMLoadLibraryPermanently(NULL);
}

static void run_optimizations(CodegenContext *ctx, LLVMTargetMachineRef machine) {
    if (ctx->opt_level <= 0) return;

    char passes[32];
    snprintf(passes, sizeof(passes), "default<O%d>", ctx->opt_level);

    LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
    LLVMRunPasses(ctx->module, passes, machine, opts);
    LLVMDisposePassBuilderOptions(opts);
}

int codegen_program(CodegenContext *ctx) {
    if (!ctx->program || ctx->program->node_type != AST_PROGRAM) return -1;

    DynArray *decls = ctx->program->data.program.decls;
    if (!decls) return 0;

    for (size_t i = 0; i < decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(decls, i);
        codegen_decl_proto(ctx, decl);
    }
    for (size_t i = 0; i < decls->count; i++) {
        AstNode *decl = *(AstNode**)dynarray_get(decls, i);
        codegen_decl_body(ctx, decl);
    }

    char *target_triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(ctx->module, target_triple);

    char        *error  = NULL;
    LLVMTargetRef target;
    if (LLVMGetTargetFromTriple(target_triple, &target, &error)) {
        fprintf(stderr, "Error getting target: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(target_triple);
        return -1;
    }

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, target_triple, "generic", "",
        LLVMCodeGenLevelAggressive, LLVMRelocDefault, LLVMCodeModelDefault);

    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(machine);
    LLVMSetModuleDataLayout(ctx->module, data_layout);

    // Run optimizations
    run_optimizations(ctx, machine);

    LLVMDisposeTargetData(data_layout);
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(target_triple);

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
    char *target_triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *error = NULL;
    LLVMGetTargetFromTriple(target_triple, &target, &error);

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, target_triple, "generic", "",
        LLVMCodeGenLevelAggressive, LLVMRelocDefault, LLVMCodeModelDefault);

    if (LLVMTargetMachineEmitToFile(machine, ctx->module, (char *)filename,
                                    LLVMObjectFile, &error)) {
        fprintf(stderr, "Error emitting object file: %s\n", error);
        LLVMDisposeMessage(error);
    }

    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(target_triple);
}

int codegen_run_jit(CodegenContext *ctx) {
    LLVMExecutionEngineRef engine;
    char *error = NULL;

    if (LLVMCreateExecutionEngineForModule(&engine, ctx->module, &error) != 0) {
        fprintf(stderr, "Failed to create execution engine: %s\n", error);
        LLVMDisposeMessage(error);
        return -1;
    }

    LLVMValueRef main_func = LLVMGetNamedFunction(ctx->module, "main");
    if (!main_func) {
        fprintf(stderr, "Could not find main function\n");
        LLVMRemoveModule(engine, ctx->module, &ctx->module, &error);
        LLVMDisposeExecutionEngine(engine);
        return -1;
    }

    uint64_t addr = LLVMGetFunctionAddress(engine, "main");
    int (*main_ptr)() = (int (*)())addr;
    int result = main_ptr();

    // Remove the module from the engine so that LLVMDisposeExecutionEngine 
    // doesn't dispose it. CodegenContext still owns the module.
    LLVMModuleRef removed_module;
    if (LLVMRemoveModule(engine, ctx->module, &removed_module, &error) != 0) {
        fprintf(stderr, "Failed to remove module from JIT: %s\n", error);
        LLVMDisposeMessage(error);
    }

    LLVMDisposeExecutionEngine(engine);
    return result;
}
