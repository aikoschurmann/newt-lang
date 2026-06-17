#include "codegen_internal.h"
#include "codegen_utils.h"

LLVMValueRef create_entry_block_alloca(CodegenContext *ctx, LLVMTypeRef ty, const char *name) {
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef current_func = LLVMGetBasicBlockParent(current_block);
    LLVMBasicBlockRef entry_block = LLVMGetEntryBasicBlock(current_func);

    // Create a temporary builder pointing to the top of the entry block
    LLVMBuilderRef tmp_builder = LLVMCreateBuilderInContext(ctx->context);

    LLVMValueRef first_instr = LLVMGetFirstInstruction(entry_block);
    if (first_instr) {
        LLVMPositionBuilderBefore(tmp_builder, first_instr);
    } else {
        LLVMPositionBuilderAtEnd(tmp_builder, entry_block);
    }

    LLVMValueRef alloca = LLVMBuildAlloca(tmp_builder, ty, name);
    LLVMDisposeBuilder(tmp_builder);

    return alloca;
}

static void mangle_type_recursive(char *buf, size_t *pos, size_t cap, Type *t) {
    if (!t) return;
    if (*pos >= cap - 4) return;

    switch (t->kind) {
        case TYPE_VOID:      buf[(*pos)++] = 'v'; break;
        case TYPE_PRIMITIVE: 
            switch (t->as.primitive) {
                case PRIM_I8:   buf[(*pos)++] = 'b'; break; // 'b' for byte
                case PRIM_I16:  buf[(*pos)++] = 's'; break; // 's' for short
                case PRIM_I32:  buf[(*pos)++] = 'i'; break;
                case PRIM_I64:  buf[(*pos)++] = 'I'; break;
                case PRIM_U8:   buf[(*pos)++] = 'B'; break; // 'B' for u-byte
                case PRIM_U16:  buf[(*pos)++] = 'S'; break; // 'S' for u-short
                case PRIM_U32:  buf[(*pos)++] = 'u'; break; // 'u' for u-int
                case PRIM_U64:  buf[(*pos)++] = 'U'; break; // 'U' for u-long
                case PRIM_F32:  buf[(*pos)++] = 'f'; break;
                case PRIM_F64:  buf[(*pos)++] = 'F'; break;
                case PRIM_BOOL: buf[(*pos)++] = 'b'; break;
                case PRIM_CHAR: buf[(*pos)++] = 'c'; break;
            }
            break;
        case TYPE_POINTER:
            buf[(*pos)++] = 'P';
            mangle_type_recursive(buf, pos, cap, t->as.ptr.base);
            break;
        case TYPE_ARRAY:
            buf[(*pos)++] = 'A';
            *pos += snprintf(buf + *pos, cap - *pos, "%lld", (long long)t->as.array.size);
            mangle_type_recursive(buf, pos, cap, t->as.array.base);
            break;
        case TYPE_SLICE:
            buf[(*pos)++] = 'S';
            mangle_type_recursive(buf, pos, cap, t->as.slice.base);
            break;
        case TYPE_STRUCT:
            buf[(*pos)++] = 'T';
            if (t->as.struct_type.name) {
                Slice *s = (Slice*)t->as.struct_type.name->key;
                *pos += snprintf(buf + *pos, cap - *pos, "%zu%.*s", (size_t)s->len, (int)s->len, s->ptr);
            } else {
                *pos += snprintf(buf + *pos, cap - *pos, "anon");
            }
            break;
        default: break;
    }
}

char* mangle_name(CodegenContext *ctx, CompilationUnit *unit, InternResult *symbol_name, Type *fn_type) {
    Slice *s = (Slice*)symbol_name->key;

    // Special cases: 'main' and functions with no unit (intrinsics/special) are never mangled
    if ((s->len == 4 && strncmp(s->ptr, "main", 4) == 0) || !unit || !unit->logical_path) {
        char *name = xmalloc(s->len + 1);
        memcpy(name, s->ptr, s->len);
        name[s->len] = '\0';
        return name;
    }

    // Build the mangled name: __mod_<module_path>_<symbol_name>.<signature>
    size_t cap = strlen(unit->logical_path) + s->len + 256;
    char *mangled = xmalloc(cap);

    // Replace dots with underscores in logical path
    char *log_path_fixed = xstrdup(unit->logical_path);
    for (char *p = log_path_fixed; *p; p++) if (*p == '.') *p = '_';

    size_t pos = snprintf(mangled, cap, "__mod_%s_%.*s", log_path_fixed, (int)s->len, s->ptr);
    free(log_path_fixed);

    // Overload Suffix: .<type_signature>
    if (fn_type && fn_type->kind == TYPE_FUNCTION) {
        mangled[pos++] = '.';
        for (size_t i = 0; i < fn_type->as.func.param_count; i++) {
            mangle_type_recursive(mangled, &pos, cap, fn_type->as.func.params[i]);
        }
        if (fn_type->as.func.param_count == 0) {
            mangled[pos++] = 'v'; // void/empty args
        }
    }

    mangled[pos] = '\0';
    return mangled;
}

bool struct_field_index(Type *struct_type, const char *field_name, size_t *out_index) {
    
    for (size_t i = 0; i < struct_type->as.struct_type.field_count; i++) {
        Slice *name = (Slice*)struct_type->as.struct_type.fields[i].name->key;
        if (name->len == strlen(field_name) && memcmp(name->ptr, field_name, name->len) == 0) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    return false;
}

bool get_struct_field_index(Type *struct_type, InternResult *field_name, size_t *out_index) {
    for (size_t i = 0; i < struct_type->as.struct_type.field_count; i++) {
        if (struct_type->as.struct_type.fields[i].name == field_name) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    return false;
}
