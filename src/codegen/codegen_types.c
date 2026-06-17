#include "codegen_internal.h"

LLVMTypeRef get_llvm_function_type(CodegenContext *ctx, Type *t) {
    if (!t || t->kind != TYPE_FUNCTION) return LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, 0);

    bool sret = type_is_indirect(ctx, t->as.func.return_type);
    LLVMTypeRef ret_type = sret ? LLVMVoidTypeInContext(ctx->context) : get_llvm_type(ctx, t->as.func.return_type);
    
    size_t param_count = t->as.func.param_count;
    size_t llvm_arg_count = param_count + (sret ? 1 : 0);
    LLVMTypeRef *llvm_params = xmalloc(sizeof(LLVMTypeRef) * llvm_arg_count);

    size_t idx = 0;
    if (sret) {
        llvm_params[idx++] = LLVMPointerType(get_llvm_type(ctx, t->as.func.return_type), 0);
    }

    for (size_t i = 0; i < param_count; i++) {
        Type *param_ty = t->as.func.params[i];
        if (type_is_indirect(ctx, param_ty)) {
            llvm_params[idx++] = LLVMPointerType(get_llvm_type(ctx, param_ty), 0);
        } else {
            llvm_params[idx++] = get_llvm_type(ctx, param_ty);
        }
    }

    LLVMTypeRef fn_ty = LLVMFunctionType(ret_type, llvm_params, (unsigned)llvm_arg_count, 0);
    free(llvm_params);
    return fn_ty;
}

bool type_is_address_only(Type *t) {
    if (!t) ICE("type_is_address_only: NULL type reached codegen");
    switch (t->kind) {
        case TYPE_FUNCTION: return true;                         // functions decay to pointer
        case TYPE_ARRAY:    return true;                         // [N]T lives at its address
        case TYPE_SLICE:    return false;                        // T[] (slice) is loaded as a value
        case TYPE_VOID:     return true;                         // nothing to load
        default:            return false;                        // primitives, pointers, structs -> load by value
    }
}

bool type_is_indirect(CodegenContext *ctx, Type *t) {
    if (t->kind != TYPE_STRUCT) return false;
    LLVMTypeRef llvm_ty = get_llvm_type(ctx, t);
    // Threshold: 16 bytes (2 x 64-bit registers)
    return LLVMABISizeOfType(ctx->target_data, llvm_ty) > 16;
}

LLVMValueRef codegen_load_value(CodegenContext *ctx, LLVMValueRef ptr, Type *type) {
    if (!ptr) ICE("codegen_load_value: NULL lvalue for type kind %d", type ? type->kind : -1);
    if (type_is_address_only(type)) return ptr;
    return LLVMBuildLoad2(ctx->builder, get_llvm_type(ctx, type), ptr, "loadtmp");
}

LLVMTypeRef get_llvm_type(CodegenContext *ctx, Type *t) {
    if (!t) ICE("get_llvm_type: received NULL type.");
    
    // Check cache for ALL types
    LLVMTypeRef cached = hashmap_get(ctx->type_cache, t, ptr_hash, ptr_cmp);
    if (cached) return cached;

    LLVMTypeRef res = NULL;

    switch (t->kind) {
        case TYPE_VOID: res = LLVMVoidTypeInContext(ctx->context); break;
        case TYPE_PRIMITIVE:
            switch (t->as.primitive) {
                case PRIM_I8:
                case PRIM_U8:   res = LLVMInt8TypeInContext(ctx->context); break;
                case PRIM_I16:
                case PRIM_U16:  res = LLVMInt16TypeInContext(ctx->context); break;
                case PRIM_I32:
                case PRIM_U32:  res = LLVMInt32TypeInContext(ctx->context); break;
                case PRIM_I64:
                case PRIM_U64:  res = LLVMInt64TypeInContext(ctx->context); break;
                case PRIM_F32:  res = LLVMFloatTypeInContext(ctx->context); break;
                case PRIM_F64:  res = LLVMDoubleTypeInContext(ctx->context); break;
                case PRIM_BOOL: res = LLVMInt8TypeInContext(ctx->context); break;
                case PRIM_CHAR: res = LLVMInt8TypeInContext(ctx->context); break;
                default:        ICE("get_llvm_type: unrecognized primitive kind %d", t->as.primitive);
            }
            break;
        case TYPE_POINTER:
        case TYPE_FUNCTION: // In LLVM 15+ functions are opaque pointers when used as values/struct fields.
            res = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
            break;
        case TYPE_ARRAY:
            res = LLVMArrayType(get_llvm_type(ctx, t->as.array.base), (unsigned int)t->as.array.size);
            break;
        case TYPE_SLICE:
            {
                // FAT POINTER (Slice): struct { T* ptr, i64 len }
                LLVMTypeRef elements[] = {
                    LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0),
                    LLVMInt64TypeInContext(ctx->context)
                };
                res = LLVMStructTypeInContext(ctx->context, elements, 2, 0);
            }
            break;
        case TYPE_STRUCT: {
            const char *struct_name = "anon_struct";
            if (t->as.struct_type.name && t->as.struct_type.name->key) {
                struct_name = ((Slice*)t->as.struct_type.name->key)->ptr;
            }
            
            // Create named struct (opaque first)
            LLVMTypeRef struct_ty = LLVMStructCreateNamed(ctx->context, struct_name);
            
            // Cache before resolving body to handle recursive types
            hashmap_put(ctx->type_cache, (void*)t, struct_ty, ptr_hash, ptr_cmp);

            // Now resolve the body
            size_t field_count = t->as.struct_type.field_count;
            if (field_count > 0) {
                LLVMTypeRef *field_types = xmalloc(sizeof(LLVMTypeRef) * field_count);
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

    if (res) {
        hashmap_put(ctx->type_cache, (void*)t, res, ptr_hash, ptr_cmp);
    }
    return res;
}
