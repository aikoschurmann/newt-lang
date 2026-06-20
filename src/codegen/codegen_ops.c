#include "codegen_internal.h"

static LLVMValueRef build_fat_ptr(CodegenContext *ctx, LLVMTypeRef dst_ty, LLVMValueRef ptr, size_t len) {
    LLVMTypeRef i64_ty = LLVMInt64TypeInContext(ctx->context);
    LLVMValueRef fat = LLVMGetUndef(dst_ty);
    fat = LLVMBuildInsertValue(ctx->builder, fat, ptr, 0, "fat_ptr_ptr");
    fat = LLVMBuildInsertValue(ctx->builder, fat, LLVMConstInt(i64_ty, len, 0), 1, "fat_ptr_len");
    return fat;
}

LLVMValueRef codegen_materialize_slice(CodegenContext *ctx, LLVMValueRef val, Type *src_type, Type *dst_type) {
    size_t n = src_type->as.array.size;
    Type *src_inner = src_type->as.array.base;
    Type *dst_inner = dst_type->as.slice.base;
    
    LLVMTypeRef dst_ty = get_llvm_type(ctx, dst_type);
    LLVMTypeRef i32_ty = LLVMInt32TypeInContext(ctx->context);

    // Check if the inner array also needs casting to a slice
    bool inner_needs_deep_cast = 
        src_inner->kind == TYPE_ARRAY && dst_inner->kind == TYPE_SLICE;

    if (!inner_needs_deep_cast) {
        // Base case (1D array): Just bitcast the pointer and pack it with length
        LLVMTypeRef elem_ptr_ty = LLVMStructGetTypeAtIndex(dst_ty, 0);
        LLVMValueRef elem_ptr = LLVMBuildBitCast(ctx->builder, val, elem_ptr_ty, "array_ptr_decay");
        return build_fat_ptr(ctx, dst_ty, elem_ptr, n);
    }

    // Recursive case (2D+ array): Allocate headers on stack
    LLVMTypeRef elem_slice_ty = get_llvm_type(ctx, dst_inner);
    LLVMTypeRef hdrs_arr_ty = LLVMArrayType(elem_slice_ty, n);
    LLVMTypeRef outer_arr_ty = get_llvm_type(ctx, src_type);
    
    LLVMValueRef hdrs = LLVMBuildAlloca(ctx->builder, hdrs_arr_ty, "slice_hdrs");

    // Loop through rows and materialize inner slices
    for (size_t i = 0; i < n; i++) {
        LLVMValueRef idx[] = { LLVMConstInt(i32_ty, 0, 0), LLVMConstInt(i32_ty, (unsigned)i, 0) };
        LLVMValueRef row_ptr = LLVMBuildGEP2(ctx->builder, outer_arr_ty, val, idx, 2, "row_ptr");
        
        LLVMValueRef row_fat = codegen_materialize_slice(ctx, row_ptr, src_inner, dst_inner);
        
        LLVMValueRef hdr_ptr = LLVMBuildGEP2(ctx->builder, hdrs_arr_ty, hdrs, idx, 2, "hdr_ptr");
        LLVMBuildStore(ctx->builder, row_fat, hdr_ptr);
    }

    // Pack the temporary headers into the outer slice
    return build_fat_ptr(ctx, dst_ty, hdrs, n);
}

LLVMValueRef codegen_expr_ops(CodegenContext *ctx, AstNode *expr) {
    if (expr->node_type == AST_CAST) {
        AstCastExpr *cast_node = &expr->data.cast_expr;
        LLVMValueRef val    = codegen_expr(ctx, cast_node->expr);
        LLVMTypeRef  dst_ty = get_llvm_type(ctx, cast_node->target_type);
        if (!val || !dst_ty) return val;

        LLVMTypeRef  src_ty = LLVMTypeOf(val);
        LLVMTypeKind src_k  = LLVMGetTypeKind(src_ty);
        LLVMTypeKind dst_k  = LLVMGetTypeKind(dst_ty);

        if (src_k == dst_k) {
            if (src_k == LLVMIntegerTypeKind) {
                unsigned src_w = LLVMGetIntTypeWidth(src_ty);
                unsigned dst_w = LLVMGetIntTypeWidth(dst_ty);
                if (src_w == dst_w) return val;
                if (src_w > dst_w)  return LLVMBuildTrunc(ctx->builder, val, dst_ty, "trunc");
                
                // Use ZExt for unsigned sources, SExt for signed
                if (type_is_unsigned(cast_node->expr->type)) {
                    return LLVMBuildZExt(ctx->builder, val, dst_ty, "zext");
                }
                return LLVMBuildSExt(ctx->builder, val, dst_ty, "sext");
            }

            // Special Case: T[N]* -> T[]* (Pointer to Array to Pointer to Slice)
            if (src_k == LLVMPointerTypeKind && 
                cast_node->expr->type->kind == TYPE_POINTER && 
                cast_node->target_type->kind == TYPE_POINTER) {
                
                Type *src_base = cast_node->expr->type->as.ptr.base;
                Type *dst_base = cast_node->target_type->as.ptr.base;
                
                if (src_base->kind == TYPE_ARRAY && dst_base->kind == TYPE_SLICE) {
                    
                    LLVMTypeRef slice_ty = get_llvm_type(ctx, dst_base);
                    LLVMValueRef slice_val = codegen_materialize_slice(ctx, val, src_base, dst_base);
                    LLVMValueRef slice_alloca = LLVMBuildAlloca(ctx->builder, slice_ty, "temp_slice_ptr_cast");
                    LLVMBuildStore(ctx->builder, slice_val, slice_alloca);

                    return slice_alloca;
                }
            }
        } else {
            // Special Case: Array to Slice (Fat Pointer)
            if (cast_node->expr->type->kind == TYPE_ARRAY && cast_node->target_type->kind == TYPE_SLICE) {
                // If val is not a pointer (e.g., an inline array value from an initializer list), spill it.
                if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) {
                    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, LLVMTypeOf(val), "array_spill");
                    LLVMBuildStore(ctx->builder, val, alloca);
                    val = alloca;
                }
                return codegen_materialize_slice(ctx, val, cast_node->expr->type, cast_node->target_type);
            }

            if (src_k == LLVMPointerTypeKind && dst_k == LLVMIntegerTypeKind)
                return LLVMBuildPtrToInt(ctx->builder, val, dst_ty, "ptrtoint");
            if (src_k == LLVMIntegerTypeKind && dst_k == LLVMPointerTypeKind)
                return LLVMBuildIntToPtr(ctx->builder, val, dst_ty, "inttoptr");
            if (src_k == LLVMIntegerTypeKind &&
                (dst_k == LLVMFloatTypeKind || dst_k == LLVMDoubleTypeKind))
                return LLVMBuildSIToFP(ctx->builder, val, dst_ty, "sitofp");
            if ((src_k == LLVMFloatTypeKind || src_k == LLVMDoubleTypeKind) &&
                dst_k == LLVMIntegerTypeKind)
                return LLVMBuildFPToSI(ctx->builder, val, dst_ty, "fptosi");
            if ((src_k == LLVMFloatTypeKind || src_k == LLVMDoubleTypeKind) &&
                (dst_k == LLVMFloatTypeKind || dst_k == LLVMDoubleTypeKind)) {
                if (dst_k == LLVMDoubleTypeKind)
                    return LLVMBuildFPExt(ctx->builder, val, dst_ty, "fpext");
                else
                    return LLVMBuildFPTrunc(ctx->builder, val, dst_ty, "fptrunc");
            }
        }
        return LLVMBuildBitCast(ctx->builder, val, dst_ty, "bitcast");
    }

    if (expr->node_type == AST_BINARY_EXPR) {
        LLVMValueRef L = codegen_expr(ctx, expr->data.binary_expr.left);
        LLVMValueRef R = codegen_expr(ctx, expr->data.binary_expr.right);
        if (!L || !R) return NULL;

        Type *ltype    = expr->data.binary_expr.left->type;
        int   is_float = (ltype && ltype->kind == TYPE_PRIMITIVE &&
                          (ltype->as.primitive == PRIM_F32 ||
                           ltype->as.primitive == PRIM_F64));

        switch (expr->data.binary_expr.op) {
            case OP_ADD:  return is_float ? LLVMBuildFAdd(ctx->builder, L, R, "addtmp") : LLVMBuildAdd(ctx->builder, L, R, "addtmp");
            case OP_SUB:  return is_float ? LLVMBuildFSub(ctx->builder, L, R, "subtmp") : LLVMBuildSub(ctx->builder, L, R, "subtmp");
            case OP_MUL:  return is_float ? LLVMBuildFMul(ctx->builder, L, R, "multmp") : LLVMBuildMul(ctx->builder, L, R, "multmp");
            case OP_DIV:  return is_float ? LLVMBuildFDiv(ctx->builder, L, R, "divtmp") : LLVMBuildSDiv(ctx->builder, L, R, "divtmp");
            case OP_MOD:  return is_float ? LLVMBuildFRem(ctx->builder, L, R, "modtmp") : LLVMBuildSRem(ctx->builder, L, R, "modtmp");
            case OP_EQ: {
                LLVMValueRef res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, L, R, "eqtmp")  : LLVMBuildICmp(ctx->builder, LLVMIntEQ,  L, R, "eqtmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_NEQ: {
                LLVMValueRef res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, L, R, "neqtmp") : LLVMBuildICmp(ctx->builder, LLVMIntNE,  L, R, "neqtmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_LT: {
                LLVMValueRef res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, L, R, "lttmp") : LLVMBuildICmp(ctx->builder, LLVMIntSLT, L, R, "lttmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_GT: {
                LLVMValueRef res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, L, R, "gttmp") : LLVMBuildICmp(ctx->builder, LLVMIntSGT, L, R, "gttmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_LE: {
                LLVMValueRef res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, L, R, "letmp") : LLVMBuildICmp(ctx->builder, LLVMIntSLE, L, R, "letmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_GE: {
                LLVMValueRef res = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, L, R, "getmp") : LLVMBuildICmp(ctx->builder, LLVMIntSGE, L, R, "getmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_AND: {
                LLVMValueRef res = LLVMBuildAnd(ctx->builder, L, R, "andtmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            case OP_OR: {
                LLVMValueRef res = LLVMBuildOr(ctx->builder, L, R, "ortmp");
                return LLVMBuildZExt(ctx->builder, res, get_llvm_type(ctx, expr->type), "bool_zext");
            }
            default: {
                ICE("codegen_expr_ops: unhandled binary operator %d", expr->data.binary_expr.op);
            }
        }
    }

    if (expr->node_type == AST_ASSIGNMENT_EXPR) {
        AstAssignmentExpr *assign = &expr->data.assignment_expr;
        LLVMValueRef ptr  = codegen_lvalue(ctx, assign->lvalue);
        LLVMValueRef rval = codegen_expr(ctx, assign->rvalue);
        if (!ptr || !rval) return NULL;

        if (assign->op == OP_ASSIGN) {
            LLVMBuildStore(ctx->builder, rval, ptr);
            return rval;
        }

        LLVMTypeRef  ty   = get_llvm_type(ctx, assign->lvalue->type);
        LLVMValueRef lval = LLVMBuildLoad2(ctx->builder, ty, ptr, "loadtmp");
        LLVMValueRef res  = NULL;

        Type *ltype    = assign->lvalue->type;
        int   is_float = (ltype && ltype->kind == TYPE_PRIMITIVE &&
                          (ltype->as.primitive == PRIM_F32 ||
                           ltype->as.primitive == PRIM_F64));

        switch (assign->op) {
            case OP_PLUS_EQ:  res = is_float ? LLVMBuildFAdd(ctx->builder, lval, rval, "addtmp") : LLVMBuildAdd(ctx->builder, lval, rval, "addtmp"); break;
            case OP_MINUS_EQ: res = is_float ? LLVMBuildFSub(ctx->builder, lval, rval, "subtmp") : LLVMBuildSub(ctx->builder, lval, rval, "subtmp"); break;
            case OP_MUL_EQ:   res = is_float ? LLVMBuildFMul(ctx->builder, lval, rval, "multmp") : LLVMBuildMul(ctx->builder, lval, rval, "multmp"); break;
            case OP_DIV_EQ:   res = is_float ? LLVMBuildFDiv(ctx->builder, lval, rval, "divtmp") : LLVMBuildSDiv(ctx->builder, lval, rval, "divtmp"); break;
            case OP_MOD_EQ:   res = is_float ? LLVMBuildFRem(ctx->builder, lval, rval, "modtmp") : LLVMBuildSRem(ctx->builder, lval, rval, "modtmp"); break;
            default:           res = rval; break;
        }
        if (res) LLVMBuildStore(ctx->builder, res, ptr);
        return res;
    }

    if (expr->node_type == AST_UNARY_EXPR) {
        AstUnaryExpr *ue = &expr->data.unary_expr;

        if (ue->op == OP_ADDRESS) {
            return codegen_lvalue(ctx, ue->expr);
        } else if (ue->op == OP_DEREF) {
            LLVMValueRef ptr = codegen_expr(ctx, ue->expr);
            return codegen_load_value(ctx, ptr, expr->type);
        } else if (ue->op == OP_SUB) {
            LLVMValueRef val = codegen_expr(ctx, ue->expr);
            LLVMTypeRef  ty  = LLVMTypeOf(val);
            if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind)
                return LLVMBuildNeg(ctx->builder, val, "negtmp");
            return LLVMBuildFNeg(ctx->builder, val, "fnegtmp");
        } else if (ue->op == OP_NOT) {
            LLVMValueRef val = codegen_expr(ctx, ue->expr);
            LLVMTypeRef ty = LLVMTypeOf(val);
            if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) {
                // Logical NOT: val == 0
                LLVMValueRef res = LLVMBuildICmp(ctx->builder, LLVMIntEQ, val, LLVMConstInt(ty, 0, 0), "nottmp");
                // Convert i1 result back to the original type (e.g. i8 for bool)
                return LLVMBuildZExt(ctx->builder, res, ty, "not_bool");
            }
            return LLVMBuildNot(ctx->builder, val, "nottmp");
        } 
 else if (ue->op == OP_POST_INC || ue->op == OP_POST_DEC ||
                   ue->op == OP_PRE_INC  || ue->op == OP_PRE_DEC) {
            LLVMValueRef ptr = codegen_lvalue(ctx, ue->expr);
            LLVMTypeRef  ty  = get_llvm_type(ctx, ue->expr->type);
            if (LLVMGetTypeKind(ty) == LLVMVoidTypeKind) return NULL;

            LLVMValueRef val     = LLVMBuildLoad2(ctx->builder, ty, ptr, "loadtmp");
            LLVMValueRef new_val = NULL;
            int is_float = (LLVMGetTypeKind(ty) == LLVMFloatTypeKind ||
                            LLVMGetTypeKind(ty) == LLVMDoubleTypeKind);

            if (!is_float) {
                if (ue->op == OP_POST_INC || ue->op == OP_PRE_INC)
                    new_val = LLVMBuildAdd(ctx->builder, val, LLVMConstInt(ty, 1, 0), "inc");
                else
                    new_val = LLVMBuildSub(ctx->builder, val, LLVMConstInt(ty, 1, 0), "dec");
            } else {
                if (ue->op == OP_POST_INC || ue->op == OP_PRE_INC)
                    new_val = LLVMBuildFAdd(ctx->builder, val, LLVMConstReal(ty, 1.0), "inc");
                else
                    new_val = LLVMBuildFSub(ctx->builder, val, LLVMConstReal(ty, 1.0), "dec");
            }
            if (new_val) LLVMBuildStore(ctx->builder, new_val, ptr);
            return (ue->op == OP_POST_INC || ue->op == OP_POST_DEC) ? val : new_val;
        }
        ICE("codegen_expr_ops: unhandled unary operator %d", ue->op);
    }

    ICE("codegen_expr_ops: unhandled node type %d", expr->node_type);
    return NULL;
}
