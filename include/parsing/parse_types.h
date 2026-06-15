#pragma once

#include "ast.h"
#include "parser.h"

AstNode *parse_type(Parser *p, ParseError *err);
AstNode *parse_type_atom(Parser *p, ParseError *err);
InternResult *get_base_type(Parser *p, ParseError *err);
AstNode *parse_function_type(Parser *p, ParseError *err);
