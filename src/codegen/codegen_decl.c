#include "codegen_internal.h"
#include "sema/type_utils.h"

static void codegen_func_proto(CodegenContext *ctx, AstNode *decl) {
    AstFunctionDeclaration *fdecl       = &decl->data.function_declaration;
    Type                   *fn_type_sema = decl->type;

    if (!fn_type_sema || fn_type_sema->kind != TYPE_FUNCTION) {
        ICE("Function declaration missing type or has incorrect type kind: %d", fn_type_sema ? fn_type_sema->kind : -1);
    }

    // Return type: Use indirect return if large struct
    bool sret = type_is_indirect(ctx, fn_type_sema->as.func.return_type);
    LLVMTypeRef ret_type = sret ? LLVMVoidTypeInContext(ctx->context) : get_llvm_type(ctx, fn_type_sema->as.func.return_type);
    
    size_t param_count = fn_type_sema->as.func.param_count;
    size_t llvm_param_count = param_count + (sret ? 1 : 0);
    LLVMTypeRef *llvm_params = xmalloc(sizeof(LLVMTypeRef) * llvm_param_count);

    size_t idx = 0;
    if (sret) {
        llvm_params[idx++] = LLVMPointerType(get_llvm_type(ctx, fn_type_sema->as.func.return_type), 0);
    }

    for (size_t i = 0; i < param_count; i++) {
        Type *param_ty = fn_type_sema->as.func.params[i];
        if (type_is_indirect(ctx, param_ty)) {
            llvm_params[idx++] = LLVMPointerType(get_llvm_type(ctx, param_ty), 0);
        } else {
            llvm_params[idx++] = get_llvm_type(ctx, param_ty);
        }
    }

    const char *name           = "anon_func";
    char       *allocated_name = NULL;
    if (fdecl->intern_result && fdecl->intern_result->key) {
        CompilationUnit *u = module_loader_get_unit(ctx->loader, decl->filename);
        allocated_name = mangle_name(ctx, u, fdecl->intern_result, decl->type);
        name = allocated_name;
    }

    LLVMTypeRef func_type = LLVMFunctionType(ret_type, llvm_params, (unsigned)llvm_param_count, 0);
    LLVMValueRef func = LLVMAddFunction(ctx->module, name, func_type);
    
    // Add attributes for sret/byval
    if (sret) {
        LLVMAddAttributeAtIndex(func, 1, LLVMCreateTypeAttribute(ctx->context, 
            LLVMGetEnumAttributeKindForName("sret", 4), get_llvm_type(ctx, fn_type_sema->as.func.return_type)));
    }

    idx = sret ? 1 : 0;
    for (size_t i = 0; i < param_count; i++) {
        if (type_is_indirect(ctx, fn_type_sema->as.func.params[i])) {
            LLVMTypeRef param_ty = get_llvm_type(ctx, fn_type_sema->as.func.params[i]);
            LLVMAddAttributeAtIndex(func, (unsigned)(idx + i + 1), LLVMCreateTypeAttribute(ctx->context, 
                LLVMGetEnumAttributeKindForName("byval", 5), param_ty));
        }
    }

    if (allocated_name) free(allocated_name);
    if (llvm_params)     free(llvm_params);
}

static void codegen_var_proto(CodegenContext *ctx, AstNode *decl) {
    AstVariableDeclaration *vdecl = &decl->data.variable_declaration;
    if (!decl->type) ICE("Variable declaration missing type.");
    LLVMTypeRef ty = get_llvm_type(ctx, decl->type);

    const char *name           = "global_var";
    char       *allocated_name = NULL;
    if (vdecl->intern_result && vdecl->intern_result->key) {
        CompilationUnit *u = module_loader_get_unit(ctx->loader, decl->filename);
        allocated_name = mangle_name(ctx, u, vdecl->intern_result, NULL);
        name = allocated_name;
    }

    LLVMValueRef gvar = LLVMAddGlobal(ctx->module, ty, name);
    if (LLVMGetTypeKind(ty) != LLVMVoidTypeKind)
        LLVMSetInitializer(gvar, LLVMConstNull(ty));

    if (vdecl->intern_result) {
        hashmap_put(ctx->globals,
                    (void*)(intptr_t)vdecl->intern_result->entry->dense_index,
                    gvar, ptr_hash, ptr_cmp);
    }
    if (allocated_name) free(allocated_name);
}

void codegen_decl_proto(CodegenContext *ctx, AstNode *decl) {
    if (decl->node_type == AST_FUNCTION_DECLARATION) {
        codegen_func_proto(ctx, decl);
    } else if (decl->node_type == AST_VARIABLE_DECLARATION) {
        codegen_var_proto(ctx, decl);
    }
}

static void codegen_func_body(CodegenContext *ctx, AstNode *decl) {
    AstFunctionDeclaration *fdecl = &decl->data.function_declaration;
    Type *fn_type_sema = decl->type;

    const char *name           = "anon_func";
    char       *allocated_name = NULL;
    if (fdecl->intern_result && fdecl->intern_result->key) {
        CompilationUnit *u = module_loader_get_unit(ctx->loader, decl->filename);
        allocated_name = mangle_name(ctx, u, fdecl->intern_result, decl->type);
        name = allocated_name;
    }

    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, name);
    if (!func) ICE("codegen_decl_body: function '%s' not declared in proto pass", name);

    if (fdecl->body) {
        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        ctx->locals = codegen_map_create(ctx, ctx->locals);
        ctx->current_func_type = fn_type_sema;
        ctx->deferred_actions->count = 0; // Clear for new function

        ctx->current_cleanup_bb = NULL;
        ctx->exit_dest_var = create_entry_block_alloca(ctx, LLVMInt32TypeInContext(ctx->context), "exit_dest");
        
        bool sret = type_is_indirect(ctx, fn_type_sema->as.func.return_type);
        if (sret) {
            ctx->sret_ptr = LLVMGetParam(func, 0);
            ctx->ret_val_var = NULL;
        } else {
            ctx->sret_ptr = NULL;
            LLVMTypeRef ret_ty = get_llvm_type(ctx, fn_type_sema->as.func.return_type);
            if (LLVMGetTypeKind(ret_ty) != LLVMVoidTypeKind) {
                ctx->ret_val_var = create_entry_block_alloca(ctx, ret_ty, "ret_val");
            } else {
                ctx->ret_val_var = NULL;
            }
        }

        size_t param_count = fdecl->params ? fdecl->params->count : 0;
        size_t idx = sret ? 1 : 0;

        for (size_t i = 0; i < param_count; i++) {
            AstNode *param_node = *(AstNode**)dynarray_get(fdecl->params, i);
            AstParam *param = &param_node->data.param;
            LLVMValueRef val = LLVMGetParam(func, (unsigned int)idx++);
            Type *param_ty = fn_type_sema->as.func.params[i];
            
            LLVMValueRef storage;
            if (type_is_indirect(ctx, param_ty)) {
                storage = val;
            } else {
                LLVMTypeRef ty = get_llvm_type(ctx, param_node->type);
                storage = LLVMBuildAlloca(ctx->builder, ty, "param");
                LLVMBuildStore(ctx->builder, val, storage);
            }

            if (param->name_idx != -1) {
                codegen_map_put(ctx->locals, (void*)(intptr_t)param->name_idx, storage);
            }
        }
        
        codegen_statement(ctx, fdecl->body);

        LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
        if (current_block && !LLVMGetBasicBlockTerminator(current_block)) {
            bool sret_implicit = type_is_indirect(ctx, fn_type_sema->as.func.return_type);
            LLVMTypeRef ret_ty = sret_implicit ? LLVMVoidTypeInContext(ctx->context) : get_llvm_type(ctx, fn_type_sema->as.func.return_type);
            if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) LLVMBuildRetVoid(ctx->builder);
            else LLVMBuildRet(ctx->builder, LLVMConstNull(ret_ty));
        }

        CodegenMap *old = ctx->locals;
        ctx->locals = old->parent;
        codegen_map_destroy(old);
        ctx->current_func_type = NULL;
        ctx->sret_ptr = NULL;
        
    } else if (fdecl->link_name) {
        Slice *s = (Slice*)fdecl->link_name->key;
        char *ext_name = xmalloc(s->len + 1);
        memcpy(ext_name, s->ptr, s->len);
        ext_name[s->len] = '\0';

        LLVMSetLinkage(func, LLVMInternalLinkage);
        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        LLVMValueRef ext_func = LLVMGetNamedFunction(ctx->module, ext_name);
        if (!ext_func) ext_func = LLVMAddFunction(ctx->module, ext_name, LLVMGlobalGetValueType(func));

        size_t param_count = LLVMCountParams(func);
        LLVMValueRef *args = NULL;
        if (param_count > 0) {
            args = xmalloc(sizeof(LLVMValueRef) * param_count);
            for (size_t i = 0; i < param_count; i++) args[i] = LLVMGetParam(func, (unsigned int)i);
        }

        LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(func));
        if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) {
            LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ext_func), ext_func, args, (unsigned int)param_count, "");
            LLVMBuildRetVoid(ctx->builder);
        } else {
            LLVMValueRef call_res = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(ext_func), ext_func, args, (unsigned int)param_count, "ffi_call");
            LLVMBuildRet(ctx->builder, call_res);
        }
        if (args) free(args);
        free(ext_name);
    }
    if (allocated_name) free(allocated_name);
}

void codegen_decl_body(CodegenContext *ctx, AstNode *decl) {
    if (decl->node_type == AST_FUNCTION_DECLARATION) {
        codegen_func_body(ctx, decl);
    } else if (decl->node_type == AST_VARIABLE_DECLARATION) {
        AstVariableDeclaration *vdecl = &decl->data.variable_declaration;
        if (vdecl->initializer) {
            const char *name           = "global_var";
            char       *allocated_name = NULL;
            if (vdecl->intern_result && vdecl->intern_result->key) {
                CompilationUnit *u = module_loader_get_unit(ctx->loader, decl->filename);
                allocated_name = mangle_name(ctx, u, vdecl->intern_result, NULL);
                name = allocated_name;
            }
            LLVMValueRef gvar = LLVMGetNamedGlobal(ctx->module, name);
            if (gvar) {
                LLVMValueRef init_val = codegen_expr(ctx, vdecl->initializer);
                if (init_val && LLVMIsConstant(init_val)) LLVMSetInitializer(gvar, init_val);
            }
            if (allocated_name) free(allocated_name);
        }
    }
}
