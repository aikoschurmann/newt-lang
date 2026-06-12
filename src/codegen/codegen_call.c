#include "codegen_internal.h"
#include "sema/intrinsics.h"
#include "datastructures/scope.h"

static LLVMValueRef codegen_intrinsic_print(CodegenContext *ctx, AstNode *arg, bool newline) {
    if (!arg || !arg->type) return NULL;

    const char *func_name = NULL;
    Type *t = arg->type;
    LLVMTypeRef param_t = NULL;

    if (t->kind == TYPE_PRIMITIVE) {
        switch (t->as.primitive) {
            case PRIM_I32:  
                func_name = "print_i32"; 
                param_t = LLVMInt32TypeInContext(ctx->context);
                break;
            case PRIM_I64:  
                func_name = "print_i64"; 
                param_t = LLVMInt64TypeInContext(ctx->context);
                break;
            case PRIM_F32:  
                func_name = "print_f32"; 
                param_t = LLVMFloatTypeInContext(ctx->context);
                break;
            case PRIM_F64:  
                func_name = "print_f64"; 
                param_t = LLVMDoubleTypeInContext(ctx->context);
                break;
            case PRIM_BOOL: 
                func_name = "print_bool"; 
                param_t = LLVMInt8TypeInContext(ctx->context);
                break;
            case PRIM_CHAR: 
                func_name = "print_char"; 
                param_t = LLVMInt8TypeInContext(ctx->context);
                break;
            case PRIM_STR:  
                func_name = "print_str"; 
                param_t = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                break;
            default: break;
        }
    }

    if (!func_name) return NULL;

    LLVMValueRef func = LLVMGetNamedFunction(ctx->module, func_name);
    if (!func) {
        LLVMTypeRef ret_ty = LLVMVoidTypeInContext(ctx->context);
        LLVMTypeRef fn_ty = LLVMFunctionType(ret_ty, &param_t, 1, 0);
        func = LLVMAddFunction(ctx->module, func_name, fn_ty);
    }

    LLVMValueRef val = codegen_expr(ctx, arg);
    
    // Ensure the value matches the parameter type
    if (LLVMTypeOf(val) != param_t) {
        if (t->as.primitive == PRIM_BOOL || t->as.primitive == PRIM_CHAR) {
            // Already handled by bit-width consistency in codegen_types.c
        }
    }

    LLVMValueRef call = LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(func), func, &val, 1, "");

    if (newline) {
        const char *nl_name = "print_newline";
        LLVMValueRef nl_func = LLVMGetNamedFunction(ctx->module, nl_name);
        if (!nl_func) {
            LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);
            nl_func = LLVMAddFunction(ctx->module, nl_name, fn_ty);
        }
        LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(nl_func), nl_func, NULL, 0, "");
    }

    return call;
}

LLVMValueRef codegen_expr_call(CodegenContext *ctx, AstNode *expr) {
    AstCallExpr *call = &expr->data.call_expr;

    // Check for intrinsic
    if (call->callee->node_type == AST_IDENTIFIER) {
        Slice *s = (Slice*)call->callee->data.identifier.intern_result->key;
        if (s->len == 5 && memcmp(s->ptr, "print", 5) == 0) {
            size_t arg_count = call->args ? call->args->count : 0;
            for (size_t i = 0; i < arg_count; i++) {
                codegen_intrinsic_print(ctx, *(AstNode**)dynarray_get(call->args, i), false);
            }
            return NULL;
        }
        if (s->len == 7 && memcmp(s->ptr, "println", 7) == 0) {
            size_t arg_count = call->args ? call->args->count : 0;
            if (arg_count == 0) {
                // Just newline
                const char *nl_name = "print_newline";
                LLVMValueRef nl_func = LLVMGetNamedFunction(ctx->module, nl_name);
                if (!nl_func) {
                    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);
                    nl_func = LLVMAddFunction(ctx->module, nl_name, fn_ty);
                }
                LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(nl_func), nl_func, NULL, 0, "");
            } else {
                for (size_t i = 0; i < arg_count; i++) {
                    codegen_intrinsic_print(ctx, *(AstNode**)dynarray_get(call->args, i), i == arg_count - 1);
                }
            }
            return NULL;
        }
    }

    LLVMValueRef callee = codegen_expr(ctx, call->callee);
    if (!callee) return NULL;

    size_t arg_count = call->args ? call->args->count : 0;
    LLVMValueRef *args = NULL;

    if (arg_count > 0) {
        args = malloc(sizeof(LLVMValueRef) * arg_count);

        LLVMTypeRef  func_type_pre  = get_llvm_function_type(ctx, call->callee->type);
        LLVMTypeRef *param_types    = malloc(sizeof(LLVMTypeRef) * arg_count);
        LLVMGetParamTypes(func_type_pre, param_types);

        for (size_t i = 0; i < arg_count; i++) {
            AstNode *arg_node = *(AstNode**)dynarray_get(call->args, i);
            args[i] = codegen_expr(ctx, arg_node);
            if (!args[i]) {
                LLVMTypeRef fallback = (param_types[i])
                                        ? param_types[i]
                                        : LLVMInt32TypeInContext(ctx->context);
                args[i] = LLVMConstNull(fallback);
            }
        }
        free(param_types);
    }

    LLVMTypeRef func_type = get_llvm_function_type(ctx, call->callee->type);
    LLVMTypeRef ret_ty    = LLVMGetReturnType(func_type);
    const char *call_name = (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) ? "" : "calltmp";

    LLVMValueRef res = LLVMBuildCall2(ctx->builder, func_type, callee, args, arg_count, call_name);
    if (args) free(args);
    return res;
}
