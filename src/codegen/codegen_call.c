/**
 * @file codegen_call.c
 * @brief Generates LLVM IR for function calls and compiler intrinsics.
 * * Handles standard function calls including System V ABI compliance 
 * (sret for large returns, byval for large pass-by-value arguments), 
 * as well as the recursive, type-aware code generation for the built-in 
 * `print` and `println` intrinsics.
 */

#include "codegen_internal.h"
#include "codegen/codegen_utils.h"
#include <stdio.h>

// Forward declaration for recursive printing (e.g., arrays of structs, nested structs)
static void codegen_intrinsic_print_value(CodegenContext *ctx, LLVMValueRef val, Type *t);

// =============================================================================
// SECTION: INTRINSIC PRINTING HELPERS
// =============================================================================

/**
 * Helper to emit a call to the runtime's `print_str` function with a constant C-string.
 */
static void codegen_intrinsic_print_str_lit(CodegenContext *ctx, const char *s) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), &i8ptr, 1, 0);
    
    // Look up or declare the external print_str runtime function
    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "print_str");
    if (!fn) fn = LLVMAddFunction(ctx->module, "print_str", fn_ty);
    
    // Create a global string constant and pass its pointer
    LLVMValueRef str = LLVMBuildGlobalStringPtr(ctx->builder, s, "print_tmp");
    LLVMBuildCall2(ctx->builder, fn_ty, fn, &str, 1, "");
}

/**
 * Recursively generates LLVM IR to print an arbitrary compiler value based on its Type.
 * This dynamically dispatches to runtime functions (e.g., print_i32, print_bool) 
 * or generates structural printing loops for aggregates.
 */
static void codegen_intrinsic_print_value(CodegenContext *ctx, LLVMValueRef val, Type *t) {
    if (!t) return;

    // -------------------------------------------------------------------------
    // 1. PRIMITIVES
    // -------------------------------------------------------------------------
    if (t->kind == TYPE_PRIMITIVE) {
        const char *fn_name = NULL;
        LLVMTypeRef param_ty = NULL;

        // Map primitive types to their corresponding C runtime printing functions
        switch (t->as.primitive) {
            case PRIM_I32:  fn_name = "print_i32";  param_ty = LLVMInt32TypeInContext(ctx->context); break;
            case PRIM_I64:  fn_name = "print_i64";  param_ty = LLVMInt64TypeInContext(ctx->context); break;
            case PRIM_F32:  fn_name = "print_f32";  param_ty = LLVMFloatTypeInContext(ctx->context); break;
            case PRIM_F64:  fn_name = "print_f64";  param_ty = LLVMDoubleTypeInContext(ctx->context); break;
            case PRIM_BOOL: fn_name = "print_bool"; param_ty = LLVMInt32TypeInContext(ctx->context); break;
            case PRIM_CHAR: fn_name = "print_char"; param_ty = LLVMInt8TypeInContext(ctx->context);  break;
            default: break;
        }

        if (fn_name) {
            LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), &param_ty, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, fn_name);
            if (!fn) fn = LLVMAddFunction(ctx->module, fn_name, fn_ty);
            
            LLVMValueRef arg = val;
            // LLVM i1 (bool) must be zero-extended to i32 for the C runtime boundary
            if (t->as.primitive == PRIM_BOOL) {
                arg = LLVMBuildZExt(ctx->builder, val, param_ty, "bool_to_i32");
            }
            LLVMBuildCall2(ctx->builder, fn_ty, fn, &arg, 1, "");
        }
    } 
    // -------------------------------------------------------------------------
    // 2. POINTERS (With Null Checking)
    // -------------------------------------------------------------------------
    else if (t->kind == TYPE_POINTER) {
        // Special case: str type (canonical pointer to char)
        if (t == ctx->store->t_str || (t->as.ptr.base->kind == TYPE_PRIMITIVE && t->as.ptr.base->as.primitive == PRIM_CHAR)) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
            LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), &i8ptr, 1, 0);
            
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "print_str");
            if (!fn) fn = LLVMAddFunction(ctx->module, "print_str", fn_ty);
            
            LLVMValueRef arg = LLVMBuildBitCast(ctx->builder, val, i8ptr, "str_cast");
            LLVMBuildCall2(ctx->builder, fn_ty, fn, &arg, 1, "");
            return;
        }

        LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
        
        // Control Flow Graph: [Check] -> [Null Block] OR [Ptr Block] -> [End Block]
        LLVMBasicBlockRef null_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_null");
        LLVMBasicBlockRef ptr_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_ptr_val");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_ptr_end");

        // Conditional branch
        LLVMValueRef is_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, val, LLVMConstNull(LLVMTypeOf(val)), "is_null");
        LLVMBuildCondBr(ctx->builder, is_null, null_bb, ptr_bb);

        // -- Block: Null pointer --
        LLVMPositionBuilderAtEnd(ctx->builder, null_bb);
        codegen_intrinsic_print_str_lit(ctx, "null");
        LLVMBuildBr(ctx->builder, end_bb);

        // -- Block: Valid pointer --
        LLVMPositionBuilderAtEnd(ctx->builder, ptr_bb);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
        LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), &i8ptr, 1, 0);
        
        LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "print_ptr");
        if (!fn) fn = LLVMAddFunction(ctx->module, "print_ptr", fn_ty);
        
        LLVMValueRef arg = LLVMBuildBitCast(ctx->builder, val, i8ptr, "ptr_cast");
        LLVMBuildCall2(ctx->builder, fn_ty, fn, &arg, 1, "");
        LLVMBuildBr(ctx->builder, end_bb);

        // -- Block: End --
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    }
    // -------------------------------------------------------------------------
    // 3. STRUCTS
    // -------------------------------------------------------------------------
    else if (t->kind == TYPE_STRUCT) {
        codegen_intrinsic_print_str_lit(ctx, "{ ");
        for (size_t i = 0; i < t->as.struct_type.field_count; i++) {
            if (i > 0) codegen_intrinsic_print_str_lit(ctx, ", ");
            
            // Print field name: e.g., "x: "
            Slice *fname = (Slice*)t->as.struct_type.fields[i].name->key;
            char field_prefix[256];
            snprintf(field_prefix, sizeof(field_prefix), "%.*s: ", (int)fname->len, fname->ptr);
            codegen_intrinsic_print_str_lit(ctx, field_prefix);
            
            // Extract field value depending on whether 'val' is a pointer (lvalue) or loaded struct
            LLVMValueRef field_val;
            LLVMTypeRef struct_ty = get_llvm_type(ctx, t);
            if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
                field_val = LLVMBuildStructGEP2(ctx->builder, struct_ty, val, (unsigned)i, "field_ptr");
                field_val = codegen_load_value(ctx, field_val, t->as.struct_type.fields[i].type);
            } else {
                field_val = LLVMBuildExtractValue(ctx->builder, val, (unsigned)i, "field_val");
            }
            
            // Recurse
            codegen_intrinsic_print_value(ctx, field_val, t->as.struct_type.fields[i].type);
        }
        codegen_intrinsic_print_str_lit(ctx, " }");
    }
    // -------------------------------------------------------------------------
    // 4. ARRAYS AND SLICES
    // -------------------------------------------------------------------------
    else if (t->kind == TYPE_ARRAY || t->kind == TYPE_SLICE) {
        codegen_intrinsic_print_str_lit(ctx, "[");
        
        LLVMValueRef ptr = NULL;
        LLVMValueRef len = NULL;
        Type *base_type = (t->kind == TYPE_ARRAY) ? t->as.array.base : t->as.slice.base;
        LLVMTypeRef elem_ty = get_llvm_type(ctx, base_type);

        // Normalize both Arrays and Slices into a standard (ptr, len) pair
        if (t->kind == TYPE_ARRAY) {
            ptr = LLVMBuildBitCast(ctx->builder, val, LLVMPointerType(elem_ty, 0), "arr_ptr");
            len = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)t->as.array.size, 0);
        } else {
            LLVMValueRef slice_struct = val;
            if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
                slice_struct = LLVMBuildLoad2(ctx->builder, get_llvm_type(ctx, t), val, "slice_load");
            }
            ptr = LLVMBuildExtractValue(ctx->builder, slice_struct, 0, "slice_ptr");
            ptr = LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(elem_ty, 0), "slice_ptr_cast");
            len = LLVMBuildExtractValue(ctx->builder, slice_struct, 1, "slice_len");
        }

        LLVMValueRef current_func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_loop_cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_loop_body");
        LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_loop_end");

        // Allocate loop index (i = 0)
        LLVMValueRef index_ptr = create_entry_block_alloca(ctx, LLVMInt64TypeInContext(ctx->context), "print_idx");
        LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, 0), index_ptr);
        LLVMBuildBr(ctx->builder, cond_bb);

        // -- Block: Loop Condition (i < len) --
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef curr_idx = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), index_ptr, "curr_idx");
        LLVMValueRef cond = LLVMBuildICmp(ctx->builder, LLVMIntULT, curr_idx, len, "loop_cond");
        LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

        // -- Block: Loop Body --
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        LLVMBasicBlockRef comma_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_comma");
        LLVMBasicBlockRef no_comma_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print_no_comma");
        
        // Print comma only if i > 0
        LLVMValueRef is_not_first = LLVMBuildICmp(ctx->builder, LLVMIntUGT, curr_idx, LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, 0), "is_not_first");
        LLVMBuildCondBr(ctx->builder, is_not_first, comma_bb, no_comma_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, comma_bb);
        codegen_intrinsic_print_str_lit(ctx, ", ");
        LLVMBuildBr(ctx->builder, no_comma_bb);

        // Print actual element
        LLVMPositionBuilderAtEnd(ctx->builder, no_comma_bb);
        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, elem_ty, ptr, &curr_idx, 1, "elem_ptr");
        LLVMValueRef elem_val = codegen_load_value(ctx, elem_ptr, base_type);
        codegen_intrinsic_print_value(ctx, elem_val, base_type);

        // Increment i
        LLVMValueRef next_idx = LLVMBuildAdd(ctx->builder, curr_idx, LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, 0), "next_idx");
        LLVMBuildStore(ctx->builder, next_idx, index_ptr);
        LLVMBuildBr(ctx->builder, cond_bb);

        // -- Block: End --
        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        codegen_intrinsic_print_str_lit(ctx, "]");
    }
}

// =============================================================================
// SECTION: CORE CALL EXPRESSION GENERATION
// =============================================================================

LLVMValueRef codegen_expr_call(CodegenContext *ctx, AstNode *expr) {
    // Sanity check: Ensure the node passed semantic analysis
    if (!expr->type) {
        AstCallExpr *c = &expr->data.call_expr;
        const char *callee_name = "<complex>";
        if (c->callee->node_type == AST_IDENTIFIER) {
            callee_name = ((Slice*)c->callee->data.identifier.intern_result->key)->ptr;
        }
        ICE_AT(expr, "Call to '%s' missing type. (Sema failure?)", callee_name);
    }
    
    AstCallExpr *call = &expr->data.call_expr;

    // -------------------------------------------------------------------------
    // 1. COMPILER INTRINSICS (Fast Path)
    // -------------------------------------------------------------------------
    if (call->callee->node_type == AST_IDENTIFIER) {
        Symbol *sym = call->callee->data.identifier.symbol;
        if (sym && sym->kind == SYMBOL_VALUE_INTRINSIC) {
            // Intercept print() and println()
            if (sym->intrinsic_kind == INTRINSIC_PRINT || sym->intrinsic_kind == INTRINSIC_PRINT_NEWLINE) {
                bool newline = (sym->intrinsic_kind == INTRINSIC_PRINT_NEWLINE);

                size_t arg_count = call->args ? call->args->count : 0;
                for (size_t i = 0; i < arg_count; i++) {
                    AstNode *arg = *(AstNode**)dynarray_get(call->args, i);
                    LLVMValueRef val = codegen_expr(ctx, arg);
                    codegen_intrinsic_print_value(ctx, val, arg->type);
                }

                if (newline) {
                    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);
                    LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, "print_newline");
                    if (!fn) fn = LLVMAddFunction(ctx->module, "print_newline", fn_ty);
                    LLVMBuildCall2(ctx->builder, fn_ty, fn, NULL, 0, "");
                }

                return NULL; // Print intrinsics return void
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. STANDARD FUNCTION CALLS (ABI Compliance)
    // -------------------------------------------------------------------------
    LLVMValueRef callee = codegen_expr(ctx, call->callee);
    Type *fn_type = call->callee->type;
    
    // Unwrap pointer-to-function if necessary
    if (fn_type->kind == TYPE_POINTER) fn_type = fn_type->as.ptr.base;
    if (!fn_type || fn_type->kind != TYPE_FUNCTION) ICE("Callee must be a function type");

    // ABI: If returning a large struct (>16 bytes), System V requires an 'sret' (Struct Return).
    // The caller allocates the memory and passes a hidden pointer as the 1st parameter.
    bool sret = type_is_indirect(ctx, fn_type->as.func.return_type);
    size_t param_count = fn_type->as.func.param_count;
    size_t llvm_arg_count = param_count + (sret ? 1 : 0);
    
    LLVMValueRef *args = NULL;
    if (llvm_arg_count > 0) args = xmalloc(sizeof(LLVMValueRef) * llvm_arg_count);

    size_t idx = 0;
    LLVMValueRef sret_alloca = NULL;
    
    // Setup hidden sret pointer
    if (sret) {
        LLVMTypeRef ret_ty = get_llvm_type(ctx, fn_type->as.func.return_type);
        sret_alloca = create_entry_block_alloca(ctx, ret_ty, "sret_tmp");
        args[idx++] = sret_alloca;
    }

    // Process explicit arguments
    for (size_t i = 0; i < param_count; i++) {
        AstNode *arg_node = *(AstNode**)dynarray_get(call->args, i);
        Type *param_ty = fn_type->as.func.params[i];
        
        // ABI: If a struct argument is large, it must be passed via a pointer with 'byval'
        if (type_is_indirect(ctx, param_ty)) {
            args[idx++] = codegen_lvalue(ctx, arg_node);
        } else {
            args[idx++] = codegen_expr(ctx, arg_node);
        }
    }

    // Emit the call instruction
    LLVMTypeRef fn_ty = get_llvm_function_type(ctx, fn_type);
    LLVMValueRef call_instr = LLVMBuildCall2(
        ctx->builder, 
        fn_ty, 
        callee, 
        args, 
        (unsigned)llvm_arg_count, 
        (type_is_void(expr->type) || sret) ? "" : "calltmp"
    );
    
    // -------------------------------------------------------------------------
    // 3. ATTACH ABI ATTRIBUTES
    // -------------------------------------------------------------------------
    if (sret) {
        LLVMTypeRef ret_ty = get_llvm_type(ctx, fn_type->as.func.return_type);
        // FIXED: LLAddCallSiteAttribute -> LLVMAddCallSiteAttribute
        LLVMAddCallSiteAttribute(call_instr, 1, LLVMCreateTypeAttribute(ctx->context, 
            LLVMGetEnumAttributeKindForName("sret", 4), ret_ty));
    }
    
    size_t attr_idx = sret ? 2 : 1; // Attributes shift by 1 if there's a hidden sret arg
    for (size_t i = 0; i < param_count; i++) {
        if (type_is_indirect(ctx, fn_type->as.func.params[i])) {
            LLVMTypeRef param_ty = get_llvm_type(ctx, fn_type->as.func.params[i]);
            LLVMAddCallSiteAttribute(call_instr, (unsigned)(attr_idx + i), LLVMCreateTypeAttribute(ctx->context, 
                LLVMGetEnumAttributeKindForName("byval", 5), param_ty));
        }
    }

    if (args) free(args);

    // If we used an sret, the true return value is sitting inside our local alloca
    if (sret) {
        return codegen_load_value(ctx, sret_alloca, fn_type->as.func.return_type);
    }

    return call_instr;
}