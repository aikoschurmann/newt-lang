/**
 * @file codegen_expr.c
 * @brief Generates LLVM IR for expressions.
 * * This phase is now strictly separated from semantic analysis. It dynamically 
 * calculates struct layouts and memory indices on the fly, ensuring changes 
 * to LLVM layouts don't require rewriting the typechecker.
 */

#include "codegen_internal.h"
#include "codegen/codegen_utils.h"

// Forward declaration
LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *expr);

// =============================================================================
// SECTION 1: INTRINSICS
// =============================================================================

static LLVMValueRef codegen_expr_intrinsic(CodegenContext *ctx, AstNode *expr) {
    if (!expr->type) ICE("Intrinsic node (kind %d) missing type.", expr->data.intrinsic.kind);
    IntrinsicKind kind = expr->data.intrinsic.kind;
    DynArray *args = expr->data.intrinsic.args;

    // Standard types used across memory intrinsics
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef i64ty = LLVMInt64TypeInContext(ctx->context);

    if (kind == INTRINSIC_ALLOC) {
        AstNode *allocator_arg = *(AstNode**)dynarray_get(args, 1);
        AstNode *count_arg = args->count == 3 ? *(AstNode**)dynarray_get(args, 2) : NULL;

        // 1. Target Type Extraction
        Type *target_type = expr->type;
        if (target_type->kind == TYPE_POINTER) target_type = target_type->as.ptr.base;
        LLVMTypeRef llvm_target_type = get_llvm_type(ctx, target_type);

        // 2. Compute Allocation Size (count * sizeof(T))
        LLVMValueRef allocator_val = codegen_expr(ctx, allocator_arg);
        LLVMValueRef count_val = count_arg ? codegen_expr(ctx, count_arg) : LLVMConstInt(i64ty, 1, 0);
        
        LLVMValueRef total_bytes = LLVMBuildMul(ctx->builder,
            LLVMBuildIntCast(ctx->builder, count_val, i64ty, "count_i64"),
            LLVMSizeOf(llvm_target_type), "alloc_bytes");

        // 3. Extract Context and Function Pointer from Allocator Interface
        size_t ctx_idx = struct_field_index(allocator_arg->type, "ctx");
        size_t alloc_idx = struct_field_index(allocator_arg->type, "alloc");
        if (ctx_idx == (size_t)-1 || alloc_idx == (size_t)-1) ICE("@alloc allocator missing required fields");

        LLVMValueRef ctx_val = LLVMBuildExtractValue(ctx->builder, allocator_val, (unsigned)ctx_idx, "ctx_val");
        LLVMValueRef alloc_fn = LLVMBuildExtractValue(ctx->builder, allocator_val, (unsigned)alloc_idx, "alloc_fn_val");

        // 4. Invoke Custom Allocator
        LLVMTypeRef alloc_fn_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64ty}, 2, 0);
        LLVMValueRef call_args[] = { ctx_val, total_bytes };
        LLVMValueRef raw_mem = LLVMBuildCall2(ctx->builder, alloc_fn_ty, alloc_fn, call_args, 2, "raw_mem");

        return LLVMBuildBitCast(ctx->builder, raw_mem, LLVMPointerType(llvm_target_type, 0), "typed_mem");
    }
    else if (kind == INTRINSIC_FREE) {
        AstNode *allocator_arg = *(AstNode**)dynarray_get(args, args->count == 3 ? 1 : 0);
        AstNode *ptr_arg = *(AstNode**)dynarray_get(args, args->count == 3 ? 2 : 1);

        LLVMValueRef allocator_val = codegen_expr(ctx, allocator_arg);
        LLVMValueRef ptr_val = codegen_expr(ctx, ptr_arg);

        // Extract Context and Free Function
        size_t ctx_idx = struct_field_index(allocator_arg->type, "ctx");
        size_t free_idx = struct_field_index(allocator_arg->type, "free");
        if (ctx_idx == (size_t)-1 || free_idx == (size_t)-1) ICE("@free allocator missing required fields");

        LLVMValueRef ctx_val = LLVMBuildExtractValue(ctx->builder, allocator_val, (unsigned)ctx_idx, "ctx_val");
        LLVMValueRef free_fn = LLVMBuildExtractValue(ctx->builder, allocator_val, (unsigned)free_idx, "free_fn_val");

        // Invoke Custom Free
        LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx->context);
        LLVMTypeRef free_fn_ty = LLVMFunctionType(void_ty, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
        LLVMValueRef call_args[] = { ctx_val, LLVMBuildBitCast(ctx->builder, ptr_val, i8ptr, "ptr_to_free") };
        
        return LLVMBuildCall2(ctx->builder, free_fn_ty, free_fn, call_args, 2, "");
    }

    return NULL;
}

// =============================================================================
// SECTION 2: VARIABLES & MEMBERS (L-VALUE LOADS)
// =============================================================================

LLVMValueRef codegen_expr_ident(CodegenContext *ctx, AstNode *expr) {
    Symbol *sym = expr->data.identifier.symbol;
    if (!sym) ICE("Identifier '%s' missing symbol in Codegen.", ((Slice*)expr->data.identifier.intern_result->key)->ptr);

    LLVMValueRef val = codegen_lvalue(ctx, expr);

    // If it's a variable, we must load the value from its memory address,
    // UNLESS it's a fixed-size array. Arrays naturally decay to pointers.
    if (sym->kind == SYMBOL_VARIABLE) {
        if (expr->type->kind == TYPE_ARRAY) {
             return val; 
        }
        return codegen_load_value(ctx, val, expr->type);
    }
    
    return val;
}

static LLVMValueRef codegen_expr_member(CodegenContext *ctx, AstNode *expr) {
    AstMemberExpr *member = &expr->data.member_expr;
    LLVMValueRef lval = codegen_lvalue(ctx, expr);

    // Module-level function access shouldn't be loaded (it's already a fn pointer)
    if (member->symbol && member->symbol->kind == SYMBOL_VALUE_FUNCTION) {
         return lval;
    }

    // Array fields (rare) decay to pointers
    if (expr->type->kind == TYPE_ARRAY) {
        return lval;
    }

    // Load standard member variables from their resolved pointer
    return codegen_load_value(ctx, lval, expr->type);
}

// =============================================================================
// SECTION 3: AGGREGATE CREATION
// =============================================================================

static LLVMValueRef codegen_expr_struct_literal(CodegenContext *ctx, AstNode *expr) {
    AstStructLiteral *lit = &expr->data.struct_literal;
    LLVMTypeRef struct_ty = get_llvm_type(ctx, expr->type);
    
    // Constant Folding Path (Global Initializers)
    if (expr->is_llvm_const_safe) {
        LLVMValueRef *fields = xmalloc(sizeof(LLVMValueRef) * lit->fields->count);
        for (size_t i = 0; i < lit->fields->count; i++) {
            AstFieldInit *init = (AstFieldInit*)dynarray_get(lit->fields, i);
            
            // DYNAMIC LAYOUT RESOLUTION (Fix for PS-1)
            size_t idx = get_struct_field_index(expr->type, init->name);
            if (idx == (size_t)-1) ICE_AT(expr, "Field index not found in codegen");
            
            fields[idx] = codegen_expr(ctx, init->expr);
        }
        LLVMValueRef val = LLVMConstNamedStruct(struct_ty, fields, (unsigned)lit->fields->count);
        free(fields);
        return val;
    }

    // Dynamic Runtime Path (Local Structs)
    LLVMValueRef val = LLVMGetUndef(struct_ty);

    for (size_t i = 0; i < lit->fields->count; i++) {
        AstFieldInit *init = (AstFieldInit*)dynarray_get(lit->fields, i);
        LLVMValueRef field_val = codegen_expr(ctx, init->expr);
        
        // DYNAMIC LAYOUT RESOLUTION (Fix for PS-1)
        size_t idx = get_struct_field_index(expr->type, init->name);
        if (idx == (size_t)-1) ICE_AT(expr, "Field index not found in codegen");
        
        val = LLVMBuildInsertValue(ctx->builder, val, field_val, (unsigned)idx, "struct_init");
    }

    return val;
}

static LLVMValueRef codegen_expr_initializer_list(CodegenContext *ctx, AstNode *expr) {
    AstInitializeList *list = &expr->data.initializer_list;
    Type *t = expr->type;
    if (t->kind != TYPE_ARRAY) ICE("Initializer list must have array type in Codegen");

    LLVMTypeRef arr_ty = get_llvm_type(ctx, t);
    LLVMTypeRef elem_ty = get_llvm_type(ctx, t->as.array.base);

    if (expr->is_llvm_const_safe) {
        LLVMValueRef *elems = xmalloc(sizeof(LLVMValueRef) * list->elements->count);
        for (size_t i = 0; i < list->elements->count; i++) {
            AstNode *elem = *(AstNode**)dynarray_get(list->elements, i);
            elems[i] = codegen_expr(ctx, elem);
        }
        LLVMValueRef val = LLVMConstArray(elem_ty, elems, (unsigned)list->elements->count);
        free(elems);
        return val;
    }

    LLVMValueRef arr_val = LLVMGetUndef(arr_ty);

    for (size_t i = 0; i < list->elements->count; i++) {
        AstNode *elem = *(AstNode**)dynarray_get(list->elements, i);
        LLVMValueRef elem_val = codegen_expr(ctx, elem);
        arr_val = LLVMBuildInsertValue(ctx->builder, arr_val, elem_val, (unsigned)i, "arr_init");
    }

    return arr_val;
}

static LLVMValueRef codegen_expr_subscript(CodegenContext *ctx, AstNode *expr) {
    LLVMValueRef ptr = codegen_lvalue(ctx, expr);
    return codegen_load_value(ctx, ptr, expr->type);
}

// =============================================================================
// SECTION 4: CONSTANTS & DISPATCH
// =============================================================================

static LLVMValueRef codegen_const_value(CodegenContext *ctx, Type *type, ConstValue *val) {
    if (val->type == STRING_LITERAL) {
         return LLVMBuildGlobalStringPtr(ctx->builder, ((Slice*)val->value.string_val->key)->ptr, "str_lit");
    }

    if (type_is_integer(type)) return LLVMConstInt(get_llvm_type(ctx, type), (unsigned long long)val->value.int_val, 0);
    if (type_is_float(type))   return LLVMConstReal(get_llvm_type(ctx, type), val->value.float_val);
    if (type_is_bool(type))    return LLVMConstInt(get_llvm_type(ctx, type), val->value.bool_val ? 1 : 0, 0);
    if (type_is_char(type))    return LLVMConstInt(get_llvm_type(ctx, type), (unsigned long long)val->value.char_val, 0);
    
    if (type->kind == TYPE_POINTER) {
        LLVMTypeRef ptr_ty = get_llvm_type(ctx, type);
        if (val->value.int_val == 0) return LLVMConstNull(ptr_ty);
        return LLVMConstIntToPtr(LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)val->value.int_val, 0), ptr_ty);
    }
    return NULL;
}

LLVMValueRef codegen_expr(CodegenContext *ctx, AstNode *expr) {
    if (!expr) return NULL;

    // Short-circuit for completely foldable expressions
    if (expr->is_foldable_const) {
        return codegen_const_value(ctx, expr->type, &expr->const_value);
    }

    switch (expr->node_type) {
        case AST_LITERAL: {
            if (expr->data.literal.type == STRING_LITERAL) {
                return LLVMBuildGlobalStringPtr(ctx->builder, ((Slice*)expr->data.literal.value.string_val->key)->ptr, "str_lit");
            }
            return codegen_const_value(ctx, expr->type, &expr->const_value);
        }
        case AST_IDENTIFIER:       return codegen_expr_ident(ctx, expr);
        case AST_CALL_EXPR:        return codegen_expr_call(ctx, expr);
        case AST_MEMBER_EXPR:      return codegen_expr_member(ctx, expr);
        case AST_STRUCT_LITERAL:   return codegen_expr_struct_literal(ctx, expr);
        case AST_INITIALIZER_LIST: return codegen_expr_initializer_list(ctx, expr);
        case AST_SUBSCRIPT_EXPR:   return codegen_expr_subscript(ctx, expr);
        case AST_INTRINSIC:        return codegen_expr_intrinsic(ctx, expr);
        
        case AST_BINARY_EXPR:
        case AST_UNARY_EXPR:
        case AST_ASSIGNMENT_EXPR:
        case AST_CAST:
            return codegen_expr_ops(ctx, expr);
            
        default:
            ICE("codegen_expr: unhandled node type %d", expr->node_type);
    }
    return NULL;
}