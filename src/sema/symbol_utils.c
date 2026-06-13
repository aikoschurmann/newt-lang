#include "sema/symbol_utils.h"
#include "parsing/ast.h" 
#include "core/error.h"
#include <stdio.h>

void define_symbol_or_error(TypeCheckContext *ctx, Scope *scope, InternResult *name, Type *type, SymbolValue kind, Span span, bool is_pub, const char *filename, AstNode *decl_node) {
    if (!scope || !name) return;
    
    Symbol *sym = scope_define_symbol(scope, name, type, kind, filename, is_pub, decl_node);
    if (!sym) {
        const char *name_str = ((Slice*)name->key)->ptr;
        TypeError err = { 
            .kind = TE_REDECLARATION, 
            .span = span, 
            .filename = ctx->filename, 
            .as.name.name = name_str 
        };
        dynarray_push_value(ctx->errors, &err);
    }
}