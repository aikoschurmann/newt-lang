#pragma once

#include "ast.h"
#include "parser.h"

AstNode *parse_program(Parser *p, ParseError *err);
AstNode *parse_declaration(Parser *p, ParseError *err);
AstNode *parse_import_declaration(Parser *p, ParseError *err);
AstNode *parse_struct_declaration(Parser *p, ParseError *err);
AstNode *parse_variable_declaration(Parser *p, ParseError *err);
AstNode *parse_function_declaration(Parser *p, ParseError *err);
AstNode *parse_declaration_stmt(Parser *p, ParseError *err);
AstNode *parse_statement(Parser *p, ParseError *err);
AstNode *parse_block(Parser *p, ParseError *err);
AstNode *parse_expression(Parser *p, ParseError *err);
AstNode *parse_initializer_list(Parser *p, ParseError *err);
AstNode *parse_type(Parser *p, ParseError *err);
AstNode *parse_type_atom(Parser *p, ParseError *err);
InternResult *get_base_type(Parser *p, ParseError *err);
AstNode *parse_function_type(Parser *p, ParseError *err);

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
AstNode *parse_assignment(Parser *p, AstNode *lhs, ParseError *err);

int parse_parameter_list(Parser *p, AstNode *func_decl, ParseError *err);
int parse_argument_list(Parser *p, AstNode* call, ParseError *err);

AstNode *parse_if_statement(Parser *p, ParseError *err);
AstNode *parse_while_statement(Parser *p, ParseError *err);
AstNode *parse_for_statement(Parser *p, ParseError *err);
AstNode *parse_return_statement(Parser *p, ParseError *err);
AstNode *parse_break_statement(Parser *p, ParseError *err);
AstNode *parse_continue_statement(Parser *p, ParseError *err);
AstNode *parse_expression_statement(Parser *p, ParseError *err);