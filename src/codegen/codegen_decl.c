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

            ctx->locals = codegen_map_create(ctx->locals);

            // Register parameters
            size_t param_count = fdecl->params ? fdecl->params->count : 0;
            for (size_t i = 0; i < param_count; i++) {
                AstNode *param_node = *(AstNode**)dynarray_get(fdecl->params, i);
                AstParam *param = &param_node->data.param;
                
                LLVMValueRef val = LLVMGetParam(func, (unsigned int)i);
                LLVMTypeRef ty = get_llvm_type(ctx, param_node->type);
                
                LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, ty, "param");
                LLVMBuildStore(ctx->builder, val, alloca);

                if (param->name_idx != -1) {
                    codegen_map_put(ctx->locals, (void*)(intptr_t)param->name_idx, alloca);
                }
            }
            
            // Generate the user's actual code
            codegen_expr_stmt(ctx, fdecl->body);

            CodegenMap *old = ctx->locals;
            ctx->locals = old->parent;
            codegen_map_destroy(old);
            
            LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
            if (!LLVMGetBasicBlockTerminator(current_block)) {
                LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(func));
                if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) {
                    LLVMBuildRetVoid(ctx->builder);
                } else {
                    LLVMBuildRet(ctx->builder, LLVMConstNull(ret_ty));
                }
            }

        } else if (fdecl->link_name) {
            Slice *s = (Slice*)fdecl->link_name->key;
            char *ext_name = malloc(s->len + 1);
            memcpy(ext_name, s->ptr, s->len);
            ext_name[s->len] = '\0';

            Slice *ns = (Slice*)fdecl->intern_result->key;
            char *int_name = malloc(ns->len + 1);
            memcpy(int_name, ns->ptr, ns->len);
            int_name[ns->len] = '\0';

            if (strcmp(ext_name, int_name) != 0) {
                LLVMSetLinkage(func, LLVMInternalLinkage);

                LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
                LLVMPositionBuilderAtEnd(ctx->builder, entry);

                LLVMValueRef ext_func = LLVMGetNamedFunction(ctx->module, ext_name);
                if (!ext_func) {
                    ext_func = LLVMAddFunction(ctx->module, ext_name, LLVMGlobalGetValueType(func));
                }

                size_t param_count = LLVMCountParams(func);
                LLVMValueRef *args = NULL;
                if (param_count > 0) {
                    args = malloc(sizeof(LLVMValueRef) * param_count);
                    for (size_t i = 0; i < param_count; i++) {
                        args[i] = LLVMGetParam(func, (unsigned int)i);
                    }
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
            }
            free(ext_name);
            free(int_name);
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
