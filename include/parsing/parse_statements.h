#pragma once

#include "ast.h"
#include "parser.h"

AstNode *parse_statement(Parser *p, ParseError *err);
AstNode *parse_block(Parser *p, ParseError *err);
AstNode *parse_if_statement(Parser *p, ParseError *err);
AstNode *parse_while_statement(Parser *p, ParseError *err);
AstNode *parse_for_statement(Parser *p, ParseError *err);
AstNode *parse_return_statement(Parser *p, ParseError *err);
AstNode *parse_break_statement(Parser *p, ParseError *err);
AstNode *parse_continue_statement(Parser *p, ParseError *err);
AstNode *parse_defer_statement(Parser *p, ParseError *err);
AstNode *parse_expression_statement(Parser *p, ParseError *err);
