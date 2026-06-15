#pragma once

#include "ast.h"
#include "parser.h"

AstNode *parse_program(Parser *p, ParseError *err);
AstNode *parse_declaration(Parser *p, ParseError *err);
AstNode *parse_import_declaration(Parser *p, ParseError *err);
AstNode *parse_alias_declaration(Parser *p, ParseError *err);
AstNode *parse_struct_declaration(Parser *p, ParseError *err);
AstNode *parse_declaration_stmt(Parser *p, ParseError *err);
AstNode *parse_variable_declaration(Parser *p, ParseError *err);
AstNode *parse_function_declaration(Parser *p, ParseError *err);
int parse_parameter_list(Parser *p, AstNode *func_decl, ParseError *err);
