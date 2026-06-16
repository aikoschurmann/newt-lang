#include "codegen_internal.h"
#include "dynamic_array.h"

CodegenMap* codegen_map_create(CodegenContext *ctx, CodegenMap *parent) {
    CodegenMap *m = xmalloc(sizeof(CodegenMap));
    m->map = hashmap_create(8);
    m->parent = parent;
    return m;
}

void codegen_map_destroy(CodegenMap *m) {
    if (!m) return;
    hashmap_destroy(m->map, NULL, NULL);
    free(m);
}

void codegen_map_put(CodegenMap *m, void *key, LLVMValueRef val) {
    hashmap_put(m->map, key, val, ptr_hash, ptr_cmp);
}

LLVMValueRef codegen_map_get(CodegenMap *m, void *key) {
    while (m) {
        LLVMValueRef val = hashmap_get(m->map, key, ptr_hash, ptr_cmp);
        if (val) return val;
        m = m->parent;
    }
    return NULL;
}

CodegenContext* codegen_context_create(TypeStore *store, const char *module_name, int opt_level, ModuleLoader *loader) {
    CodegenContext *ctx = xmalloc(sizeof(CodegenContext));
    ctx->store = store;
    ctx->context = LLVMContextCreate();
    ctx->module = LLVMModuleCreateWithNameInContext(module_name, ctx->context);
    ctx->builder = LLVMCreateBuilderInContext(ctx->context);
    ctx->loader = loader;

    // 1. Initialize Target
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    char *target_triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(ctx->module, target_triple);

    char *error = NULL;
    if (LLVMGetTargetFromTriple(target_triple, &ctx->target, &error) != 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "%s", error);
        LLVMDisposeMessage(error);
        ICE("LLVMGetTargetFromTriple failed: %s", err_msg);
    }

    // 2. Initialize Machine with Host CPU
    char *cpu = LLVMGetHostCPUName();
    char *features = LLVMGetHostCPUFeatures();

    ctx->machine = LLVMCreateTargetMachine(
        ctx->target, target_triple, cpu, features,
        LLVMCodeGenLevelAggressive, LLVMRelocDefault, LLVMCodeModelDefault
    );

    LLVMDisposeMessage(target_triple);
    LLVMDisposeMessage(cpu);
    LLVMDisposeMessage(features);

    if (!ctx->machine) {
        ICE("Failed to create LLVMTargetMachine");
    }

    // 3. Setup Target Data
    ctx->target_data = LLVMCreateTargetDataLayout(ctx->machine);
    LLVMSetModuleDataLayout(ctx->module, ctx->target_data);

    ctx->globals = hashmap_create(1024);
    ctx->type_cache = hashmap_create(256);
    ctx->locals = codegen_map_create(ctx, NULL);
    ctx->loop_cond_bb = NULL;
    ctx->loop_end_bb = NULL;
    ctx->opt_level = opt_level;
    ctx->current_func_type = NULL;
    ctx->sret_ptr = NULL;
    ctx->deferred_actions = xmalloc(sizeof(DynArray));
    dynarray_init(ctx->deferred_actions, sizeof(AstNode*));
    ctx->loop_defer_count = 0;
    return ctx;
}

void codegen_context_destroy(CodegenContext *ctx) {
    hashmap_destroy(ctx->globals, NULL, NULL);
    hashmap_destroy(ctx->type_cache, NULL, NULL);
    dynarray_free(ctx->deferred_actions);
    free(ctx->deferred_actions);
    LLVMDisposeTargetData(ctx->target_data);
    LLVMDisposeTargetMachine(ctx->machine);
    while (ctx->locals) {
        CodegenMap *parent = ctx->locals->parent;
        codegen_map_destroy(ctx->locals);
        ctx->locals = parent;
    }
    LLVMDisposeBuilder(ctx->builder);
    
    // CRITICAL FIX: Only dispose the module if the JIT Engine didn't take ownership.
    if (ctx->module) {
        LLVMDisposeModule(ctx->module);
    }
    
    LLVMContextDispose(ctx->context);
    free(ctx);
}
