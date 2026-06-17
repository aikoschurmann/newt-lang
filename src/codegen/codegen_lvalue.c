/**
 * @file codegen_lvalue.c
 * @brief Generates LLVM IR for Left-Values (memory addresses).
 */

#include "codegen_internal.h"
#include "codegen/codegen_utils.h"

static LLVMValueRef codegen_lvalue_identifier(CodegenContext *ctx, AstNode *expr) {
    AstIdentifier *ident = &expr->data.identifier;
    
    // 1a. Try local variables first
    if (ident->intern_result) {
        void *key = (void*)(intptr_t)ident->intern_result->entry->dense_index;
        LLVMValueRef val = codegen_map_get(ctx->locals, key);
        if (val) return val;
    }

    // 1b. Try global symbols
    CompilationUnit *current_unit = module_loader_get_unit(ctx->loader, expr->filename);
    if (current_unit && current_unit->global_scope) {
        Symbol *sym = scope_lookup_symbol(current_unit->global_scope, ident->intern_result, expr->filename);
        if (sym) {
            while (sym && sym->kind == SYMBOL_VALUE_ALIAS) {
                sym = sym->target_symbol;
            }
            if (!sym) ICE_AT(expr, "Alias '%s' resolved to NULL.", ((Slice*)ident->intern_result->key)->ptr);

            CompilationUnit *origin_unit = module_loader_get_unit(ctx->loader, sym->filename);
            Type *fn_type = (sym->kind == SYMBOL_VALUE_FUNCTION) ? sym->type : NULL;
            char *mangled = mangle_name(ctx, origin_unit, sym->name_rec, fn_type);
            
            LLVMValueRef val = LLVMGetNamedFunction(ctx->module, mangled);
            if (!val) val = LLVMGetNamedGlobal(ctx->module, mangled);
            
            free(mangled);
            if (val) return val;
        }
    }

    ICE_AT(expr, "Identifier '%s' could not be resolved to an lvalue.", ((Slice*)ident->intern_result->key)->ptr);
    return NULL;
}

static LLVMValueRef codegen_lvalue_subscript(CodegenContext *ctx, AstNode *expr) {
    AstSubscriptExpr *sub = &expr->data.subscript_expr;
    Type *target_type = sub->target->type;
    LLVMValueRef idx = codegen_expr(ctx, sub->index);

    if (target_type->kind == TYPE_ARRAY) {
        LLVMValueRef target = codegen_lvalue(ctx, sub->target);
        LLVMTypeRef arr_ty = get_llvm_type(ctx, target_type);
        LLVMValueRef indices[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
            idx
        };
        return LLVMBuildInBoundsGEP2(ctx->builder, arr_ty, target, indices, 2, "arrayidx");
    } 
    else if (target_type->kind == TYPE_SLICE) {
        LLVMValueRef struct_ptr = codegen_lvalue(ctx, sub->target);
        LLVMTypeRef struct_ty = get_llvm_type(ctx, target_type);
        LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(ctx->builder, struct_ty, struct_ptr, 0, "data_ptr_ptr");
        LLVMTypeRef elem_ptr_ty = LLVMStructGetTypeAtIndex(struct_ty, 0);
        LLVMValueRef data_ptr = LLVMBuildLoad2(ctx->builder, elem_ptr_ty, data_ptr_ptr, "data_ptr");
        LLVMTypeRef elem_ty = get_llvm_type(ctx, target_type->as.slice.base);
        return LLVMBuildGEP2(ctx->builder, elem_ty, data_ptr, &idx, 1, "sliceidx");
    } 
    else if (target_type->kind == TYPE_POINTER) {
        LLVMValueRef target = codegen_expr(ctx, sub->target);
        LLVMTypeRef elem_ty = get_llvm_type(ctx, target_type->as.ptr.base);
        return LLVMBuildGEP2(ctx->builder, elem_ty, target, &idx, 1, "ptr_idx");
    }

    ICE_AT(sub->target, "Subscript target must be array, slice, or pointer");
    return NULL;
}

static LLVMValueRef codegen_lvalue_member(CodegenContext *ctx, AstNode *expr) {
    AstMemberExpr *mem_expr = &expr->data.member_expr;
    
    // Module/Namespace Access
    if (mem_expr->symbol) {
        CompilationUnit *u = module_loader_get_unit(ctx->loader, mem_expr->symbol->filename);
        Type *fn_type = (mem_expr->symbol->kind == SYMBOL_VALUE_FUNCTION) ? mem_expr->symbol->type : NULL;
        char *mangled = mangle_name(ctx, u, mem_expr->member, fn_type);
        LLVMValueRef val = LLVMGetNamedFunction(ctx->module, mangled);
        if (!val) val = LLVMGetNamedGlobal(ctx->module, mangled);
        free(mangled);
        return val;
    }

    Type *target_type = mem_expr->target->type;
    Type *underlying = (target_type->kind == TYPE_POINTER) ? target_type->as.ptr.base : target_type;
    LLVMValueRef target_lvalue = codegen_lvalue(ctx, mem_expr->target);

    if (target_type->kind == TYPE_POINTER) {
        LLVMTypeRef ptr_ty = get_llvm_type(ctx, target_type);
        target_lvalue = LLVMBuildLoad2(ctx->builder, ptr_ty, target_lvalue, "deref_ptr");
    }

    LLVMTypeRef struct_ty = get_llvm_type(ctx, underlying);
    if (underlying->kind == TYPE_SLICE) {
        unsigned idx = (mem_expr->member == ctx->store->kw_len) ? 1 : 0;
        return LLVMBuildStructGEP2(ctx->builder, struct_ty, target_lvalue, idx, "slice_gep");
    } 
    else if (underlying->kind == TYPE_STRUCT) {
        size_t idx;
        if (!get_struct_field_index(underlying, mem_expr->member, &idx)) ICE_AT(expr, "Field index not found");
        return LLVMBuildStructGEP2(ctx->builder, struct_ty, target_lvalue, (unsigned)idx, "field_gep");
    }

    ICE_AT(expr, "Member access requires struct or slice");
    return NULL;
}

static LLVMValueRef codegen_lvalue_literal(CodegenContext *ctx, AstNode *expr) {
    LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
    LLVMValueRef slot = LLVMBuildAlloca(ctx->builder, ty, "literal_mem");
    LLVMValueRef val = codegen_expr(ctx, expr);
    LLVMBuildStore(ctx->builder, val, slot);
    return slot;
}

LLVMValueRef codegen_lvalue(CodegenContext *ctx, AstNode *expr) {
    if (!expr) return NULL;
    if (!expr->type) ICE_AT(expr, "LValue node missing type.");

    switch (expr->node_type) {
        case AST_IDENTIFIER:       return codegen_lvalue_identifier(ctx, expr);
        case AST_SUBSCRIPT_EXPR:   return codegen_lvalue_subscript(ctx, expr);
        case AST_MEMBER_EXPR:      return codegen_lvalue_member(ctx, expr);
        case AST_UNARY_EXPR:
            if (expr->data.unary_expr.op == OP_DEREF) {
                return codegen_expr(ctx, expr->data.unary_expr.expr);
            }
            break;
        case AST_STRUCT_LITERAL:
        case AST_INITIALIZER_LIST: return codegen_lvalue_literal(ctx, expr);
        default: break;
    }

    ICE_AT(expr, "Unhandled lvalue node type %d", expr->node_type);
    return NULL;
}
