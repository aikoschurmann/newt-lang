#pragma once

#include "sema/typecheck.h"
#include "datastructures/scope.h"
#include "parsing/ast.h"

// Note the change to SymbolValue here!
void define_symbol_or_error(TypeCheckContext *ctx, Scope *scope, InternResult *name, Type *type, SymbolValue kind, Span span, bool is_pub, const char *filename, AstNode *decl_node);

int is_lvalue_node(AstNode *node);