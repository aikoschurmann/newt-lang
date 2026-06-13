#include "codegen_internal.h"

LLVMTypeRef get_llvm_function_type(CodegenContext *ctx, Type *t) {
    if (!t || t->kind != TYPE_FUNCTION) return LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);

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

LLVMTypeRef get_llvm_type(CodegenContext *ctx, Type *t) {
    if (!t) ICE("get_llvm_type: received NULL type.");
    switch (t->kind) {
        case TYPE_VOID: return LLVMVoidTypeInContext(ctx->context);
        case TYPE_PRIMITIVE:
            switch (t->as.primitive) {
                case PRIM_I32:  return LLVMInt32TypeInContext(ctx->context);
                case PRIM_I64:  return LLVMInt64TypeInContext(ctx->context);
                case PRIM_F32:  return LLVMFloatTypeInContext(ctx->context);
                case PRIM_F64:  return LLVMDoubleTypeInContext(ctx->context);
                case PRIM_BOOL: return LLVMInt8TypeInContext(ctx->context);
                case PRIM_CHAR: return LLVMInt8TypeInContext(ctx->context);
                case PRIM_VOID: return LLVMVoidTypeInContext(ctx->context);
                case PRIM_STR:  return LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                default:        ICE("get_llvm_type: unrecognized primitive kind %d", t->as.primitive);
            }
        case TYPE_POINTER:
        case TYPE_FUNCTION: // In LLVM 15+ functions are opaque pointers when used as values/struct fields.
            return LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
        case TYPE_ARRAY:
            if (t->as.array.size_known) {
                return LLVMArrayType(get_llvm_type(ctx, t->as.array.base), (unsigned int)t->as.array.size);
            } else {
                // FAT POINTER (Slice): struct { T* ptr, i64 len }
                LLVMTypeRef elements[] = {
                    LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0),
                    LLVMInt64TypeInContext(ctx->context)
                };
                return LLVMStructTypeInContext(ctx->context, elements, 2, 0);
            }
        case TYPE_STRUCT: {
            const char *struct_name = "anon_struct";
            if (t->as.struct_type.name && t->as.struct_type.name->key) {
                struct_name = ((Slice*)t->as.struct_type.name->key)->ptr;
            }
            
            // Check if it already exists in the module
            LLVMTypeRef existing = LLVMGetTypeByName2(ctx->context, struct_name);
            if (existing) {
                return existing;
            }

            // Create named struct (opaque first)
            LLVMTypeRef struct_ty = LLVMStructCreateNamed(ctx->context, struct_name);

            // Now resolve the body
            size_t field_count = t->as.struct_type.field_count;
            if (field_count > 0) {
                LLVMTypeRef *field_types = malloc(sizeof(LLVMTypeRef) * field_count);
                for (size_t i = 0; i < field_count; i++) {
                    field_types[i] = get_llvm_type(ctx, t->as.struct_type.fields[i].type);
                }
                LLVMStructSetBody(struct_ty, field_types, (unsigned int)field_count, 0);
                free(field_types);
            } else {
                LLVMStructSetBody(struct_ty, NULL, 0, 0);
            }
            
            return struct_ty;
        }
        default:
            ICE("get_llvm_type: unrecognized type kind %d", t->kind);
    }
}
