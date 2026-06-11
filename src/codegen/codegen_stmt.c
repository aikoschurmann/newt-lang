#include "codegen_internal.h"

LLVMValueRef codegen_expr_stmt(CodegenContext *ctx, AstNode *expr) {
    if (expr->node_type == AST_BLOCK) {
        DynArray *stmts = expr->data.block.statements;
        LLVMValueRef last_val = NULL;

        ctx->locals = codegen_map_create(ctx->locals);

        for (size_t i = 0; i < stmts->count; i++) {
            AstNode *stmt = *(AstNode**)dynarray_get(stmts, i);
            last_val = codegen_expr(ctx, stmt);
        }

        CodegenMap *old = ctx->locals;
        ctx->locals = old->parent;
        codegen_map_destroy(old);

        return last_val;
    }

    if (expr->node_type == AST_RETURN_STATEMENT) {
        if (expr->data.return_statement.expression) {
            LLVMValueRef retval = codegen_expr(ctx, expr->data.return_statement.expression);
            if (!retval) {
                LLVMTypeRef ty = get_llvm_type(ctx, expr->data.return_statement.expression->type);
                if (LLVMGetTypeKind(ty) != LLVMVoidTypeKind)
                    retval = LLVMConstNull(ty);
            }
            if (retval) return LLVMBuildRet(ctx->builder, retval);
        }
        return LLVMBuildRetVoid(ctx->builder);
    }

    if (expr->node_type == AST_VARIABLE_DECLARATION) {
        AstVariableDeclaration *vdecl = &expr->data.variable_declaration;
        LLVMTypeRef  ty    = get_llvm_type(ctx, expr->type);
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, ty, "var");

        if (vdecl->intern_result) {
            void *key = (void*)(intptr_t)vdecl->intern_result->entry->dense_index;
            codegen_map_put(ctx->locals, key, alloca);
        }
        if (vdecl->initializer) {
            LLVMValueRef init_val = codegen_expr(ctx, vdecl->initializer);
            if (init_val) LLVMBuildStore(ctx->builder, init_val, alloca);
        }
        return alloca;
    }

    if (expr->node_type == AST_INITIALIZER_LIST) {
        AstInitializeList *init  = &expr->data.initializer_list;
        size_t             count = init->elements->count;
        LLVMValueRef *vals = malloc(sizeof(LLVMValueRef) * count);
        bool all_const = true;

        for (size_t i = 0; i < count; i++) {
            vals[i] = codegen_expr(ctx, *(AstNode**)dynarray_get(init->elements, i));
            if (!vals[i] || !LLVMIsAConstant(vals[i])) {
                all_const = false;
            }
        }

        LLVMTypeRef  ty  = get_llvm_type(ctx, expr->type);
        LLVMValueRef res = NULL;

        if (LLVMGetTypeKind(ty) == LLVMArrayTypeKind) {
            LLVMTypeRef elem_ty = LLVMGetElementType(ty);
            if (all_const) {
                res = LLVMConstArray(elem_ty, vals, count);
            } else {
                LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, ty, "array_tmp");
                for (size_t i = 0; i < count; i++) {
                    LLVMValueRef idxs[2] = {
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), (unsigned long long)i, 0)
                    };
                    LLVMValueRef gep = LLVMBuildGEP2(ctx->builder, ty, alloca, idxs, 2, "elem_ptr");
                    if (vals[i]) {
                        LLVMBuildStore(ctx->builder, vals[i], gep);
                    }
                }
                res = LLVMBuildLoad2(ctx->builder, ty, alloca, "array_val");
            }
        }
        free(vals);
        return res;
    }

    if (expr->node_type == AST_EXPR_STATEMENT) {
        return codegen_expr(ctx, expr->data.expr_statement.expression);
    }

    return NULL;
}
