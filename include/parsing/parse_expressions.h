#pragma once

#include "ast.h"
#include "parser.h"

AstNode *parse_expression(Parser *p, ParseError *err);
AstNode *parse_assignment(Parser *p, AstNode *lhs, ParseError *err);
AstNode *parse_logical_or(Parser *p, ParseError *err);
AstNode *parse_logical_and(Parser *p, ParseError *err);
AstNode *parse_equality(Parser *p, ParseError *err);
AstNode *parse_relational(Parser *p, ParseError *err);
AstNode *parse_additive(Parser *p, ParseError *err);
AstNode *parse_multiplicative(Parser *p, ParseError *err);
AstNode *parse_cast(Parser *p, ParseError *err);
AstNode *parse_unary(Parser *p, ParseError *err);
AstNode *parse_postfix(Parser *p, ParseError *err);
AstNode *parse_primary(Parser *p, ParseError *err);
AstNode *parse_initializer_list(Parser *p, ParseError *err);
int parse_argument_list(Parser *p, AstNode* call, ParseError *err);
