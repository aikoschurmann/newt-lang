#include "sema/typecheck_expr.h"
#include "sema/type_utils.h"
#include "sema/symbol_utils.h"
#include "sema/typecheck.h"
#include "parsing/ast.h" 
#include "datastructures/scope.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
// Cast Helper
// -----------------------------------------------------------------------------

void insert_cast(TypeCheckContext *ctx, AstNode *node, Type *to_type) {
    if (!node || !to_type) return;
    if (node->type == to_type) return;

    AstNode *original = arena_alloc(ctx->store->arena, sizeof(AstNode));
    memcpy(original, node, sizeof(AstNode));

    node->node_type = AST_CAST;
    node->type = to_type;
    node->span = original->span; 
    
    node->data.cast_expr.expr = original;
    node->data.cast_expr.target_type = to_type;

    node->is_const_expr = original->is_const_expr;
    
    if (original->is_const_expr) {
        node->const_value = original->const_value; 
        if (type_is_integer(original->type) && type_is_float(to_type)) {
            node->const_value.type = FLOAT_LITERAL;
            node->const_value.value.float_val = (double)original->const_value.value.int_val;
        } 
        else if (type_is_float(original->type) && type_is_integer(to_type)) {
            node->const_value.type = INT_LITERAL;
            node->const_value.value.int_val = (int64_t)original->const_value.value.float_val;
        }
        else if (type_is_integer(original->type) && type_is_integer(to_type)) {
            node->const_value.type = INT_LITERAL;
        }
    }
}

// -----------------------------------------------------------------------------
// Helpers & Folding
// -----------------------------------------------------------------------------

static Type* unite_numeric_types(TypeCheckContext *ctx, Type *a, Type *b) {
    TypeStore *s = ctx->store;
    if (a == b) return a;
    if (a == s->t_f64 || b == s->t_f64) return s->t_f64;
    if (a == s->t_f32 || b == s->t_f32) return s->t_f32;
    if (a == s->t_i64 || b == s->t_i64) return s->t_i64;
    if (a == s->t_i32 || b == s->t_i32) return s->t_i32;
    return NULL;
}

static void fold_unary_op(AstNode *node, OpKind op, AstNode *operand) {
    if (!operand->is_const_expr) return;
    LiteralType type = operand->const_value.type;

    if (op == OP_NOT && type == BOOL_LITERAL) {
        node->is_const_expr = 1;
        node->const_value.type = BOOL_LITERAL;
        node->const_value.value.bool_val = !operand->const_value.value.bool_val;
        return;
    }

    if (op == OP_SUB) {
        node->is_const_expr = 1;
        node->const_value.type = type;
        if (type == INT_LITERAL) {
            node->const_value.value.int_val = -operand->const_value.value.int_val;
        } else if (type == FLOAT_LITERAL) {
            node->const_value.value.float_val = -operand->const_value.value.float_val;
        }
    }
}

static void fold_binary_op(AstNode *node, OpKind op, AstNode *l, AstNode *r) {
    if (!l->is_const_expr || !r->is_const_expr) return;

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

        node->is_const_expr = 1;
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

        node->is_const_expr = 1;
        if (is_bool) {
            node->const_value.type = BOOL_LITERAL;
            node->const_value.value.bool_val = (bool)res;
        } else {
            node->const_value.type = INT_LITERAL;
            node->const_value.value.int_val = res;
        }
    }
}

// -----------------------------------------------------------------------------
// Core Checkers
// -----------------------------------------------------------------------------

static Type* resolve_literal_type(TypeCheckContext *ctx, LiteralType lit_kind, Type *expected) {
    TypeStore *s = ctx->store;
    switch (lit_kind) {
        case INT_LITERAL:
            if (expected && type_is_float(expected)) return expected;
            if (expected && type_is_integer(expected)) return expected;
            return s->t_i64;
        case FLOAT_LITERAL:
            if (expected && type_is_float(expected)) return expected;
            return s->t_f64;
        case BOOL_LITERAL:   return s->t_bool;
        case CHAR_LITERAL:   return s->t_char;
        case STRING_LITERAL: return s->t_str;
        default: return NULL;
    }
}

Type* check_literal(TypeCheckContext *ctx, AstNode *expr, Type *expected_type) {
    Type *type = resolve_literal_type(ctx, expr->data.literal.type, expected_type);
    
    if (expr->data.literal.type == INT_LITERAL && type && type_is_float(type)) {
        expr->data.literal.type = FLOAT_LITERAL;
        expr->data.literal.value.float_val = (double)expr->data.literal.value.int_val;
    }

    expr->is_const_expr = 1;
    expr->const_value.type  = expr->data.literal.type;
    expr->const_value.value = expr->data.literal.value;
    expr->type = type;
    return type;
}

Type* check_identifier(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstIdentifier *ident = &expr->data.identifier;
    Symbol *sym = scope_lookup_symbol(scope, ident->intern_result);
    
    if (!sym) {
        const char *name_str = "<unknown>";
        if (ident->intern_result && ident->intern_result->key) {
            name_str = ((Slice*)ident->intern_result->key)->ptr;
        }
        TypeError err = { .kind = TE_UNDECLARED, .span = expr->span, .filename = ctx->filename, .as.name.name = name_str };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    expr->type = sym->type;
    if ((sym->flags & SYMBOL_FLAG_CONST) && (sym->flags & SYMBOL_FLAG_COMPUTED_VALUE)) {
        expr->is_const_expr = 1;
        if (type_is_integer(sym->type)) {
            expr->const_value.type = INT_LITERAL;
            expr->const_value.value.int_val = sym->value.int_val;
        } else if (type_is_float(sym->type)) {
            expr->const_value.type = FLOAT_LITERAL;
            expr->const_value.value.float_val = sym->value.float_val;
        } else if (type_is_bool(sym->type)) {
            expr->const_value.type = BOOL_LITERAL;
            expr->const_value.value.bool_val = sym->value.bool_val;
        } else if (type_is_char(sym->type)) {
            expr->const_value.type = CHAR_LITERAL;
            expr->const_value.value.char_val = (char)sym->value.int_val;
        }
    } else {
        expr->is_const_expr = 0;
    }
    return sym->type;
}

Type* check_call_expr(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstCallExpr *call = &expr->data.call_expr;
    Type *callee_type = check_expression(ctx, scope, call->callee, NULL);
    if (!callee_type) return NULL;

    if (callee_type->kind != TYPE_FUNCTION) {
        TypeError err = { .kind = TE_NOT_CALLABLE, .span = call->callee->span, .filename = ctx->filename, .as.bad_usage.actual = callee_type };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    size_t param_count = callee_type->as.func.param_count;
    size_t arg_count = call->args ? call->args->count : 0;

    if (arg_count != param_count) {
        TypeError err = { .kind = TE_ARG_COUNT_MISMATCH, .span = expr->span, .filename = ctx->filename, .as.arg_count = { .expected = param_count, .actual = arg_count } };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    for (size_t i = 0; i < arg_count; i++) {
        AstNode *arg = *(AstNode**)dynarray_get(call->args, i);
        Type *param_type = callee_type->as.func.params[i];
        Type *arg_type = check_expression(ctx, scope, arg, param_type);
        
        if (arg_type && param_type && arg_type != param_type) {
            if (type_can_implicit_cast(param_type, arg_type)) {
                insert_cast(ctx, arg, param_type);
            } else {
                TypeError err = { .kind = TE_TYPE_MISMATCH, .span = arg->span, .filename = ctx->filename, .as.mismatch = { .expected = param_type, .actual = arg_type } };
                dynarray_push_value(ctx->errors, &err);
            }
        }
    }
    return callee_type->as.func.return_type;
}

Type* check_subscript(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstSubscriptExpr *subscript = &expr->data.subscript_expr;
    Type *base_type = check_expression(ctx, scope, subscript->target, NULL);
    if (!base_type) return NULL;

    if (base_type->kind != TYPE_ARRAY && base_type->kind != TYPE_POINTER) {
        TypeError err = { .kind = TE_NOT_INDEXABLE, .span = subscript->target->span, .filename = ctx->filename, .as.bad_usage.actual = base_type };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    Type *index_type = check_expression(ctx, scope, subscript->index, ctx->store->t_i64);
    if (!index_type || !type_is_integer(index_type)) {
        TypeError err = { .kind = TE_TYPE_MISMATCH, .span = subscript->index->span, .filename = ctx->filename, .as.mismatch = { .expected = ctx->store->t_i64, .actual = index_type } };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    return (base_type->kind == TYPE_ARRAY) ? base_type->as.array.base : base_type->as.ptr.base;
}

Type* check_assignment(TypeCheckContext *ctx, Scope *scope, AstNode *expr) {
    AstAssignmentExpr *assign = &expr->data.assignment_expr;
    if (!is_lvalue_node(assign->lvalue)) {
        TypeError err = { .kind = TE_NOT_LVALUE, .span = assign->lvalue->span, .filename = ctx->filename };
        dynarray_push_value(ctx->errors, &err);
        return NULL;
    }

    Type *lhs = check_expression(ctx, scope, assign->lvalue, NULL);
    Type *rhs = check_expression(ctx, scope, assign->rvalue, lhs); 

    if (!lhs || !rhs) return NULL;

    if (lhs != rhs) {
        if (type_can_implicit_cast(lhs, rhs)) {
             insert_cast(ctx, assign->rvalue, lhs);
        } else {
             TypeError err = { .kind = TE_TYPE_MISMATCH, .span = assign->rvalue->span, .filename = ctx->filename, .as.mismatch = { .expected = lhs, .actual = rhs } };
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }
    }
    expr->type = lhs;
    return lhs;
}

// -----------------------------------------------------------------------------
// Structure Helpers
// -----------------------------------------------------------------------------

static int get_type_rank(Type *t) {
    int rank = 0;
    while (t && t->kind == TYPE_ARRAY) {
        rank++;
        t = t->as.array.base;
    }
    return rank;
}

static int get_initializer_rank(AstNode *node) {
    if (!node || node->node_type != AST_INITIALIZER_LIST) return 0;
    if (node->data.initializer_list.elements->count == 0) return 1;
    AstNode *first = *(AstNode**)dynarray_get(node->data.initializer_list.elements, 0);
    return 1 + get_initializer_rank(first);
}

// -----------------------------------------------------------------------------
// Initializer List Checker
// -----------------------------------------------------------------------------

Type* check_initializer_list(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    if (!expr || expr->node_type != AST_INITIALIZER_LIST) return NULL;
    
    // 1. Context Requirement: We need an expected type to infer structure
    if (!expected_type) return NULL; 

    // 2. Structural Mismatch (Array vs Scalar)
    if (expected_type->kind != TYPE_ARRAY) {
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

    AstInitializeList *list = &expr->data.initializer_list;
    size_t elem_count = list->elements ? list->elements->count : 0;
    
    // 4. Size Mismatch
    if (expected_type->as.array.size_known) {
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

    Type *base_expected = expected_type->as.array.base;
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
        if (base_expected->kind == TYPE_ARRAY && actual_elem_type->kind != TYPE_ARRAY) {
             TypeError err = {
                .kind = TE_EXPECTED_ARRAY,
                .span = node->span,
                .filename = ctx->filename,
                .as.mismatch = { .expected = base_expected, .actual = actual_elem_type }
            };
            dynarray_push_value(ctx->errors, &err);
            return NULL;
        }
        
        if (base_expected->kind != TYPE_ARRAY && actual_elem_type->kind == TYPE_ARRAY) {
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
            if (type_can_implicit_cast(base_expected, actual_elem_type)) {
                insert_cast(ctx, node, base_expected);
                actual_elem_type = base_expected; 
            } else {
                TypeError err = { 
                    .kind = TE_TYPE_MISMATCH, 
                    .span = node->span, 
                    .filename = ctx->filename, 
                    .as.mismatch = { .expected = base_expected, .actual = actual_elem_type } 
                };
                dynarray_push_value(ctx->errors, &err);
                return NULL;
            }
        }

        if (i == 0) {
            common_base = actual_elem_type;
        }
    }

    if (any_error) return NULL;

    Type *final_base = common_base ? common_base : base_expected;
    if (base_expected) final_base = base_expected; 

    Type new_type = {0};
    new_type.kind = TYPE_ARRAY;
    new_type.as.array.base = final_base;
    new_type.as.array.size = elem_count;
    new_type.as.array.size_known = true;
    
    InternResult *res = intern_type(ctx->store, &new_type);
    Type *concrete_type = (Type*)((Slice*)res->key)->ptr;
    
    expr->type = concrete_type;
    return concrete_type;
}

// -----------------------------------------------------------------------------
// Unary (Updated with Hint)
// -----------------------------------------------------------------------------

Type* check_unary(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    AstUnaryExpr *unary = &expr->data.unary_expr;
    
    // Propagate hint for numeric operators (e.g. -1 with expected i32 -> Hint i32)
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
            if (operand_type != ctx->store->t_bool) { return NULL; }
            if (unary->expr->is_const_expr) fold_unary_op(expr, unary->op, unary->expr);
            return ctx->store->t_bool;
        case OP_SUB: 
            if (!type_is_numeric(operand_type)) { return NULL; }
            if (unary->expr->is_const_expr) fold_unary_op(expr, unary->op, unary->expr);
            return operand_type;
        case OP_ADRESS: 
            if (!is_lvalue_node(unary->expr)) { return NULL; }
            {
                Type proto = { .kind = TYPE_POINTER, .as.ptr.base = operand_type };
                InternResult *res = intern_type(ctx->store, &proto);
                return res ? (Type*)((Slice*)res->key)->ptr : NULL;
            }
        case OP_DEREF: 
            if (operand_type->kind != TYPE_POINTER) { return NULL; }
            return operand_type->as.ptr.base;
        default: return NULL;
    }
}

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

    if (bin->left->node_type == AST_LITERAL && lhs != rhs) {
        if (type_is_integer(lhs) && type_is_integer(rhs)) {
             lhs = check_expression(ctx, scope, bin->left, rhs);
        } else if (type_is_float(lhs) && type_is_float(rhs)) {
             lhs = check_expression(ctx, scope, bin->left, rhs);
        }
    }

    Type *result_type = NULL;

    if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD) {
        if (!type_is_numeric(lhs) || !type_is_numeric(rhs)) { return NULL; }
        result_type = unite_numeric_types(ctx, lhs, rhs);
        if (!result_type) { return NULL; }
        
        if (lhs != result_type) insert_cast(ctx, bin->left, result_type);
        if (rhs != result_type) insert_cast(ctx, bin->right, result_type);
    } 
    else if (op == OP_EQ || op == OP_NEQ || op == OP_LT || op == OP_GT || op == OP_LE || op == OP_GE) {
        Type *common = unite_numeric_types(ctx, lhs, rhs);
        if (!common && (op == OP_EQ || op == OP_NEQ)) {
            if (lhs == rhs && lhs->kind == TYPE_POINTER) common = lhs;
        }
        if (!common) { return NULL; }

        if (lhs != common) insert_cast(ctx, bin->left, common);
        if (rhs != common) insert_cast(ctx, bin->right, common);
        result_type = ctx->store->t_bool;
    }
    else if (op == OP_AND || op == OP_OR) {
        if (lhs != ctx->store->t_bool || rhs != ctx->store->t_bool) { return NULL; }
        result_type = ctx->store->t_bool;
    }

    if (bin->left->is_const_expr && bin->right->is_const_expr) {
        fold_binary_op(expr, op, bin->left, bin->right);
        if (result_type && type_is_float(result_type) && expr->const_value.type == INT_LITERAL) {
            expr->const_value.type = FLOAT_LITERAL;
            expr->const_value.value.float_val = (double)expr->const_value.value.int_val;
        }
    }
    return result_type;
}

Type* check_expression(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type) {
    if (!expr) return NULL;
    
    Type *result_type = NULL;
    expr->is_const_expr = 0; 

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
            result_type = check_subscript(ctx, scope, expr);
            break;
        case AST_BINARY_EXPR:
            result_type = check_binary(ctx, scope, expr, expected_type);
            break;
        case AST_UNARY_EXPR:
            result_type = check_unary(ctx, scope, expr, expected_type); // <--- Added expected_type
            break;
        case AST_ASSIGNMENT_EXPR:
            result_type = check_assignment(ctx, scope, expr);
            break;
        case AST_INITIALIZER_LIST:
            result_type = check_initializer_list(ctx, scope, expr, expected_type);
            break;
        case AST_CAST:
            result_type = expr->data.cast_expr.target_type;
            break;
        default: break;
    }

    expr->type = result_type;
    return result_type;
}