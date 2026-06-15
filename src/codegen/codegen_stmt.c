#include "codegen_internal.h"
#include "dynamic_array.h"

static LLVMValueRef coerce_to_bool(CodegenContext *ctx, LLVMValueRef val) {
    LLVMTypeRef ty = LLVMTypeOf(val);
    LLVMTypeKind kind = LLVMGetTypeKind(ty);

    if (kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(ty) == 1) {
        return val;
    }

    if (kind == LLVMPointerTypeKind) {
        return LLVMBuildICmp(ctx->builder, LLVMIntNE, val, LLVMConstNull(ty), "cond");
    } else if (kind == LLVMIntegerTypeKind) {
        return LLVMBuildICmp(ctx->builder, LLVMIntNE, val, LLVMConstInt(ty, 0, 0), "cond");
    } else if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind) {
        return LLVMBuildFCmp(ctx->builder, LLVMRealUNE, val, LLVMConstNull(ty), "cond");
    }

    return val;
}

void codegen_statement(CodegenContext *ctx, AstNode *stmt) {
    if (!stmt) return;
    
    // Check if the current block is already terminated. If so, any further statements
    // in this block are unreachable dead code and must not be emitted.
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    if (current_block && LLVMGetBasicBlockTerminator(current_block)) {
        return;
    }
    
    // Strict contract: verify nodes have types, except for pure control-flow statements
    if (!stmt->type) {
        switch (stmt->node_type) {
            case AST_BLOCK:
            case AST_IF_STATEMENT:
            case AST_WHILE_STATEMENT:
            case AST_FOR_STATEMENT:
            case AST_RETURN_STATEMENT:
            case AST_BREAK_STATEMENT:
            case AST_CONTINUE_STATEMENT:
            case AST_DEFER_STATEMENT:
            case AST_EXPR_STATEMENT:
                break; // These are permitted to be untyped
            default:
                ICE_AT(stmt, "Statement node type %d missing type annotation.", stmt->node_type);
        }
    }

    switch (stmt->node_type) {
        case AST_BLOCK: {
            DynArray *stmts = stmt->data.block.statements;
            ctx->locals = codegen_map_create(ctx, ctx->locals);
            size_t previous_defer_count = ctx->deferred_actions->count;

            for (size_t i = 0; i < stmts->count; i++) {
                AstNode *s = *(AstNode**)dynarray_get(stmts, i);
                codegen_statement(ctx, s);
            }

            // Execute deferred actions in LIFO order
            for (int i = (int)ctx->deferred_actions->count - 1; i >= (int)previous_defer_count; i--) {
                AstNode *body = *(AstNode**)dynarray_get(ctx->deferred_actions, i);
                codegen_statement(ctx, body);
            }
            ctx->deferred_actions->count = previous_defer_count;

            CodegenMap *old = ctx->locals;
            ctx->locals = old->parent;
            codegen_map_destroy(old);
            break;
        }

        case AST_IF_STATEMENT: {
            AstIfStatement *ifs  = &stmt->data.if_statement;
            LLVMValueRef    cond = coerce_to_bool(ctx, codegen_expr(ctx, ifs->condition));

            LLVMValueRef     func     = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
            LLVMBasicBlockRef then_bb  = LLVMAppendBasicBlockInContext(ctx->context, func, "then");
            LLVMBasicBlockRef else_bb  = LLVMAppendBasicBlockInContext(ctx->context, func, "else");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "merge");

            LLVMBuildCondBr(ctx->builder, cond, then_bb, else_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
            codegen_statement(ctx, ifs->then_branch);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                LLVMBuildBr(ctx->builder, merge_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
            if (ifs->else_branch) codegen_statement(ctx, ifs->else_branch);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                LLVMBuildBr(ctx->builder, merge_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
            break;
        }

        case AST_WHILE_STATEMENT: {
            AstWhileStatement *whl = &stmt->data.while_statement;
            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "while.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "while.body");
            LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx->context, func, "while.end");

            LLVMBuildBr(ctx->builder, cond_bb);
            LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);

            LLVMValueRef cond = coerce_to_bool(ctx, codegen_expr(ctx, whl->condition));
            LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

            LLVMBasicBlockRef old_cond = ctx->loop_cond_bb;
            LLVMBasicBlockRef old_end  = ctx->loop_end_bb;
            size_t old_loop_defer      = ctx->loop_defer_count;

            ctx->loop_cond_bb = cond_bb;
            ctx->loop_end_bb  = end_bb;
            ctx->loop_defer_count = ctx->deferred_actions->count;

            LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
            codegen_statement(ctx, whl->body);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                LLVMBuildBr(ctx->builder, cond_bb);

            ctx->loop_cond_bb = old_cond;
            ctx->loop_end_bb  = old_end;
            ctx->loop_defer_count = old_loop_defer;

            LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
            break;
        }

        case AST_FOR_STATEMENT: {
            AstForStatement *fst  = &stmt->data.for_statement;
            LLVMValueRef     func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));

            ctx->locals = codegen_map_create(ctx, ctx->locals);
            if (fst->init) codegen_statement(ctx, fst->init);

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "for.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "for.body");
            LLVMBasicBlockRef post_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "for.post");
            LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx->context, func, "for.end");

            LLVMBuildBr(ctx->builder, cond_bb);
            LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);

            if (fst->condition) {
                LLVMValueRef cond = coerce_to_bool(ctx, codegen_expr(ctx, fst->condition));
                LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);
            } else {
                LLVMBuildBr(ctx->builder, body_bb);
            }

            LLVMBasicBlockRef old_cond = ctx->loop_cond_bb;
            LLVMBasicBlockRef old_end  = ctx->loop_end_bb;
            size_t old_loop_defer      = ctx->loop_defer_count;

            ctx->loop_cond_bb = post_bb;
            ctx->loop_end_bb  = end_bb;
            ctx->loop_defer_count = ctx->deferred_actions->count;

            LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
            codegen_statement(ctx, fst->body);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                LLVMBuildBr(ctx->builder, post_bb);

            LLVMPositionBuilderAtEnd(ctx->builder, post_bb);
            if (fst->post) codegen_expr(ctx, fst->post);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
                LLVMBuildBr(ctx->builder, cond_bb);

            ctx->loop_cond_bb = old_cond;
            ctx->loop_end_bb  = old_end;
            ctx->loop_defer_count = old_loop_defer;

            LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
            CodegenMap *old = ctx->locals;
            ctx->locals = old->parent;
            codegen_map_destroy(old);
            break;
        }

        case AST_RETURN_STATEMENT: {
            Type *fn_type = ctx->current_func_type;
            if (!fn_type) ICE("Return statement outside of function context.");
            
            LLVMValueRef retval = NULL;
            if (stmt->data.return_statement.expression) {
                retval = codegen_expr(ctx, stmt->data.return_statement.expression);
                if (!retval) {
                    LLVMTypeRef ty = get_llvm_type(ctx, stmt->data.return_statement.expression->type);
                    if (LLVMGetTypeKind(ty) != LLVMVoidTypeKind)
                        retval = LLVMConstNull(ty);
                }
            }

            // Execute defers in LIFO order. Pop them one by one so that if a defer 
            // contains a return, the recursive AST_RETURN_STATEMENT call will 
            // process the remaining defers correctly.
            while (ctx->deferred_actions->count > 0) {
                AstNode *body = *(AstNode**)dynarray_get(ctx->deferred_actions, ctx->deferred_actions->count - 1);
                ctx->deferred_actions->count--; // Pop before execution
                codegen_statement(ctx, body);
                
                current_block = LLVMGetInsertBlock(ctx->builder);
                if (current_block && LLVMGetBasicBlockTerminator(current_block)) {
                    // A defer terminated the block (e.g. it had a return).
                    // The recursive call will have executed the rest of the stack.
                    return;
                }
            }

            bool sret = type_is_indirect(ctx, fn_type->as.func.return_type);
            if (sret) {
                if (retval) LLVMBuildStore(ctx->builder, retval, ctx->sret_ptr);
                LLVMBuildRetVoid(ctx->builder);
                return;
            } else if (retval) {
                LLVMBuildRet(ctx->builder, retval);
                return;
            }

            LLVMBuildRetVoid(ctx->builder);
            break;
        }

        case AST_BREAK_STATEMENT: {
            if (ctx->loop_end_bb) {
                // For break/continue, we only execute defers belonging to the current loop body
                size_t original_count = ctx->deferred_actions->count;
                while (ctx->deferred_actions->count > ctx->loop_defer_count) {
                    AstNode *body = *(AstNode**)dynarray_get(ctx->deferred_actions, ctx->deferred_actions->count - 1);
                    ctx->deferred_actions->count--;
                    codegen_statement(ctx, body);
                    
                    LLVMBasicBlockRef stmt_block = LLVMGetInsertBlock(ctx->builder);
                    if (stmt_block && LLVMGetBasicBlockTerminator(stmt_block)) {
                        break;
                    }
                }
                // Restore count so outer scopes still run their defers upon normal exit
                ctx->deferred_actions->count = original_count;
                
                current_block = LLVMGetInsertBlock(ctx->builder);
                if (current_block && !LLVMGetBasicBlockTerminator(current_block)) {
                    LLVMBuildBr(ctx->builder, ctx->loop_end_bb);
                }
            }
            break;
        }

        case AST_CONTINUE_STATEMENT: {
            if (ctx->loop_cond_bb) {
                size_t original_count = ctx->deferred_actions->count;
                while (ctx->deferred_actions->count > ctx->loop_defer_count) {
                    AstNode *body = *(AstNode**)dynarray_get(ctx->deferred_actions, ctx->deferred_actions->count - 1);
                    ctx->deferred_actions->count--;
                    codegen_statement(ctx, body);
                    
                    LLVMBasicBlockRef stmt_block = LLVMGetInsertBlock(ctx->builder);
                    if (stmt_block && LLVMGetBasicBlockTerminator(stmt_block)) {
                        break;
                    }
                }
                ctx->deferred_actions->count = original_count;

                current_block = LLVMGetInsertBlock(ctx->builder);
                if (current_block && !LLVMGetBasicBlockTerminator(current_block)) {
                    LLVMBuildBr(ctx->builder, ctx->loop_cond_bb);
                }
            }
            break;
        }

        case AST_VARIABLE_DECLARATION: {
            AstVariableDeclaration *vdecl = &stmt->data.variable_declaration;
            if (!stmt->type) ICE_AT(stmt, "Variable declaration missing type.");

            LLVMTypeRef  ty    = get_llvm_type(ctx, stmt->type);
            LLVMValueRef alloca = create_entry_block_alloca(ctx, ty, "var");

            if (vdecl->intern_result) {
                void *key = (void*)(intptr_t)vdecl->intern_result->entry->dense_index;
                codegen_map_put(ctx->locals, key, alloca);
            }
            if (vdecl->initializer) {
                LLVMValueRef init_val = codegen_expr(ctx, vdecl->initializer);
                if (init_val) LLVMBuildStore(ctx->builder, init_val, alloca);
            }
            break;
        }

        case AST_EXPR_STATEMENT: {
            codegen_expr(ctx, stmt->data.expr_statement.expression);
            break;
        }

        case AST_DEFER_STATEMENT: {
            dynarray_push_value(ctx->deferred_actions, &stmt->data.defer_statement.body);
            break;
        }

        default:
            ICE_AT(stmt, "Unhandled statement type %d in codegen.", stmt->node_type);
    }
}
