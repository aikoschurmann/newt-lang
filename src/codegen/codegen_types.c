#include "codegen_internal.h"

LLVMTypeRef get_llvm_type(CodegenContext *ctx, Type *t) {
    if (!t) return LLVMVoidTypeInContext(ctx->context);
    switch (t->kind) {
        case TYPE_VOID: return LLVMVoidTypeInContext(ctx->context);
        case TYPE_PRIMITIVE:
            switch (t->as.primitive) {
                case PRIM_I32:  return LLVMInt32TypeInContext(ctx->context);
                case PRIM_I64:  return LLVMInt64TypeInContext(ctx->context);
                case PRIM_F32:  return LLVMFloatTypeInContext(ctx->context);
                case PRIM_F64:  return LLVMDoubleTypeInContext(ctx->context);
                case PRIM_BOOL: return LLVMInt1TypeInContext(ctx->context);
                case PRIM_CHAR: return LLVMInt8TypeInContext(ctx->context);
                case PRIM_VOID: return LLVMVoidTypeInContext(ctx->context);
                case PRIM_STR:  return LLVMPointerTypeInContext(ctx->context, 0);
                default:        return LLVMInt32TypeInContext(ctx->context);
            }
        case TYPE_POINTER:
            return LLVMPointerTypeInContext(ctx->context, 0);
        case TYPE_ARRAY:
            if (t->as.array.size_known) {
                return LLVMArrayType(get_llvm_type(ctx, t->as.array.base), t->as.array.size);
            } else {
                // FAT POINTER (Slice): struct { T* ptr, i64 len }
                LLVMTypeRef elements[] = {
                    LLVMPointerTypeInContext(ctx->context, 0),
                    LLVMInt64TypeInContext(ctx->context)
                };
                return LLVMStructTypeInContext(ctx->context, elements, 2, 0);
            }
        case TYPE_FUNCTION: {
            LLVMTypeRef ret_type = get_llvm_type(ctx, t->as.func.return_type);
            size_t param_count = t->as.func.param_count;
            LLVMTypeRef *param_types = NULL;
            if (param_count > 0) {
                param_types = malloc(sizeof(LLVMTypeRef) * param_count);
                for (size_t i = 0; i < param_count; i++) {
                    param_types[i] = get_llvm_type(ctx, t->as.func.params[i]);
                }
            }
            LLVMTypeRef fn_ty = LLVMFunctionType(ret_type, param_types, param_count, 0);
            if (param_types) free(param_types);
            return fn_ty;
        }
        default:
            return LLVMInt32TypeInContext(ctx->context);
    }
}
