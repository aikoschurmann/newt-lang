#include "codegen_internal.h"

LLVMValueRef codegen_expr_literal(CodegenContext *ctx, AstNode *expr) {
    if (expr->data.literal.type == INT_LITERAL) {
        LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
        return LLVMConstInt(ty, expr->data.literal.value.int_val, 0);
    } else if (expr->data.literal.type == CHAR_LITERAL) {
        LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
        return LLVMConstInt(ty, (unsigned char)expr->data.literal.value.char_val, 0);
    } else if (expr->data.literal.type == BOOL_LITERAL) {
        LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
        return LLVMConstInt(ty, expr->data.literal.value.bool_val, 0);
    } else if (expr->data.literal.type == FLOAT_LITERAL) {
        LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
        return LLVMConstReal(ty, expr->data.literal.value.float_val);
    } else if (expr->data.literal.type == STRING_LITERAL) {
        InternResult *res = expr->data.literal.value.string_val;
        Slice *s = (Slice*)res->key;
        char *str = strndup(s->ptr, s->len);
        LLVMValueRef gstr = LLVMBuildGlobalStringPtr(ctx->builder, str, "str_lit");
        free(str);
        return gstr;
    }
    return NULL;
}

LLVMValueRef codegen_expr_ident(CodegenContext *ctx, AstNode *expr) {
    LLVMValueRef ptr = codegen_lvalue(ctx, expr);
    if (!ptr) return NULL;

    /* Functions are values, not stack slots — return directly. */
    if (LLVMGetTypeKind(LLVMTypeOf(ptr)) == LLVMFunctionTypeKind ||
        (expr->type && expr->type->kind == TYPE_FUNCTION)) {
        return ptr;
    }

    LLVMTypeRef ty = get_llvm_type(ctx, expr->type);

    if (LLVMGetTypeKind(ty) == LLVMVoidTypeKind) return ptr;

    if (expr->type && expr->type->kind == TYPE_ARRAY) {
        if (expr->type->as.array.size_known) {
            /* Decay: return the alloca pointer itself; callers
               that need an element use codegen_lvalue / GEP. */
            return ptr;
        }
    }

    return LLVMBuildLoad2(ctx->builder, ty, ptr, "loadtmp");
}

LLVMValueRef codegen_expr_member(CodegenContext *ctx, AstNode *expr) {
    AstMemberExpr *mem_expr = &expr->data.member_expr;
    Type *target_type = mem_expr->target->type;

    if (target_type->kind == TYPE_ARRAY) {
        if (target_type->as.array.size_known) {
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), target_type->as.array.size, 0);
        }
        
        LLVMValueRef target_lvalue = codegen_lvalue(ctx, mem_expr->target);
        if (!target_lvalue) return NULL;

        LLVMTypeRef struct_ty = get_llvm_type(ctx, target_type);
        LLVMValueRef len_ptr = LLVMBuildStructGEP2(ctx->builder, struct_ty, target_lvalue, 1, "len_gep");
        return LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), len_ptr, "len_val");
    }
    
    Type *underlying = target_type;
    if (underlying->kind == TYPE_POINTER) underlying = underlying->as.ptr.base;

    if (underlying->kind == TYPE_STRUCT) {
        LLVMValueRef target_lvalue = codegen_lvalue(ctx, mem_expr->target);
        if (!target_lvalue) return NULL;

        // If target was a pointer, we need to load it first to get the struct pointer
        if (target_type->kind == TYPE_POINTER) {
            LLVMTypeRef ptr_ty = get_llvm_type(ctx, target_type);
            target_lvalue = LLVMBuildLoad2(ctx->builder, ptr_ty, target_lvalue, "deref_ptr");
        }

        LLVMTypeRef struct_ty = get_llvm_type(ctx, underlying);
        LLVMValueRef field_ptr = LLVMBuildStructGEP2(ctx->builder, struct_ty, target_lvalue, mem_expr->field_index, "field_gep");
        
        LLVMTypeRef field_ty = get_llvm_type(ctx, expr->type);
        return LLVMBuildLoad2(ctx->builder, field_ty, field_ptr, "field_val");
    }

    return NULL;
}

LLVMValueRef codegen_expr_struct_literal(CodegenContext *ctx, AstNode *expr) {
    AstStructLiteral *lit = &expr->data.struct_literal;
    size_t count = lit->fields ? lit->fields->count : 0;
    LLVMTypeRef struct_ty = get_llvm_type(ctx, expr->type);

    size_t field_count = expr->type->as.struct_type.field_count;
    LLVMValueRef *vals = malloc(sizeof(LLVMValueRef) * field_count);
    bool all_const = true;

    for (size_t i = 0; i < field_count; i++) {
        vals[i] = NULL;
        InternResult *name = expr->type->as.struct_type.fields[i].name;
        for (size_t j = 0; j < count; j++) {
            AstFieldInit *init = (AstFieldInit*)dynarray_get(lit->fields, j);
            if (init->name == name) {
                vals[i] = codegen_expr(ctx, init->expr);
                break;
            }
        }
        if (!vals[i]) {
            vals[i] = LLVMConstNull(get_llvm_type(ctx, expr->type->as.struct_type.fields[i].type));
        }
        if (!LLVMIsAConstant(vals[i])) {
            all_const = false;
        }
    }

    if (all_const) {
        LLVMValueRef res = LLVMConstNamedStruct(struct_ty, vals, (unsigned int)field_count);
        free(vals);
        return res;
    }

    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, struct_ty, "struct_tmp");
    for (size_t i = 0; i < field_count; i++) {
        LLVMValueRef gep = LLVMBuildStructGEP2(ctx->builder, struct_ty, alloca, (unsigned int)i, "field_ptr");
        LLVMBuildStore(ctx->builder, vals[i], gep);
    }
    
    free(vals);
    return LLVMBuildLoad2(ctx->builder, struct_ty, alloca, "struct_val");
}

LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *expr) {
    if (!expr) return NULL;

    switch (expr->node_type) {
        case AST_LITERAL:          return codegen_expr_literal(ctx, expr);
        case AST_IDENTIFIER:       return codegen_expr_ident(ctx, expr);
        case AST_MEMBER_EXPR:      return codegen_expr_member(ctx, expr);
        case AST_STRUCT_LITERAL:   return codegen_expr_struct_literal(ctx, expr);
        case AST_SUBSCRIPT_EXPR: {
            LLVMValueRef ptr = codegen_lvalue(ctx, expr);
            if (!ptr) return NULL;

            if (expr->type && expr->type->kind == TYPE_ARRAY) {
                if (expr->type->as.array.size_known) {
                    return ptr; // Decay
                } else {
                    LLVMTypeRef struct_ty = get_llvm_type(ctx, expr->type);
                    return LLVMBuildLoad2(ctx->builder, struct_ty, ptr, "slice_load");
                }
            }

            LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
            if (LLVMGetTypeKind(ty) == LLVMVoidTypeKind) return NULL;
            return LLVMBuildLoad2(ctx->builder, ty, ptr, "loadtmp");
        }
        case AST_CALL_EXPR:        return codegen_expr_call(ctx, expr);
        case AST_BINARY_EXPR:
        case AST_UNARY_EXPR:
        case AST_CAST:
        case AST_ASSIGNMENT_EXPR:  return codegen_expr_ops(ctx, expr);
        case AST_IF_STATEMENT:
        case AST_WHILE_STATEMENT:
        case AST_FOR_STATEMENT:
        case AST_BREAK_STATEMENT:
        case AST_CONTINUE_STATEMENT: return codegen_expr_flow(ctx, expr);
        case AST_BLOCK:
        case AST_RETURN_STATEMENT:
        case AST_VARIABLE_DECLARATION:
        case AST_INITIALIZER_LIST:
        case AST_EXPR_STATEMENT:   return codegen_expr_stmt(ctx, expr);
        default: {
            LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
            if (LLVMGetTypeKind(ty) == LLVMVoidTypeKind) return NULL;
            return LLVMConstNull(ty);
        }
    }
}
