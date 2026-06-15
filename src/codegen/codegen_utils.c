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

char* mangle_name(CodegenContext *ctx, CompilationUnit *unit, InternResult *symbol_name) {
    if (!unit || !unit->logical_path) {
        // Main module: no mangling
        Slice *s = (Slice*)symbol_name->key;
        char *name = xmalloc(s->len + 1);
        memcpy(name, s->ptr, s->len);
        name[s->len] = '\0';
        return name;
    }

    Slice *s = (Slice*)symbol_name->key;
    // __mod_<logical_path>_<symbol_name>
    size_t len = strlen(unit->logical_path) + s->len + 10;
    char *mangled = xmalloc(len);

    // Replace dots with underscores in logical path
    char *log_path_fixed = xstrdup(unit->logical_path);
    for (char *p = log_path_fixed; *p; p++) if (*p == '.') *p = '_';

    snprintf(mangled, len, "__mod_%s_%.*s", log_path_fixed, (int)s->len, s->ptr);
    free(log_path_fixed);

    return mangled;
}

size_t struct_field_index(Type *struct_type, const char *field_name) {
    
    for (size_t i = 0; i < struct_type->as.struct_type.field_count; i++) {
        Slice *name = (Slice*)struct_type->as.struct_type.fields[i].name->key;
        if (name->len == strlen(field_name) && memcmp(name->ptr, field_name, name->len) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

size_t get_struct_field_index(Type *struct_type, InternResult *field_name) {
    for (size_t i = 0; i < struct_type->as.struct_type.field_count; i++) {
        if (struct_type->as.struct_type.fields[i].name == field_name) {
            return i;
        }
    }
    return (size_t)-1;
}
