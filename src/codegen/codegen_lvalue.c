/**
 * @file codegen_lvalue.c
 * @brief Generates LLVM IR for Left-Values (memory addresses).
 * * L-Values represent locations in memory (pointers) rather than the loaded values.
 * * This is used for the left side of assignments, passing by reference, taking addresses, 
 * * and generating GEPs (GetElementPtr) for structs and arrays.
 */

#include "codegen_internal.h"
#include "codegen/codegen_utils.h"

LLVMValueRef codegen_lvalue(CodegenContext *ctx, AstNode *expr) {
    if (!expr) return NULL;
    if (!expr->type) ICE_AT(expr, "LValue node missing type.");

    // =========================================================================
    // 1. IDENTIFIERS (Variables and Globals)
    // =========================================================================
    if (expr->node_type == AST_IDENTIFIER) {
        AstIdentifier *ident = &expr->data.identifier;
        
        // 1a. Try local variables first (Fast path, O(1) dense map lookup)
        if (ident->intern_result) {
            void *key = (void*)(intptr_t)ident->intern_result->entry->dense_index;
            LLVMValueRef val = codegen_map_get(ctx->locals, key);
            if (val) return val;
        }

        // 1b. Try global/mangled module symbols
        CompilationUnit *current_unit = module_loader_get_unit(ctx->loader, expr->filename);
        if (current_unit && current_unit->global_scope) {
            Symbol *sym = scope_lookup_symbol(current_unit->global_scope, ident->intern_result, expr->filename);
            if (sym) {
                // Resolve through any alias chains (e.g. import aliases)
                while (sym && sym->kind == SYMBOL_VALUE_ALIAS) {
                    sym = sym->target_symbol;
                }
                
                if (!sym) ICE_AT(expr, "Alias '%s' resolved to NULL.", ((Slice*)ident->intern_result->key)->ptr);

                CompilationUnit *origin_unit = module_loader_get_unit(ctx->loader, sym->filename);
                char *mangled = mangle_name(ctx, origin_unit, sym->name_rec);
                
                // Globals could be functions or variables
                LLVMValueRef val = LLVMGetNamedFunction(ctx->module, mangled);
                if (!val) val = LLVMGetNamedGlobal(ctx->module, mangled);
                
                free(mangled);
                if (val) return val;
            }
        }

        ICE_AT(expr, "Identifier '%s' could not be resolved to an lvalue.", ((Slice*)ident->intern_result->key)->ptr);
        return NULL;
    }

    // =========================================================================
    // 2. SUBSCRIPTS (Arrays, Slices, Pointers, Strings)
    // =========================================================================
    if (expr->node_type == AST_SUBSCRIPT_EXPR) {
        AstSubscriptExpr *sub = &expr->data.subscript_expr;
        Type *target_type = sub->target->type;
        if (!target_type) ICE_AT(sub->target, "Subscript target missing type.");
        
        LLVMValueRef idx = codegen_expr(ctx, sub->index);

        // -- Fixed Size Arrays --
        if (target_type->kind == TYPE_ARRAY) {
            // target is a pointer to the array [N x T]*
            LLVMValueRef target = codegen_lvalue(ctx, sub->target);
            LLVMTypeRef arr_ty = get_llvm_type(ctx, target_type);
            
            // GEP needs two indices: 0 to dereference the array pointer, then 'idx' for the element
            LLVMValueRef indices[] = {
                LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                idx
            };
            return LLVMBuildInBoundsGEP2(ctx->builder, arr_ty, target, indices, 2, "arrayidx");
        } 
        // -- Slices (Fat Pointers) --
        else if (target_type->kind == TYPE_SLICE) {
            // target is a pointer to the struct {T*, i64}*
            LLVMValueRef struct_ptr = codegen_lvalue(ctx, sub->target);
            if (!struct_ptr) ICE_AT(sub->target, "Subscript target failed to resolve.");
            LLVMTypeRef struct_ty = get_llvm_type(ctx, target_type);
            
            // Extract the actual memory pointer 'ptr' (Index 0) from the slice struct
            LLVMValueRef data_ptr_ptr = LLVMBuildStructGEP2(ctx->builder, struct_ty, struct_ptr, 0, "data_ptr_ptr");
            LLVMTypeRef elem_ptr_ty = LLVMStructGetTypeAtIndex(struct_ty, 0);
            LLVMValueRef data_ptr = LLVMBuildLoad2(ctx->builder, elem_ptr_ty, data_ptr_ptr, "data_ptr");

            // GEP directly into the extracted pointer
            LLVMTypeRef elem_ty = get_llvm_type(ctx, target_type->as.slice.base);
            return LLVMBuildGEP2(ctx->builder, elem_ty, data_ptr, &idx, 1, "sliceidx");
        } 
        // -- Pointers --
        else if (target_type->kind == TYPE_POINTER) {
            LLVMValueRef target = codegen_expr(ctx, sub->target);
            LLVMTypeRef elem_ty = get_llvm_type(ctx, target_type->as.ptr.base);
            return LLVMBuildGEP2(ctx->builder, elem_ty, target, &idx, 1, "ptr_idx");
        }
        else {
            ICE_AT(sub->target, "Subscript target must be array, slice, or pointer");
        }
    }

    // =========================================================================
    // 3. MEMBER EXPRESSIONS (Struct Fields, Slice Len, Module Namespaces)
    // =========================================================================
    if (expr->node_type == AST_MEMBER_EXPR) {
        AstMemberExpr *mem_expr = &expr->data.member_expr;
        
        // -- 3a. Module/Namespace Access --
        if (mem_expr->symbol) {
            CompilationUnit *u = module_loader_get_unit(ctx->loader, mem_expr->symbol->filename);
            if (!u) ICE("Failed to find CompilationUnit for symbol in '%s'", mem_expr->symbol->filename ? mem_expr->symbol->filename : "unknown");
            
            char *mangled = mangle_name(ctx, u, mem_expr->member);
            LLVMValueRef val = LLVMGetNamedFunction(ctx->module, mangled);
            if (!val) val = LLVMGetNamedGlobal(ctx->module, mangled);
            
            if (!val) ICE("Failed to find LLVM value for mangled symbol '%s'", mangled);
            
            free(mangled);
            return val;
        }

        // -- 3b. Struct/Slice Field Access --
        Type *target_type = mem_expr->target->type;
        if (!target_type) ICE_AT(mem_expr->target, "Member expression target missing type.");

        // Identify the underlying type (unwrapping pointers automatically)
        Type *underlying = target_type;
        if (underlying->kind == TYPE_POINTER) {
            underlying = underlying->as.ptr.base;
        }

        // Get the memory address of the struct/slice
        LLVMValueRef target_lvalue = codegen_lvalue(ctx, mem_expr->target);
        if (!target_lvalue) ICE_AT(mem_expr->target, "Member expression target failed to resolve.");

        // If the target expression was a pointer (e.g., `struct_ptr.field`), 
        // we must load the pointer to get the actual struct/slice address.
        if (target_type->kind == TYPE_POINTER) {
            LLVMTypeRef ptr_ty = get_llvm_type(ctx, target_type);
            target_lvalue = LLVMBuildLoad2(ctx->builder, ptr_ty, target_lvalue, "deref_ptr");
        }

        if (underlying->kind == TYPE_SLICE) {
            LLVMTypeRef struct_ty = get_llvm_type(ctx, underlying);
            // Slices have fixed physical layouts: { T* ptr (0), i64 len (1) }
            unsigned idx = (mem_expr->member == ctx->store->kw_len) ? 1 : 0;
            return LLVMBuildStructGEP2(ctx->builder, struct_ty, target_lvalue, idx, "slice_gep");
        } 
        else if (underlying->kind == TYPE_STRUCT) {
            LLVMTypeRef struct_ty = get_llvm_type(ctx, underlying);
            // Dynamic index lookup for safety against struct layout changes (PS-1 Fix)
            size_t idx = get_struct_field_index(underlying, mem_expr->member);
            if (idx == (size_t)-1) ICE_AT(expr, "Field index not found in codegen");
            return LLVMBuildStructGEP2(ctx->builder, struct_ty, target_lvalue, (unsigned)idx, "field_gep");
        }

        ICE_AT(expr, "Member access requires struct or slice");
    }

    // =========================================================================
    // 4. DEREF EXPRESSIONS (`*ptr`)
    // =========================================================================
    if (expr->node_type == AST_UNARY_EXPR && expr->data.unary_expr.op == OP_DEREF) {
        // The l-value of a dereference is simply the evaluated pointer value itself.
        return codegen_expr(ctx, expr->data.unary_expr.expr);
    }

    // =========================================================================
    // 5. INLINE ALLOCATIONS (Struct/Array Literals)
    // =========================================================================
    if (expr->node_type == AST_STRUCT_LITERAL || expr->node_type == AST_INITIALIZER_LIST) {
        // If a literal is used where an l-value is required (e.g., passing a struct literal by reference),
        // we allocate temporary memory for it, store the literal, and return the temporary pointer.
        LLVMTypeRef ty = get_llvm_type(ctx, expr->type);
        LLVMValueRef slot = LLVMBuildAlloca(ctx->builder, ty, "literal_mem");
        LLVMValueRef val = codegen_expr(ctx, expr);
        LLVMBuildStore(ctx->builder, val, slot);
        return slot;
    }

    ICE_AT(expr, "Unhandled lvalue node type %d", expr->node_type);
}