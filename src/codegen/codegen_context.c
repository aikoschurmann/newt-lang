#include "codegen_internal.h"

CodegenMap* codegen_map_create(CodegenMap *parent) {
    CodegenMap *m = malloc(sizeof(CodegenMap));
    m->map = hashmap_create(1024);
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

CodegenContext* codegen_context_create(AstNode *program, TypeStore *store, const char *module_name, int opt_level) {  
    CodegenContext *ctx = malloc(sizeof(CodegenContext));
    ctx->program = program;
    ctx->store = store;
    ctx->context = LLVMContextCreate();
    ctx->module = LLVMModuleCreateWithNameInContext(module_name, ctx->context);
    ctx->builder = LLVMCreateBuilderInContext(ctx->context);
    ctx->globals = hashmap_create(1024);
    ctx->locals = codegen_map_create(NULL);
    ctx->loop_cond_bb = NULL;
    ctx->loop_end_bb = NULL;
    ctx->opt_level = opt_level;
    return ctx;
}

void codegen_context_destroy(CodegenContext *ctx) {
    hashmap_destroy(ctx->globals, NULL, NULL);
    while (ctx->locals) {
        CodegenMap *parent = ctx->locals->parent;
        codegen_map_destroy(ctx->locals);
        ctx->locals = parent;
    }
    LLVMDisposeBuilder(ctx->builder);
    LLVMDisposeModule(ctx->module);
    LLVMContextDispose(ctx->context);
    free(ctx);
}
