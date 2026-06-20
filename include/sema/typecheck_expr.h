#pragma once
#include "typecheck.h"
#include "scope.h"
#include "ast.h"

// Core Expression Checkers
Type* check_literal(TypeCheckContext *ctx, AstNode *expr, Type *expected_type);
Type* check_identifier(TypeCheckContext *ctx, Scope *scope, AstNode *expr);
Type* check_call_expr(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type);
Type* check_subscript(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type);
Type* check_binary(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type);
Type* check_unary(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type); // <--- Added expected_type
Type* check_assignment(TypeCheckContext *ctx, Scope *scope, AstNode *expr);
Type* check_initializer_list(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type);
Type* check_expression(TypeCheckContext *ctx, Scope *scope, AstNode *expr, Type *expected_type);

// Helpers
void insert_cast(TypeCheckContext *ctx, AstNode *node, Type *to_type);
Type* coerce_or_error(TypeCheckContext *ctx, AstNode *node, Type *expected);