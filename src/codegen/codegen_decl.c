#include "codegen_internal.h"

void codegen_decl_proto(CodegenContext *ctx, AstNode *decl) {
    if (decl->node_type == AST_FUNCTION_DECLARATION) {
        AstFunctionDeclaration *fdecl       = &decl->data.function_declaration;
        Type                   *fn_type_sema = decl->type;

        LLVMTypeRef  ret_type   = LLVMVoidTypeInContext(ctx->context);
        LLVMTypeRef *param_types = NULL;
        size_t       param_count = 0;

        if (fn_type_sema && fn_type_sema->kind == TYPE_FUNCTION) {
            ret_type    = get_llvm_type(ctx, fn_type_sema->as.func.return_type);
            param_count = fn_type_sema->as.func.param_count;
            if (param_count > 0) {
                param_types = malloc(sizeof(LLVMTypeRef) * param_count);
                for (size_t i = 0; i < param_count; i++) {
                    param_types[i] = get_llvm_type(ctx, fn_type_sema->as.func.params[i]);
                }
            }
        }

        const char *name           = "anon_func";
        char       *allocated_name = NULL;
        if (fdecl->intern_result && fdecl->intern_result->key) {
            Slice *s = (Slice*)fdecl->intern_result->key;
            allocated_name = strndup(s->ptr, s->len);
            name = allocated_name;
        }

        LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, param_count, 0);
        LLVMAddFunction(ctx->module, name, func_type);

        if (allocated_name) free(allocated_name);
        if (param_types)     free(param_types);

    } else if (decl->node_type == AST_VARIABLE_DECLARATION) {
        AstVariableDeclaration *vdecl = &decl->data.variable_declaration;
        LLVMTypeRef ty = get_llvm_type(ctx, decl->type);

        const char *name           = "global_var";
        char       *allocated_name = NULL;
        if (vdecl->intern_result && vdecl->intern_result->key) {
            Slice *s = (Slice*)vdecl->intern_result->key;
            allocated_name = strndup(s->ptr, s->len);
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
}

void codegen_decl_body(CodegenContext *ctx, AstNode *decl) {
    if (decl->node_type == AST_FUNCTION_DECLARATION) {
        AstFunctionDeclaration *fdecl = &decl->data.function_declaration;

        const char *name           = "anon_func";
        char       *allocated_name = NULL;
        if (fdecl->intern_result && fdecl->intern_result->key) {
            Slice *s = (Slice*)fdecl->intern_result->key;
            allocated_name = strndup(s->ptr, s->len);
            name = allocated_name;
        }

        LLVMValueRef func = LLVMGetNamedFunction(ctx->module, name);

        if (fdecl->body) {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, entry);

            while (ctx->locals->parent) {
                CodegenMap *old = ctx->locals;
                ctx->locals = old->parent;
                codegen_map_destroy(old);
            }
            hashmap_destroy(ctx->locals->map, NULL, NULL);
            ctx->locals->map = hashmap_create(1024);

            size_t param_count = fdecl->params ? fdecl->params->count : 0;
            for (size_t i = 0; i < param_count; i++) {
                LLVMValueRef arg   = LLVMGetParam(func, i);
                AstParam    *pnode = &(*(AstNode**)dynarray_get(fdecl->params, i))->data.param;

                if (pnode->name_idx >= 0) {
                    LLVMTypeRef  ty    = LLVMTypeOf(arg);
                    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, ty, "arg");
                    LLVMBuildStore(ctx->builder, arg, alloca);
                    codegen_map_put(ctx->locals,
                                (void*)(intptr_t)pnode->name_idx,
                                alloca);
                }
            }

            codegen_expr(ctx, fdecl->body);

            LLVMBasicBlockRef insert_bb = LLVMGetInsertBlock(ctx->builder);
            if (!LLVMGetBasicBlockTerminator(insert_bb)) {
                LLVMTypeRef func_ty = LLVMGlobalGetValueType(func);
                LLVMTypeRef r_type  = LLVMGetReturnType(func_ty);
                if (LLVMGetTypeKind(r_type) == LLVMVoidTypeKind)
                    LLVMBuildRetVoid(ctx->builder);
                else
                    LLVMBuildRet(ctx->builder, LLVMConstNull(r_type));
            }
        }
        if (allocated_name) free(allocated_name);

    } else if (decl->node_type == AST_VARIABLE_DECLARATION) {
        AstVariableDeclaration *vdecl = &decl->data.variable_declaration;
        if (vdecl->initializer) {
            const char *name           = "global_var";
            char       *allocated_name = NULL;
            if (vdecl->intern_result && vdecl->intern_result->key) {
                Slice *s = (Slice*)vdecl->intern_result->key;
                allocated_name = strndup(s->ptr, s->len);
                name = allocated_name;
            }
            LLVMValueRef gvar = LLVMGetNamedGlobal(ctx->module, name);
            if (gvar) {
                LLVMValueRef init_val = codegen_expr(ctx, vdecl->initializer);
                if (init_val && LLVMIsConstant(init_val))
                    LLVMSetInitializer(gvar, init_val);
            }
            if (allocated_name) free(allocated_name);
        }
    }
}
