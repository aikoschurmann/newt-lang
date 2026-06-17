#include "sema/typecheck_expr.h"
#include "sema/type_coerce.h"
#include "sema/type_utils.h"
#include "sema/symbol_utils.h"
#include "sema/typecheck.h"
#include "sema/intrinsics.h"
#include "parsing/ast.h" 
#include "datastructures/scope.h"
#include "core/error.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// SECTION 1: CONSTANT FOLDING HELPERS
// =============================================================================

static void fold_unary_op(AstNode *node, OpKind op, AstNode *operand) {
    if (!operand->is_foldable_const) return;
    LiteralType type = operand->const_value.type;

    if (op == OP_NOT && type == BOOL_LITERAL) {
        node->is_foldable_const = 1;
        node->is_llvm_const_safe = 1;
        node->const_value.type = BOOL_LITERAL;
        node->const_value.value.bool_val = !operand->const_value.value.bool_val;
        return;
    }

    if (op == OP_SUB) {
        node->is_foldable_const = 1;
        node->is_llvm_const_safe = 1;
        node->const_value.type = type;
        if (type == INT_LITERAL) {
            node->const_value.value.int_val = -operand->const_value.value.int_val;
        } else if (type == FLOAT_LITERAL) {
            node->const_value.value.float_val = -operand->const_value.value.float_val;
        }
    }
}

static void fold_binary_op(AstNode *node, OpKind op, AstNode *l, AstNode *r) {
    if (!l->is_foldable_const || !r->is_foldable_const) return;

    LiteralType ltype = l->const_value.type;
    LiteralType rtype = r->const_value.type;
    
    if (ltype == FLOAT_LITERAL || rtype == FLOAT_LITERAL) {
        double v1 = (ltype == FLOAT_LITERAL) ? l->const_value.value.float_val : (double)l->const_value.value.int_val;
        double v2 = (rtype == FLOAT_LITERAL) ? r->const_value.value.float_val : (double)r->const_value.value.int_val;
        double res = 0.0;
        bool is_bool = false;

        switch (op) {
            case OP_ADD: res = v1 + v2; break;
            case OP_SUB: res = v1 - v2; break;
            case OP_MUL: res = v1 * v2; break;
            case OP_DIV: if(v2==0) return; res = v1 / v2; break;
            case OP_MOD: if(v2==0) return; res = fmod(v1, v2); break;
            case OP_EQ:  res = (v1 == v2); is_bool = true; break;
            case OP_NEQ: res = (v1 != v2); is_bool = true; break;
            case OP_LT:  res = (v1 < v2);  is_bool = true; break;
            case OP_GT:  res = (v1 > v2);  is_bool = true; break;
            case OP_LE:  res = (v1 <= v2); is_bool = true; break;
            case OP_GE:  res = (v1 >= v2); is_bool = true; break;
            default: return; 
        }

        node->is_foldable_const = 1;
        node->is_llvm_const_safe = 1;
        if (is_bool) {
            node->const_value.type = BOOL_LITERAL;
            node->const_value.value.bool_val = (bool)res;
        } else {
            node->const_value.type = FLOAT_LITERAL;
            node->const_value.value.float_val = res;
        }
    } else if (ltype == INT_LITERAL && rtype == INT_LITERAL) {
        int64_t v1 = l->const_value.value.int_val;
        int64_t v2 = r->const_value.value.int_val;
        int64_t res = 0;
        bool is_bool = false;

        switch (op) {
            case OP_ADD: res = v1 + v2; break;
            case OP_SUB: res = v1 - v2; break;
            case OP_MUL: res = v1 * v2; break;
            case OP_DIV: if(v2==0) return; res = v1 / v2; break;
            case OP_MOD: if(v2==0) return; res = v1 % v2; break;
            case OP_EQ:  res = (v1 == v2); is_bool = true; break;
            case OP_NEQ: res = (v1 != v2); is_bool = true; break;
            case OP_LT:  res = (v1 < v2);  is_bool = true; break;
            case OP_GT:  res = (v1 > v2);  is_bool = true; break;
            case OP_LE:  res = (v1 <= v2); is_bool = true; break;
            case OP_GE:  res = (v1 >= v2); is_bool = true; break;
            case OP_AND: res = (v1 && v2); is_bool = true; break;
            case OP_OR:  res = (v1 || v2); is_bool = true; break;
            default: return;
        }

        node->is_foldable_const = 1;
        node->is_llvm_const_safe = 1;
        if (is_bool) {
            node->const_value.type = BOOL_LITERAL;
            node->const_value.value.bool_val = (bool)res;
        } else {
            node->const_value.type = INT_LITERAL;
            node->const_value.value.int_val = res;
        }
    }
}

// =============================================================================
// SECTION 3: CORE EXPRESSION CHECKERS
// =============================================================================

/**
 * Resolves the concrete type of a literal kind based on context.
 * For example, an INT_LITERAL might resolve to i64 by default, or f64 if expected.
 */
static Type* resolve_literal_type(TypeCheckContext *ctx, LiteralType lit_kind, Type *expected) {
    TypeStore *s = ctx->store;
    switch (lit_kind) {
        case INT_LITERAL:
            // Numeric adaptation: Literals are polymorphic until they hit a concrete requirement.
            if (expected && type_is_float(expected)) return expected;
            if (expected && type_is_integer(expected)) return expected;
            return s->t_i64; // Default to i64
        case FLOAT_LITERAL:
            if (expected && type_is_float(expected)) return expected;
            return s->t_f64; // Default to f64
        case BOOL_LITERAL:   return s->t_bool;
        case CHAR_LITERAL:   return s->t_char;
        case STRING_LITERAL: return s->t_str;
        case NULL_LITERAL: {
            // Null adaptation: Null can be any pointer type
            if (expected && expected->kind == TYPE_POINTER) return expected;
            // Default to a generic void pointer or similar if possible, 
            // but for now we'll just return expected if it's a pointer, or 
            // maybe a generic pointer type if we have one.
            return s->t_void_ptr; // Assuming t_void_ptr exists or similar
        }
        default: ICE("Unknown LiteralType in resolve_literal_type: %d", lit_kind);
    }
}

/**
 * Validates a literal expression and performs literal-level constant folding.
 */
Type* check_literal(TypeCheckContext *ctx, AstNode *expr, Type *expected_type) {
    // =========================================================================
    // 1. TYPE RESOLUTION
    // =========================================================================
    Type *type = resolve_literal_type(ctx, expr->data.literal.type, expected_type);
    
    // Adapt integer literals to floats if that's what's expected (e.g., `float x = 5;`)
    if (expr->data.literal.type == INT_LITERAL && type && type_is_float(type)) {
        expr->data.literal.type = FLOAT_LITERAL;
        expr->data.literal.value.float_val = (double)expr->data.literal.value.int_val;
    }

    // =========================================================================
    // 2. CONSTANT EVALUATION
    // =========================================================================
    // Literals are always foldable and safe for LLVM global initializers.
    expr->is_foldable_const = 1;
    expr->is_llvm_const_safe = 1;
    expr->const_value.type  = expr->data.literal.type;
    expr->const_value.value = expr->data.literal.value;
    
    expr->type = type;
    return type;
}

/**
 * Returns the module-level scope (depth 1) for a given scope.
 * If the scope is already at depth 0 or 1, it returns it as-is.
 */
static Scope* get_module_scope(Scope *scope) {
    while (scope && scope->depth > 1) {
        scope = scope->parent;
    }
    return scope;
}

/**
 * Resolves an identifier to its symbol and determines its type and constancy.
 * 
 * This function performs three main tasks:
 * 1. Symbol Lookup: Searches the scope hierarchy for the identifier's name.
 * 2. Demand-driven Resolution: If the symbol is a global constant that hasn't
 *    been evaluated yet, it triggers its evaluation.
 * 3. Constancy Propagation: Updates the AST node with type and constant value
 *    information if the symbol is a known constant.
 * 
 * @param ctx   The type-checking context.
 * @param scope The current scope where the lookup starts.
 * @param expr  The AST node representing the identifier.
 * @return The resolved Type of the identifier, or NULL on failure.
 */
Type* check_identifier(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstIdentifier *ident = &expr->data.identifier;

    // -------------------------------------------------------------------------
    // 1. SYMBOL LOOKUP
    // -------------------------------------------------------------------------
    Symbol *sym = scope_lookup_symbol(scope, ident->intern_result, ctx->filename);

    if (!sym) {
        const char *name = (ident->intern_result && ident->intern_result->key) 
            ? ((Slice*)ident->intern_result->key)->ptr 
            : "<unknown>";

        TypeError err = { 
            .kind = TE_UNDECLARED, 
            .span = expr->span, 
            .filename = ctx->filename, 
            .as.name.name = name 
        };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    // Store resolved symbol for later stages (Codegen/Sema)
    ident->symbol = sym; 

    // -------------------------------------------------------------------------
    // 2. DEMAND-DRIVEN GLOBAL RESOLUTION
    // -------------------------------------------------------------------------
    // If we're accessing a global constant that hasn't been computed yet, 
    // we jump to its declaration to resolve it now. This enables 
    // out-of-order global access.
    bool is_unresolved_const = (sym->flags & SYMBOL_FLAG_CONST) && 
                              !(sym->flags & SYMBOL_FLAG_COMPUTED_VALUE);
    
    if (is_unresolved_const && sym->decl_node) {
        Scope *module_scope = get_module_scope(scope);
        if (module_scope) {
            check_variable_declaration(ctx, module_scope, sym->decl_node);
        }
    }

    // -------------------------------------------------------------------------
    // 3. CONSTANCY PROPAGATION
    // -------------------------------------------------------------------------
    expr->type = sym->type;
    expr->is_foldable_const = 0;
    expr->is_llvm_const_safe = 0;

    bool is_computed_const = (sym->flags & SYMBOL_FLAG_CONST) && 
                            (sym->flags & SYMBOL_FLAG_COMPUTED_VALUE);

    if (is_computed_const) {
        // Map primitives to their literal equivalents for constant folding.
        if (type_is_integer(sym->type)) {
            expr->is_foldable_const = 1;
            expr->const_value = (ConstValue){ .type = INT_LITERAL, .value.int_val = sym->value.int_val };
        } else if (type_is_float(sym->type)) {
            expr->is_foldable_const = 1;
            expr->const_value = (ConstValue){ .type = FLOAT_LITERAL, .value.float_val = sym->value.float_val };
        } else if (type_is_bool(sym->type)) {
            expr->is_foldable_const = 1;
            expr->const_value = (ConstValue){ .type = BOOL_LITERAL, .value.bool_val = sym->value.bool_val };
        } else if (type_is_char(sym->type)) {
            expr->is_foldable_const = 1;
            expr->const_value = (ConstValue){ .type = CHAR_LITERAL, .value.char_val = (char)sym->value.int_val };
        }

        // Foldable constants are always LLVM-safe. Aggregates (structs, arrays, slices) 
        // are only LLVM-safe since they don't fit in the simple union-based ConstValue.
        bool is_aggregate = sym->type->kind == TYPE_STRUCT || 
                           sym->type->kind == TYPE_ARRAY || 
                           sym->type->kind == TYPE_SLICE;
        
        expr->is_llvm_const_safe = expr->is_foldable_const || is_aggregate;
    }

    if (sym->kind == SYMBOL_VALUE_FUNCTION) {
        expr->is_llvm_const_safe = 1;
    }

    return sym->type;
}

static Type* underlying_type(Type *t) {
    while (t && t->kind == TYPE_POINTER) {
        t = t->as.ptr.base;
    }
    return t;
}

/**
 * Validates a function call expression, checking arguments against parameters.
 */
Type* check_call_expr(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstCallExpr *call = &expr->data.call_expr;
    
    // =========================================================================
    // 1. INTRINSIC DISPATCH
    // =========================================================================
    if (call->callee->node_type == AST_IDENTIFIER) {
        Symbol *sym = scope_lookup_symbol(scope, call->callee->data.identifier.intern_result, ctx->filename);
        if (sym && sym->kind == SYMBOL_VALUE_INTRINSIC) {
            expr->type = ctx->store->t_void; 
            
            size_t arg_count = call->args ? call->args->count : 0;
            for (size_t i = 0; i < arg_count; i++) {
                AstNode *arg = *(AstNode**)dynarray_get(call->args, i);
                check_expression(ctx, scope, arg, NULL);
            }

            call->callee->type = sym->type; 
            call->callee->data.identifier.symbol = sym;
            return expr->type;
        }
    }

    // =========================================================================
    // 2. CANDIDATE RESOLUTION
    // =========================================================================
    Symbol *callee_sym = NULL;
    Type   *callee_type = NULL;
    bool    is_instance_method = false;

    if (call->callee->node_type == AST_IDENTIFIER) {
        callee_sym = scope_lookup_symbol(scope, call->callee->data.identifier.intern_result, ctx->filename);
    } else if (call->callee->node_type == AST_MEMBER_EXPR) {
        AstMemberExpr *mem = &call->callee->data.member_expr;
        Type *target_type = check_expression(ctx, scope, mem->target, NULL);
        if (target_type) {
            Type *underlying = underlying_type(target_type);

            if (underlying->kind == TYPE_STRUCT) {
                callee_sym = (Symbol*)hashmap_get(underlying->as.struct_type.methods, mem->member->key, ptr_hash, ptr_cmp);
                
                if (callee_sym) {
                    // Instance method call detection:
                    bool target_is_type = false;
                    if (mem->target->node_type == AST_IDENTIFIER) {
                        Symbol *s = mem->target->data.identifier.symbol;
                        if (s && s->kind == SYMBOL_VALUE_TYPE) target_is_type = true;
                    }
                    is_instance_method = !target_is_type;
                }
            }
        }
    }

    if (!callee_sym) {
        // Fallback for non-overloadable expressions (like function pointers)
        callee_type = check_expression(ctx, scope, call->callee, NULL);
        if (call->callee->node_type == AST_IDENTIFIER) callee_sym = call->callee->data.identifier.symbol;
        else if (call->callee->node_type == AST_MEMBER_EXPR) callee_sym = call->callee->data.member_expr.symbol;
    }

    size_t arg_count = call->args ? call->args->count : 0;

    // OVERLOAD RESOLUTION LOOP
    if (callee_sym && callee_sym->kind == SYMBOL_OVERLOAD_SET) {
        size_t n_cands = callee_sym->overloads->count;

        // Pass 1: Type-check args
        Type **arg_types = alloca(sizeof(Type*) * (arg_count ? arg_count : 1));
        for (size_t i = 0; i < arg_count; i++) {
            AstNode *arg = *(AstNode**)dynarray_get(call->args, i);
            arg_types[i] = check_expression(ctx, scope, arg, NULL);
            if (!arg_types[i]) return NULL;
        }

        // Pass 2: Collect viable candidates
        Symbol **viable = alloca(sizeof(Symbol*) * n_cands);
        int **mk = alloca(sizeof(int*) * n_cands);
        size_t n_viable = 0;

        for (size_t oi = 0; oi < n_cands; oi++) {
            Symbol *cand = *(Symbol**)dynarray_get(callee_sym->overloads, oi);
            Type *ft = cand->type;
            if (!ft || ft->kind != TYPE_FUNCTION) continue;
            
            size_t param_start = 0;
            size_t effective_param_count = ft->as.func.param_count;
            
            if (is_instance_method && ft->as.func.param_count > 0) {
                param_start = 1;
                effective_param_count--;
            }

            if (effective_param_count != arg_count) continue;

            int *kinds = alloca(sizeof(int) * (arg_count ? arg_count : 1));
            bool ok = true;
            for (size_t p = 0; p < arg_count; p++) {
                Type *pt = ft->as.func.params[param_start + p];
                if (!pt) { ok = false; break; }
                if (arg_types[p] == pt) {
                    kinds[p] = 2; // exact
                } else if (type_can_implicit_cast(pt, arg_types[p])) {
                    kinds[p] = 1; // cast
                } else {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;

            mk[n_viable] = kinds;
            viable[n_viable] = cand;
            n_viable++;
        }

        if (n_viable == 0) {
            TypeError err = { .kind = TE_NO_MATCHING_OVERLOAD, .span = expr->span, .filename = ctx->filename };
            err.as.name.name = ((Slice*)callee_sym->name_rec->key)->ptr;
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }

        // Pass 3: Dominance Filter
        bool *dominated = alloca(sizeof(bool) * n_viable);
        memset(dominated, 0, sizeof(bool) * n_viable);

        for (size_t a = 0; a < n_viable; a++) {
            for (size_t b = 0; b < n_viable; b++) {
                if (a == b || dominated[a]) continue;
                bool b_dom_a = true, strictly = false;
                for (size_t p = 0; p < arg_count; p++) {
                    if (mk[b][p] < mk[a][p]) { b_dom_a = false; break; }
                    if (mk[b][p] > mk[a][p])   strictly = true;
                }
                if (b_dom_a && strictly) dominated[a] = true;
            }
        }

        Symbol *best = NULL;
        size_t n_best = 0;
        for (size_t i = 0; i < n_viable; i++) {
            if (!dominated[i]) {
                best = viable[i];
                n_best++;
            }
        }

        if (n_best > 1) {
            TypeError err = { .kind = TE_AMBIGUOUS_OVERLOAD, .span = expr->span, .filename = ctx->filename };
            err.as.name.name = ((Slice*)callee_sym->name_rec->key)->ptr;
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }

        callee_sym = best;
        callee_type = best->type;
    } else if (callee_sym) {
        callee_type = callee_sym->type;
    }

    if (!callee_type) return NULL;

    if (callee_type->kind != TYPE_FUNCTION) {
        TypeError err = { .kind = TE_NOT_CALLABLE, .span = call->callee->span, .filename = ctx->filename, .as.bad_usage.actual = callee_type };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    // Bind specific symbol and type to callee node
    if (call->callee->node_type == AST_IDENTIFIER) {
        call->callee->data.identifier.symbol = callee_sym;
        call->callee->type = callee_type;
    } else if (call->callee->node_type == AST_MEMBER_EXPR) {
        call->callee->data.member_expr.symbol = callee_sym;
        call->callee->type = callee_type;
    }

    // =========================================================================
    // 3. METHOD SELF INJECTION
    // =========================================================================
    if (is_instance_method && call->callee->node_type == AST_MEMBER_EXPR) {
        AstMemberExpr *mem = &call->callee->data.member_expr;
        
        // If not already injected, perform self-injection
        if (!mem->self_injected && mem->target->type && callee_type->as.func.param_count > 0) {
            Type *first_param_type = callee_type->as.func.params[0];
            Type *target_type = mem->target->type;

            if (first_param_type) {
                AstNode *self_arg = mem->target;

                // Auto-ref: target is S, param is *S
                if (target_type->kind != TYPE_POINTER && first_param_type->kind == TYPE_POINTER) {
                    AstNode *ref = ast_create_node(AST_UNARY_EXPR, ctx->store->arena, ctx->filename);
                    ref->span = self_arg->span;
                    ref->data.unary_expr.op = OP_ADDRESS;
                    ref->data.unary_expr.expr = self_arg;
                    
                    Type proto = { .kind = TYPE_POINTER, .as.ptr.base = target_type };
                    InternResult *res = intern_type(ctx->store, &proto);
                    ref->type = (Type*)((Slice*)res->key)->ptr;
                    self_arg = ref;
                }
                // Auto-deref: target is *S, param is S
                else if (target_type->kind == TYPE_POINTER && first_param_type->kind != TYPE_POINTER) {
                    AstNode *deref = ast_create_node(AST_UNARY_EXPR, ctx->store->arena, ctx->filename);
                    deref->span = self_arg->span;
                    deref->data.unary_expr.op = OP_DEREF;
                    deref->data.unary_expr.expr = self_arg;
                    deref->type = target_type->as.ptr.base;
                    self_arg = deref;
                }

                if (!call->args) {
                    call->args = arena_alloc(ctx->store->arena, sizeof(DynArray));
                    dynarray_init_in_arena(call->args, ctx->store->arena, sizeof(AstNode*), 2);
                }
                
                dynarray_push_ptr(call->args, NULL);
                AstNode **data = (AstNode**)call->args->data;
                if (call->args->count > 1) {
                    memmove(&data[1], &data[0], (call->args->count - 1) * sizeof(AstNode*));
                }
                data[0] = self_arg;
                mem->is_instance_method = true;
                mem->self_injected = true;
            }
        }
    }

    // =========================================================================
    // 4. ARGUMENT VALIDATION & COERCION
    // =========================================================================
    size_t param_count = callee_type->as.func.param_count;
    size_t actual_arg_count = call->args ? call->args->count : 0;

    if (actual_arg_count != param_count) {
        TypeError err = { .kind = TE_ARG_COUNT_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.arg_count = { .expected = param_count, .actual = actual_arg_count } };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    for (size_t i = 0; i < actual_arg_count; i++) {
        AstNode *arg = *(AstNode**)dynarray_get(call->args, i);
        Type *param_type = callee_type->as.func.params[i];
        
        // Re-check expression with the specific parameter type hint to ensure correct promotion (e.g. literals)
        if (check_expression(ctx, scope, arg, param_type)) {
            coerce_or_error(ctx, arg, param_type);
        }
    }

    return callee_type->as.func.return_type;
}

// =============================================================================
// SECTION 4: COLLECTIONS AND ASSIGNMENT
// =============================================================================

Type* check_subscript(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    AstSubscriptExpr *subscript = &expr->data.subscript_expr;
    
    // =========================================================================
    // 1. TOP-DOWN TYPE INFERENCE (BIDIRECTIONAL TYPE CHECKING)
    // =========================================================================
    // If the parent expression knows what type it expects *out* of this subscript 
    // (e.g., `int x = arr[0];` expects an `int`), we can infer that the `target` 
    // (the `arr`) must be a collection of `int`s.
    Type *target_expected = NULL;
    if (expected_type) {
        // We dynamically construct a prototype slice to act as a "generic collection" hint.
        // If we expect the element to be `T`, the collection hint becomes `[]T`.
        Type proto = {0};
        proto.kind = TYPE_SLICE; 
        proto.as.slice.base = expected_type;
        
        // Intern the prototype to get a safe, canonical type pointer to pass down.
        InternResult *res = intern_type(ctx->store, &proto);
        target_expected = res ? (Type*)((Slice*)res->key)->ptr : NULL;
    }

    // =========================================================================
    // 2. TARGET RESOLUTION
    // =========================================================================
    // Check the target expression (the `a` in `a[i]`), passing down our synthesized hint.
    // This is crucial for things like anonymous nested arrays: `{{1, 2}, {3, 4}}[0]`
    Type *base_type = check_expression(ctx, scope, subscript->target, target_expected);
    if (!base_type) return NULL; // Error already logged by check_expression

    // Ensure the resolved target type is actually indexable.
    if (base_type->kind != TYPE_ARRAY && base_type->kind != TYPE_SLICE && base_type->kind != TYPE_POINTER) {
        TypeError err = { 
            .kind = TE_NOT_INDEXABLE, 
            .span = subscript->target->span, 
            .filename = ctx->filename, 
            .as.bad_usage.actual = base_type 
        };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    // =========================================================================
    // 3. INDEX RESOLUTION & COERCION
    // =========================================================================
    // The index must evaluate to an integer type (we hint for i64).
    Type *index_type = check_expression(ctx, scope, subscript->index, ctx->store->t_i64);
    if (index_type) {
        // Force coercion to exactly i64 (e.g., if it's an i32 or an implicitly castable literal).
        coerce_or_error(ctx, subscript->index, ctx->store->t_i64);
    } else {
        return NULL; // Failed to typecheck the index
    }

    // =========================================================================
    // 4. BOUNDS CHECKING & RETURN TYPE DETERMINATION
    // =========================================================================
    // Compile-time bounds checking for fixed-size arrays.
    if (base_type->kind == TYPE_ARRAY) {
        // Determine if the index is a constant expression we can evaluate right now.
        bool is_const = subscript->index->is_foldable_const;
        
        // Fallback: If literal wasn't flagged as foldable but IS an integer literal, force it.
        if (!is_const && subscript->index->node_type == AST_LITERAL && subscript->index->data.literal.type == INT_LITERAL) {
             is_const = true; 
        }
        
        if (is_const) {
            int64_t idx = subscript->index->const_value.value.int_val;
            int64_t limit = base_type->as.array.size;

            // If the literal index is negative or exceeds the known array size, emit an error.
            if (idx < 0 || idx >= limit) {
                TypeError err = { 
                    .kind = TE_INDEX_OUT_OF_BOUNDS, 
                    .span = subscript->index->span, 
                    .filename = ctx->filename, 
                    .as.size = { .expected_size = (size_t)limit, .actual_size = (size_t)idx } 
                };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
        }
    }

    // Return the underlying element type depending on the specific collection type.
    return (base_type->kind == TYPE_ARRAY) ? base_type->as.array.base : 
           (base_type->kind == TYPE_SLICE) ? base_type->as.slice.base :
           base_type->as.ptr.base;
}

/**
 * Validates an assignment expression, ensuring the LHS is an l-value 
 * and types are compatible.
 */
Type* check_assignment(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstAssignmentExpr *assign = &expr->data.assignment_expr;

    // =========================================================================
    // 1. LHS/RHS RESOLUTION
    // =========================================================================
    // We check the LHS first to get a type hint for the RHS.
    Type *lhs = check_expression(ctx, scope, assign->lvalue, NULL);
    Type *rhs = check_expression(ctx, scope, assign->rvalue, lhs); 

    // =========================================================================
    // 2. L-VALUE VALIDATION
    // =========================================================================
    if (!is_lvalue_node(assign->lvalue)) {
        TypeError err = { .kind = TE_NOT_LVALUE, .span = assign->lvalue->span, .filename = ctx->filename };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    if (!lhs || !rhs) return NULL;

    // =========================================================================
    // 3. OPERATOR & TYPE CONSISTENCY
    // =========================================================================
    if (assign->op != OP_ASSIGN) {
        // Compound assignments (+=, -=, etc.) require numeric types.
        if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) {
            TypeError err = { .kind = TE_BINOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.binop = { .op = assign->op, .left = lhs, .right = rhs } };
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }
    }

    // Perform implicit coercion if necessary.
    if (lhs != rhs) {
        coerce_or_error(ctx, assign->rvalue, lhs);
    }

    expr->type = lhs;
    return lhs;
}

// =============================================================================
// SECTION 5: INITIALIZATION & AGGREGATES
// =============================================================================

static int get_type_rank(Type *t) {
    int rank = 0;
    while (t) {
        if (t->kind == TYPE_ARRAY) {
            rank++;
            t = t->as.array.base;
        } else if (t->kind == TYPE_SLICE) {
            rank++;
            t = t->as.slice.base;
        } else {
            break;
        }
    }
    return rank;
}

static int get_initializer_rank(AstNode *node) {
    if (!node || node->node_type != AST_INITIALIZER_LIST) return 0;
    if (node->data.initializer_list.elements->count == 0) return 1;
    AstNode *first = *(AstNode**)dynarray_get(node->data.initializer_list.elements, 0);
    return 1 + get_initializer_rank(first);
}

/**
 * Validates an initializer list, ensuring all elements have compatible types
 * and verifying array/slice structural dimensions (ranks).
 */
Type* check_initializer_list(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    if (!expr || expr->node_type != AST_INITIALIZER_LIST) return NULL;
    
    AstInitializeList *list = &expr->data.initializer_list;
    size_t elem_count = list->elements ? list->elements->count : 0;

    // 1. Context Requirement: If no expected type, try to infer from elements
    if (!expected_type) {
        if (elem_count == 0) return NULL; // Cannot infer empty list type
        
        AstNode **first_ptr = (AstNode**)dynarray_get(list->elements, 0);
        Type *first_ty = check_expression(ctx, scope, *first_ptr, NULL);
        if (!first_ty) return NULL;
        
        Type proto = {0};
        proto.kind = TYPE_ARRAY;
        proto.as.array.base = first_ty;
        proto.as.array.size = (int64_t)elem_count;
        InternResult *res = intern_type(ctx->store, &proto);
        expected_type = res ? (Type*)((Slice*)res->key)->ptr : NULL;
        if (!expected_type) return NULL;
    }

    // 2. Structural Mismatch (Array vs Scalar)
    if (expected_type->kind != TYPE_ARRAY && expected_type->kind != TYPE_SLICE) {
         TypeError err = {
            .kind = TE_UNEXPECTED_LIST,
            .span = expr->span,
            .filename = ctx->filename,
            .as.mismatch = { .expected = expected_type, .actual = NULL } 
        };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    // 3. Rank (Dimension) Mismatch
    int type_rank = get_type_rank(expected_type);
    int init_rank = get_initializer_rank(expr);

    if (type_rank != init_rank) {
        TypeError err = {
            .kind = TE_DIMENSION_MISMATCH,
            .span = expr->span,
            .filename = ctx->filename,
            .as.dims = { .expected_ndim = type_rank, .actual_ndim = init_rank }
        };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }
    
    // 4. Size Mismatch
    if (expected_type->kind == TYPE_ARRAY) {
        if (elem_count != (size_t)expected_type->as.array.size) {
             TypeError err = {
                .kind = TE_ARRAY_SIZE_MISMATCH,
                .span = expr->span,
                .filename = ctx->filename,
                .as.size = { .expected_size = expected_type->as.array.size, .actual_size = elem_count }
            };
            dynarray_push_value(ctx->errors, &err);
            return NULL; 
        }
    }

    Type *base_expected = (expected_type->kind == TYPE_ARRAY) ? expected_type->as.array.base : expected_type->as.slice.base;
    Type *common_base = NULL;
    bool any_error = false;

    // 5. Element Checks
    for (size_t i = 0; i < elem_count; i++) {
        AstNode **node_ptr = (AstNode**)dynarray_get(list->elements, i);
        AstNode *node = *node_ptr;
        
        Type *actual_elem_type = check_expression(ctx, scope, node, base_expected);
        
        if (!actual_elem_type) {
            any_error = true;
            break; 
        }

        // Structural Consistency
        if ((base_expected->kind == TYPE_ARRAY || base_expected->kind == TYPE_SLICE) && 
            (actual_elem_type->kind != TYPE_ARRAY && actual_elem_type->kind != TYPE_SLICE)) {
             TypeError err = {
                .kind = TE_EXPECTED_ARRAY,
                .span = node->span,
                .filename = ctx->filename,
                .as.mismatch = { .expected = base_expected, .actual = actual_elem_type }
            };
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }
        
        if ((base_expected->kind != TYPE_ARRAY && base_expected->kind != TYPE_SLICE) && 
            (actual_elem_type->kind == TYPE_ARRAY || actual_elem_type->kind == TYPE_SLICE)) {
             TypeError err = {
                .kind = TE_TYPE_MISMATCH,
                .span = node->span,
                .filename = ctx->filename,
                .as.mismatch = { .expected = base_expected, .actual = actual_elem_type }
            };
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }

        // Explicit Type Check & Cast
        if (actual_elem_type != base_expected) {
            if (coerce_or_error(ctx, node, base_expected)) {
                actual_elem_type = base_expected; 
            } else {
                return NULL;
            }
        }

        if (i == 0) {
            common_base = actual_elem_type;
        }
    }

    if (any_error) return NULL;

    Type *final_base = common_base ? common_base : base_expected;
    
    Type new_type = {0};
    new_type.kind = TYPE_ARRAY;
    new_type.as.array.base = final_base;
    new_type.as.array.size = elem_count;
    
    InternResult *res = intern_type(ctx->store, &new_type);
    Type *concrete_type = (Type*)((Slice*)res->key)->ptr;
    
    // Check if the initializer list is entirely composed of constants safe for global init
    bool all_llvm_const = true;
    for (size_t i = 0; i < elem_count; i++) {
        AstNode **node_ptr = (AstNode**)dynarray_get(list->elements, i);
        AstNode *node = *node_ptr;
        if (!node->is_llvm_const_safe) {
            all_llvm_const = false;
            break;
        }
    }
    expr->is_foldable_const = 0; 
    expr->is_llvm_const_safe = all_llvm_const ? 1 : 0;

    expr->type = concrete_type;
    return concrete_type;
}

/**
 * Validates a structural instantiation (`Struct { field: value }`),
 * ensuring all required fields are provided and correctly typed.
 */
static Type* check_struct_literal(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    AstStructLiteral *lit = &expr->data.struct_literal;

    Type *struct_type = NULL;
    if (lit->type_node) {
         struct_type = resolve_ast_type(ctx, scope, lit->type_node);
    }

    if (!struct_type || struct_type->kind != TYPE_STRUCT) {
        const char *name_str = "<unknown>";
        TypeError err = { .kind = TE_UNKNOWN_TYPE, .span = expr->span, .filename = ctx->filename, .as.name.name = name_str };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    expr->type = struct_type; // Ensure type is set early
    size_t defined_field_count = struct_type->as.struct_type.field_count;
    size_t lit_field_count = lit->fields ? lit->fields->count : 0;

    if (lit_field_count != defined_field_count) {
        TypeError err = { .kind = TE_ARG_COUNT_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.arg_count = { .expected = defined_field_count, .actual = lit_field_count } };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    bool all_llvm_const = true;

    // Check each field against its definition
    for (size_t i = 0; i < lit_field_count; i++) {
        AstFieldInit *init = (AstFieldInit*)dynarray_get(lit->fields, i);
        
        // O(1) Lookup
        void *field_idx_ptr = hashmap_get(struct_type->as.struct_type.field_map, init->name->key, ptr_hash, ptr_cmp);
        if (!field_idx_ptr) {
            const char *name_str = "<unknown>";
            if (init->name && init->name->key) name_str = ((Slice*)init->name->key)->ptr;
            TypeError err = { .kind = TE_FIELD_ACCESS, .span = init->expr->span, .filename = ctx->filename, .as.name.name = name_str };
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }
        
        // Retrieve field index by subtracting the 1-based offset
        size_t field_idx = (size_t)(uintptr_t)field_idx_ptr - 1;
        StructField *def_field = &struct_type->as.struct_type.fields[field_idx];

        if (check_expression(ctx, scope, init->expr, def_field->type)) {
            coerce_or_error(ctx, init->expr, def_field->type);
        }

        if (!init->expr->is_llvm_const_safe) {
            all_llvm_const = false;
        }
    }

    expr->is_foldable_const = 0;
    expr->is_llvm_const_safe = all_llvm_const ? 1 : 0;

    return struct_type;
}

// =============================================================================
// SECTION 6: INTRINSICS
// =============================================================================

static void validate_allocator_structure(TypeCheckContext *ctx, AstNode *alloc_arg, Type *alloc_ty) {
    if (!alloc_ty) return;
    
    Type *t = alloc_ty;
    if (t->kind == TYPE_POINTER) t = t->as.ptr.base;

    if (t->kind != TYPE_STRUCT) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alloc_arg->span, .filename = ctx->filename };
        err.as.mismatch.expected = ctx->store->t_void; // Placeholder for struct
        err.as.mismatch.actual = alloc_ty;
        dynarray_push_value(ctx->errors, &err);
        return;
    }

    if (t->as.struct_type.field_count != 3) {
        TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = alloc_arg->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator struct must have exactly 3 fields: ctx, _alloc, _free" };
        dynarray_push_value(ctx->errors, &err);
        return;
    }

    const char *expected_fields[] = {"ctx", "_alloc", "_free"};
    for (int i = 0; i < 3; i++) {
        StructField *field = &t->as.struct_type.fields[i];
        if (!field->name || !field->name->key) {
             TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = alloc_arg->span, .filename = ctx->filename,
                              .as.name.name = "Allocator struct fields must have names" };
            dynarray_push_value(ctx->errors, &err);
            return;
        }
        Slice *field_name = (Slice*)field->name->key;
        if (strncmp(field_name->ptr, expected_fields[i], field_name->len) != 0 || expected_fields[i][field_name->len] != '\0') {
            TypeError err = { .kind = TE_INCOMPLETE_TYPE, .span = alloc_arg->span, .filename = ctx->filename,
                              .as.name.name = "Allocator struct fields must be named: ctx, _alloc, _free" };
            dynarray_push_value(ctx->errors, &err);
            return;
        }
    }
    
    StructField *ctx_field = &t->as.struct_type.fields[0];
    StructField *alloc_field = &t->as.struct_type.fields[1];
    StructField *free_field = &t->as.struct_type.fields[2];

    if (!ctx_field->type || ctx_field->type->kind != TYPE_POINTER) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alloc_arg->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator field 'ctx' must be a pointer" };
        dynarray_push_value(ctx->errors, &err);
    }
    
    if (!alloc_field->type || alloc_field->type->kind != TYPE_FUNCTION) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alloc_arg->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator field '_alloc' must be a function" };
        dynarray_push_value(ctx->errors, &err);
    } else {
        Type *fn_ty = alloc_field->type;
        if (fn_ty->as.func.param_count != 2 || 
            fn_ty->as.func.params[0]->kind != TYPE_POINTER ||
            (fn_ty->as.func.params[1]->kind != TYPE_PRIMITIVE || fn_ty->as.func.params[1]->as.primitive != PRIM_I64) ||
            fn_ty->as.func.return_type->kind != TYPE_POINTER) {
            TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alloc_arg->span, .filename = ctx->filename, 
                              .as.name.name = "Allocator field '_alloc' must have signature: fn(ptr, i64) -> ptr" };
            dynarray_push_value(ctx->errors, &err);
        }
    }

    if (!free_field->type || free_field->type->kind != TYPE_FUNCTION) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alloc_arg->span, .filename = ctx->filename, 
                          .as.name.name = "Allocator field '_free' must be a function" };
        dynarray_push_value(ctx->errors, &err);
    } else {
        Type *fn_ty = free_field->type;
        if (fn_ty->as.func.param_count != 2 || 
            fn_ty->as.func.params[0]->kind != TYPE_POINTER ||
            fn_ty->as.func.params[1]->kind != TYPE_POINTER ||
            !type_is_void(fn_ty->as.func.return_type)) {
            TypeError err = { .kind = TE_TYPE_MISMATCH, .span = alloc_arg->span, .filename = ctx->filename, 
                              .as.name.name = "Allocator field '_free' must have signature: fn(ptr, ptr) -> void" };
            dynarray_push_value(ctx->errors, &err);
        }
    }
}

/**
 * Validates compiler intrinsics (e.g. @alloc, @free), performing custom 
 * type checking logic per-intrinsic since they often bypass normal rules.
 */
static Type *check_intrinsic(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    AstNode *node = expr;
    IntrinsicKind kind = node->data.intrinsic.kind;
    size_t arg_count = node->data.intrinsic.args ? node->data.intrinsic.args->count : 0;

    if (kind == INTRINSIC_ALLOC) {
        if (arg_count < 2 || arg_count > 3) {
            TypeError err = { .kind = TE_ARG_COUNT_MISMATCH, .span = node->span, .filename = ctx->filename };
            err.as.arg_count.expected = 2; // expects 2 or 3, simplified
            err.as.arg_count.actual = arg_count;
            dynarray_push_value(ctx->errors, &err);
            return ctx->store->t_void_ptr;
        }

        AstNode *type_arg = *(AstNode**)dynarray_get(node->data.intrinsic.args, 0);
        AstNode *alloc_arg = *(AstNode**)dynarray_get(node->data.intrinsic.args, 1);
        AstNode *count_arg = arg_count == 3 ? *(AstNode**)dynarray_get(node->data.intrinsic.args, 2) : NULL;

        // 1. Arg 0: Must be a type
        Type *allocated_type = resolve_ast_type(ctx, scope, type_arg);
        
        if (allocated_type) {
            type_arg->type = allocated_type;
        } else {
            // It couldn't be resolved as a type. Let's see what it evaluates to as an expression
            // to provide a better error message.
            Type *actual = check_expression(ctx, scope, type_arg, NULL);
            TypeError err = { .kind = TE_EXPECTED_TYPE_ARG, .span = type_arg->span, .filename = ctx->filename };
            err.as.mismatch.expected = NULL;
            err.as.mismatch.actual = actual;
            dynarray_push_value(ctx->errors, &err);
        }

        // 2. Arg 1: Must be an allocator struct
        Type *alloc_ty = check_expression(ctx, scope, alloc_arg, NULL);
        validate_allocator_structure(ctx, alloc_arg, alloc_ty);

        // 3. Arg 2: Must be an integer if present
        if (count_arg) {
            Type *count_ty = check_expression(ctx, scope, count_arg, ctx->store->t_i64);
            if (count_ty && !type_is_integer(count_ty)) {
                TypeError err = { .kind = TE_TYPE_MISMATCH, .span = count_arg->span, .filename = ctx->filename };
                err.as.mismatch.expected = ctx->store->t_i64;
                err.as.mismatch.actual = count_ty;
                dynarray_push_value(ctx->errors, &err);
            }
        }

        if (allocated_type) {
            // Context-aware allocation: If a slice is expected, return a slice instead of a pointer.
            if (expected_type && expected_type->kind == TYPE_SLICE && expected_type->as.slice.base == allocated_type) {
                 node->type = expected_type;
            } else {
                Type proto = { .kind = TYPE_POINTER, .as.ptr.base = allocated_type };
                InternResult *res = intern_type(ctx->store, &proto);
                if (res && res->key) node->type = (Type*)((Slice*)res->key)->ptr;
            }
        }

        if (!node->type) node->type = ctx->store->t_void_ptr;
        return node->type;
    } 
    else if (kind == INTRINSIC_FREE) {
        if (arg_count != 2) {
            TypeError err = { .kind = TE_ARG_COUNT_MISMATCH, .span = node->span, .filename = ctx->filename };
            err.as.arg_count.expected = 2;
            err.as.arg_count.actual = arg_count;
            dynarray_push_value(ctx->errors, &err);
            return ctx->store->t_void;
        }
        
        AstNode *alloc_arg = *(AstNode**)dynarray_get(node->data.intrinsic.args, 0);
        AstNode *ptr_arg = *(AstNode**)dynarray_get(node->data.intrinsic.args, 1);

        // 1. Arg 0: Must be an allocator struct
        Type *alloc_ty = check_expression(ctx, scope, alloc_arg, NULL);
        validate_allocator_structure(ctx, alloc_arg, alloc_ty);

        // 2. Arg 1: Must be a pointer or slice
        Type *ptr_ty = check_expression(ctx, scope, ptr_arg, NULL);
        if (ptr_ty && ptr_ty->kind != TYPE_POINTER && ptr_ty->kind != TYPE_SLICE) {
            TypeError err = { .kind = TE_TYPE_MISMATCH, .span = ptr_arg->span, .filename = ctx->filename };
            err.as.mismatch.expected = ctx->store->t_void_ptr; 
            err.as.mismatch.actual = ptr_ty;
            dynarray_push_value(ctx->errors, &err);
        }

        return ctx->store->t_void;
    }
    
    return ctx->store->t_void;
}

// =============================================================================
// SECTION 7: OPERATORS
// =============================================================================

/**
 * Validates unary operations (-x, !x, *x, &x).
 */
Type* check_unary(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    AstUnaryExpr *unary = &expr->data.unary_expr;
    
    Type *hint = NULL;
    if (expected_type && type_is_numeric(expected_type)) {
        if (unary->op == OP_SUB || unary->op == OP_ADD) {
             hint = expected_type;
        }
    }
    if (unary->op == OP_NOT && expected_type == ctx->store->t_bool) {
        hint = expected_type;
    }

    Type *operand_type = check_expression(ctx, scope, unary->expr, hint);
    if (!operand_type) return NULL;

    switch (unary->op) {
        case OP_NOT: 
            if (operand_type != ctx->store->t_bool) { 
                TypeError err = { .kind = TE_UNOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.unop = { .op = unary->op, .operand = operand_type } };
                dynarray_push_value(ctx->errors, &err);
                return NULL; 
            }
            if (unary->expr->is_foldable_const) fold_unary_op(expr, unary->op, unary->expr);
            return ctx->store->t_bool;
        case OP_SUB: 
            if (!type_is_numeric(operand_type)) { 
                TypeError err = { .kind = TE_UNOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.unop = { .op = unary->op, .operand = operand_type } };
                dynarray_push_value(ctx->errors, &err);
                return NULL; 
            }
            if (unary->expr->is_foldable_const) fold_unary_op(expr, unary->op, unary->expr);
            return operand_type;
        case OP_ADDRESS: 
            if (!is_lvalue_node(unary->expr)) { 
                TypeError err = { .kind = TE_NOT_LVALUE, .span = unary->expr->span, .filename = ctx->filename };
                dynarray_push_value(ctx->errors, &err);
                return NULL; 
            }
            {
                Type proto = { .kind = TYPE_POINTER, .as.ptr.base = operand_type };
                InternResult *res = intern_type(ctx->store, &proto);
                return res ? (Type*)((Slice*)res->key)->ptr : NULL;
            }
        case OP_DEREF: 
            if (operand_type->kind != TYPE_POINTER) { 
                TypeError err = { .kind = TE_UNOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.unop = { .op = unary->op, .operand = operand_type } };
                dynarray_push_value(ctx->errors, &err);
                return NULL; 
            }
            if (type_is_void(operand_type->as.ptr.base)) {
                TypeError err = { .kind = TE_UNOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.unop = { .op = unary->op, .operand = operand_type } };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
            return operand_type->as.ptr.base;
        default: return NULL;
    }
}

/**
 * Validates binary operations (x + y, x == y, x && y), enforcing strict
 * type consistency and applying literal inference.
 */
Type* check_binary(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    AstBinaryExpr *bin = &expr->data.binary_expr;
    OpKind op = bin->op;

    Type *lhs_hint = NULL;
    if (expected_type && type_is_numeric(expected_type)) {
        if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD) {
            lhs_hint = expected_type;
        }
    }

    Type *lhs = check_expression(ctx, scope, bin->left, lhs_hint);

    Type *rhs_hint = NULL;
    if (lhs && type_is_numeric(lhs)) {
        rhs_hint = lhs;
    } else if (expected_type && type_is_numeric(expected_type) && lhs_hint) {
        rhs_hint = expected_type;
    }

    Type *rhs = check_expression(ctx, scope, bin->right, rhs_hint);
    if (!lhs || !rhs) return NULL;

    // 1. Literal Inference: If one side is a literal, try to adapt it to the other side's type
    if (bin->left->node_type == AST_LITERAL && lhs != rhs) {
         lhs = check_expression(ctx, scope, bin->left, rhs);
    } 
    else if (bin->right->node_type == AST_LITERAL && lhs != rhs) {
         rhs = check_expression(ctx, scope, bin->right, lhs);
    }

    // 2. STRICTOR RULES: Operands must match exactly after inference
    if (lhs != rhs) {
        TypeError err = { .kind = TE_BINOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.binop = { .op = op, .left = lhs, .right = rhs } };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    Type *result_type = NULL;

    if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD) {
        if (!type_is_numeric(lhs)) { 
            TypeError err = { .kind = TE_BINOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.binop = { .op = op, .left = lhs, .right = rhs } };
            dynarray_push_value(ctx->errors, &err);
            return NULL; 
        }
        result_type = lhs;
    } 
    else if (op == OP_EQ || op == OP_NEQ || op == OP_LT || op == OP_GT || op == OP_LE || op == OP_GE) {
        // Disallow struct/array equality for now
        if (lhs->kind != TYPE_PRIMITIVE && lhs->kind != TYPE_POINTER) {
             TypeError err = { .kind = TE_BINOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.binop = { .op = op, .left = lhs, .right = rhs } };
             dynarray_push_value(ctx->errors, &err);
             return NULL;
        }
        result_type = ctx->store->t_bool;
    }
    else if (op == OP_AND || op == OP_OR) {
        if (lhs != ctx->store->t_bool) { 
            TypeError err = { .kind = TE_BINOP_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.binop = { .op = op, .left = lhs, .right = rhs } };
            dynarray_push_value(ctx->errors, &err);
            return NULL; 
        }
        result_type = ctx->store->t_bool;
    }

    if (bin->left->is_foldable_const && bin->right->is_foldable_const) {
        fold_binary_op(expr, op, bin->left, bin->right);
        // Correct const value type if needed (e.g. promoted in literal inference)
        if (result_type && type_is_float(result_type) && expr->const_value.type == INT_LITERAL) {
            expr->const_value.type = FLOAT_LITERAL;
            expr->const_value.value.float_val = (double)expr->const_value.value.int_val;
        }
    }
    return result_type;
}

static Type* check_member_expr(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstMemberExpr *member_expr = &expr->data.member_expr;
    member_expr->symbol = NULL; // Reset

    // 1. Resolve the target (e.g., the `arr` in `arr.len`)
    Type *target_type = check_expression(ctx, scope, member_expr->target, NULL);
    
    // 2. Special case: Module access (nested or otherwise)
    if (member_expr->target->node_type == AST_IDENTIFIER || member_expr->target->node_type == AST_MEMBER_EXPR) {
        // If the target is an identifier or another member expression, check if it resolved to a module
        Symbol *target_sym = NULL;
        if (member_expr->target->node_type == AST_IDENTIFIER) {
            target_sym = scope_lookup_symbol(scope, member_expr->target->data.identifier.intern_result, ctx->filename);
        } else {
            target_sym = member_expr->target->data.member_expr.symbol;
        }

        if (target_sym && (target_sym->kind == SYMBOL_VALUE_MODULE || target_sym->kind == SYMBOL_VALUE_NAMESPACE)) {
            // Target is a module/namespace! Access its symbols.
            Symbol *member_sym = scope_lookup_symbol_local(target_sym->module_scope, member_expr->member);
            if (!member_sym || !member_sym->is_pub) {
                const char *field_name = "<unknown>";
                if (member_expr->member && member_expr->member->key) {
                    field_name = ((Slice*)member_expr->member->key)->ptr;
                }
                TypeError err = { .kind = TE_UNDECLARED, .span = expr->span, .filename = ctx->filename };
                err.as.name.name = field_name;
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }

            member_expr->symbol = member_sym;
            expr->type = member_sym->type;
            return expr->type;
        }

    }

    if (!target_type) return NULL;

    Type *underlying = target_type;
    if (underlying->kind == TYPE_POINTER) {
        underlying = underlying->as.ptr.base;
    }

    // 3. Dispatch based on the type we are accessing
    switch (underlying->kind) {
        
        case TYPE_ARRAY:
            if (member_expr->member == ctx->store->kw_len) {
                // Fixed-size array: Morph the node directly into an Integer Literal.
                expr->node_type = AST_LITERAL;
                expr->data.literal.type = INT_LITERAL;
                expr->data.literal.value.int_val = underlying->as.array.size;
                expr->is_foldable_const = 1;
                expr->is_llvm_const_safe = 1;
                expr->const_value.type = INT_LITERAL;
                expr->const_value.value.int_val = underlying->as.array.size;
                expr->type = ctx->store->t_i64;
                return ctx->store->t_i64;
            } else {
                const char *field_name = "<unknown>";
                if (member_expr->member && member_expr->member->key) {
                    field_name = ((Slice*)member_expr->member->key)->ptr;
                }
                TypeError err = { .kind = TE_FIELD_ACCESS, .span = expr->span, .filename = ctx->filename };
                err.as.field.name = field_name;
                err.as.field.type = underlying;
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }

        case TYPE_SLICE:
            if (member_expr->member == ctx->store->kw_len) {
                // Slice: Index 1 is the 'len' in the { ptr, len } fat pointer struct
                return ctx->store->t_i64;
            } else {
                const char *field_name = "<unknown>";
                if (member_expr->member && member_expr->member->key) {
                    field_name = ((Slice*)member_expr->member->key)->ptr;
                }
                TypeError err = { .kind = TE_FIELD_ACCESS, .span = expr->span, .filename = ctx->filename };
                err.as.field.name = field_name;
                err.as.field.type = underlying;
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }

        case TYPE_STRUCT: {
            // 1. Find the field
            for (size_t i = 0; i < underlying->as.struct_type.field_count; i++) {
                if (underlying->as.struct_type.fields[i].name == member_expr->member) {
                    return underlying->as.struct_type.fields[i].type;
                }
            }
            
            // 2. Find the method
            Symbol *method_sym = hashmap_get(underlying->as.struct_type.methods, member_expr->member->key, ptr_hash, ptr_cmp);
            if (method_sym) {
                member_expr->symbol = method_sym;
                
                // Determine if this is an instance call or a static call
                // It's an instance call if the target is NOT a type name.
                bool target_is_type = false;
                if (member_expr->target->node_type == AST_IDENTIFIER) {
                    Symbol *s = member_expr->target->data.identifier.symbol;
                    if (s && s->kind == SYMBOL_VALUE_TYPE) target_is_type = true;
                }
                
                member_expr->is_instance_method = !target_is_type;
                expr->type = method_sym->type;
                return expr->type;
            }

            const char *field_name = "<unknown>";
            if (member_expr->member && member_expr->member->key) {
                field_name = ((Slice*)member_expr->member->key)->ptr;
            }
            TypeError err = { .kind = TE_FIELD_ACCESS, .span = expr->span, .filename = ctx->filename };
            err.as.field.name = field_name;
            err.as.field.type = underlying;
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }

        default:
            {
                const char *field_name = "<unknown>";
                if (member_expr->member && member_expr->member->key) {
                    field_name = ((Slice*)member_expr->member->key)->ptr;
                }
                TypeError err = { .kind = TE_FIELD_ACCESS, .span = expr->span, .filename = ctx->filename };
                err.as.field.name = field_name;
                err.as.field.type = target_type;
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
    }
}

static Type* check_cast(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstCastExpr *cast = &expr->data.cast_expr;
    
    // Resolve the target type from the type node (if it's an explicit 'as' cast)
    if (cast->target_type_node) {
        cast->target_type = resolve_ast_type(ctx, scope, cast->target_type_node);
    }
    
    if (!cast->target_type) return NULL;

    Type *src_type = check_expression(ctx, scope, cast->expr, NULL);
    if (!src_type) return NULL;

    if (!type_can_explicit_cast(cast->target_type, src_type)) {
        TypeError err = { 
            .kind = TE_TYPE_MISMATCH, 
            .span = expr->span, 
            .filename = ctx->filename, 
            .as.mismatch = { .expected = cast->target_type, .actual = src_type } 
        };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    fold_cast_node(expr);

    return cast->target_type;
}

Type* check_expression(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    if (!expr) return NULL;
    
    Type *result_type = NULL;
    expr->is_foldable_const = 0; 
    expr->is_llvm_const_safe = 0;

    switch (expr->node_type) {
        case AST_LITERAL:
            result_type = check_literal(ctx, expr, expected_type);
            break;
        case AST_IDENTIFIER:
            result_type = check_identifier(ctx, scope, expr);
            break;
        case AST_CALL_EXPR:
            result_type = check_call_expr(ctx, scope, expr);
            break;
        case AST_SUBSCRIPT_EXPR:
            result_type = check_subscript(ctx, scope, expr, expected_type);
            break;
        case AST_MEMBER_EXPR:
            result_type = check_member_expr(ctx, scope, expr);
            break;
        case AST_STRUCT_LITERAL:
            result_type = check_struct_literal(ctx, scope, expr, expected_type);
            break;
        case AST_BINARY_EXPR:
            result_type = check_binary(ctx, scope, expr, expected_type);
            break;
        case AST_UNARY_EXPR:
            result_type = check_unary(ctx, scope, expr, expected_type); 
            break;
        case AST_ASSIGNMENT_EXPR:
            result_type = check_assignment(ctx, scope, expr);
            break;
        case AST_INITIALIZER_LIST:
            result_type = check_initializer_list(ctx, scope, expr, expected_type);
            break;
        case AST_CAST:
            result_type = check_cast(ctx, scope, expr);
            break;
        case AST_INTRINSIC:
            result_type = check_intrinsic(ctx, scope, expr, expected_type);
            break;
        default: break;
    }

    expr->type = result_type;
    return result_type;
}