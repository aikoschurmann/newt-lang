#include "parse_expressions.h"
#include "parse_types.h"
#include "parser.h"
#include "dynamic_array.h"
#include "lexer.h"
#include "ast.h"
#include "core/error.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

static inline int detect_base(const char *s, size_t len, size_t *start) {
    if (len > 2 && s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') {
            *start = 2;
            return 16;
        } else if (s[1] == 'b' || s[1] == 'B') {
            *start = 2;
            return 2;
        }
    }
    *start = 0;
    return 10;
}

static inline int char_to_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool parse_int_lit(const char *s, size_t len, unsigned long long *out) {
    if (len == 0) return false;
    
    size_t start = 0;
    int base = detect_base(s, len, &start);

    uint64_t val = 0;
    bool any_digits = false;

    // Pre-calculate strict bounds for the specific base
    uint64_t max_val = UINT64_MAX / base;
    uint64_t rem = UINT64_MAX % base;

    for (size_t i = start; i < len; i++) {
        if (s[i] == '_') continue; // Support underscores

        int d = char_to_digit(s[i]);
        if (d == -1 || d >= base) return false; // invalid char

        // Strict, cross-platform 64-bit overflow check
        if (val > max_val || (val == max_val && (uint64_t)d > rem)) {
            return false; 
        }

        val = val * (uint64_t)base + (uint64_t)d;
        any_digits = true;
    }

    if (!any_digits) return false;

    *out = val;
    return true;
}

static inline bool parse_float_lit(const char *s, size_t len, double *out) {
    double val = 0.0;
    size_t i = 0;

    // integer part
    for (; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
        val = val * 10.0 + (s[i] - '0');
    }

    // fractional part
    if (i < len && s[i] == '.') {
        i++;
        double frac = 0.0, base = 0.1;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            frac += (s[i] - '0') * base;
            base *= 0.1;
            i++;
        }
        val += frac;
    }

    // exponent part
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        int exp_sign = 1;
        if (i < len && (s[i] == '+' || s[i] == '-')) {
            if (s[i] == '-') exp_sign = -1;
            i++;
        }

        int exp_val = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            exp_val = exp_val * 10 + (s[i] - '0');
            i++;
        }

        val *= pow(10.0, exp_sign * exp_val);
    }

    if (i != len) return false; // invalid chars remain

    *out = val;
    return true;
}

/* operand_parser: parse one operand/sublevel and return AST node (or NULL on error). */
typedef AstNode *(*operand_parser_fn)(Parser *p, ParseError *err);
typedef OpKind (*map_token_to_op_fn)(Token *tok);

/* Generic left-associative binary parser */
static AstNode *parse_left_assoc_binary(Parser *p, ParseError *err,
                        operand_parser_fn parse_operand,
                        map_token_to_op_fn map_op,
                        const char *oom_msg)
{
    AstNode *lhs = parse_operand(p, err);
    if (!lhs) return NULL;

    Token *token = current_token(p);
    int op;
    while (token && (op = (int)map_op(token)) != OP_NULL) {
        /* copy operator token for span/error reporting */
        Token op_token = *token;
        consume(p, token->type);

        AstNode *rhs = parse_operand(p, err);
        if (!rhs) return NULL;

        AstNode *bin = new_node_or_err(p, AST_BINARY_EXPR, err, oom_msg);
        if (!bin) {
            if (err) create_parse_error(err, p, oom_msg, &op_token);
            return NULL;
        }

        bin->data.binary_expr.left  = lhs;
        bin->data.binary_expr.right = rhs;
        bin->data.binary_expr.op    = (OpKind)op;
        bin->span = span_join(&lhs->span, &rhs->span);

        lhs = bin;
        token = current_token(p);
    }

    return lhs;
}

AstNode *parse_expression(Parser *p, ParseError *err) {
    AstNode *lhs = parse_logical_or(p, err);
    if (!lhs) return NULL;

    Token *token = current_token(p);
    if (token && is_assignment_op(token->type)) {
        AstNode *assignment = parse_assignment(p, lhs, err);
        if (!assignment) return NULL;
        return assignment;
    }
    return lhs;
}

static OpKind map_assignment_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_ASSIGN: return OP_ASSIGN;
        case TOK_PLUS_EQ: return OP_PLUS_EQ;
        case TOK_MINUS_EQ: return OP_MINUS_EQ;
        case TOK_STAR_EQ: return OP_MUL_EQ;
        case TOK_SLASH_EQ: return OP_DIV_EQ;
        case TOK_PERCENT_EQ: return OP_MOD_EQ;
        default: return OP_NULL;
    }
}

AstNode* parse_assignment(Parser *p, AstNode *lhs, ParseError *err) {
    if (!p || !lhs) return NULL;

    if (!is_lvalue_node(lhs)) {
        if (err) create_parse_error(err, p, "left-hand side of assignment must be an lvalue", NULL);
        return NULL;
    }

    Token *op_tok = current_token(p);
    if (!op_tok || !is_assignment_op(op_tok->type)) {
        if (err) create_parse_error(err, p, "expected assignment operator", current_token(p));
        return NULL;
    }
    consume(p, op_tok->type);

    AstNode *rhs = parse_expression(p, err);
    if (!rhs) return NULL;

    AstNode *assign = new_node_or_err(p, AST_ASSIGNMENT_EXPR, err, "out of memory creating assignment node");
    if (!assign) return NULL;

    assign->data.assignment_expr.lvalue = lhs;
    assign->data.assignment_expr.rvalue = rhs;
    assign->data.assignment_expr.op = map_assignment_op(op_tok);
    assign->span = span_join(&lhs->span, &rhs->span);
    return assign;
}

static OpKind map_logical_or_op(Token *tok) {
    if (!tok) return OP_NULL;
    if (tok->type == TOK_OR_OR) return OP_OR;
    return OP_NULL;
}

AstNode *parse_logical_or(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_logical_and, map_logical_or_op, "out of memory creating logical-or node");
}

static OpKind map_logical_and_op(Token *tok) {
    if (!tok) return OP_NULL;
    if (tok->type == TOK_AND_AND) return OP_AND;
    return OP_NULL;
}
AstNode *parse_logical_and(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_equality, map_logical_and_op, "out of memory creating logical-and node");
}

static OpKind map_equality_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_EQ_EQ: return OP_EQ;
        case TOK_BANG_EQ: return OP_NEQ;
        default: return OP_NULL;
    }
}
AstNode *parse_equality(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_relational, map_equality_op, "out of memory creating equality node");
}

static OpKind map_relational_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_LT: return OP_LT;
        case TOK_GT: return OP_GT;
        case TOK_LT_EQ: return OP_LE;
        case TOK_GT_EQ: return OP_GE;
        default: return OP_NULL;
    }
}
AstNode *parse_relational(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_additive, map_relational_op, "out of memory creating relational node");
}

static OpKind map_additive_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        default: return OP_NULL;
    }
}
AstNode *parse_additive(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_multiplicative, map_additive_op, "out of memory creating additive node");
}

static OpKind map_multiplicative_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_STAR: return OP_MUL;
        case TOK_SLASH: return OP_DIV;
        case TOK_PERCENT: return OP_MOD;
        default: return OP_NULL;
    }
}
AstNode *parse_multiplicative(Parser *p, ParseError *err) {
    return parse_left_assoc_binary(p, err, parse_cast, map_multiplicative_op, "out of memory creating multiplicative node");
}

AstNode *parse_cast(Parser *p, ParseError *err) {
    AstNode *expr = parse_unary(p, err);
    if (!expr) return NULL;

    while (current_token(p) && current_token(p)->type == TOK_AS) {
        consume(p, TOK_AS);
        AstNode *target_type_node = parse_type(p, err);
        if (!target_type_node) return NULL;

        AstNode *cast = new_node_or_err(p, AST_CAST, err, "out of memory creating cast node");
        if (!cast) return NULL;

        cast->data.cast_expr.expr = expr;
        cast->data.cast_expr.target_type = NULL;
        cast->data.cast_expr.target_type_node = target_type_node;
        cast->span = span_join(&expr->span, &target_type_node->span);
        expr = cast;
    }
    return expr;
}

static OpKind map_unary_op(Token *tok) {
    if (!tok) return OP_NULL;
    switch (tok->type) {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        case TOK_BANG: return OP_NOT;
        case TOK_STAR: return OP_DEREF;
        case TOK_AMP: return OP_ADDRESS;
        case TOK_PLUSPLUS: return OP_PRE_INC;
        case TOK_MINUSMINUS: return OP_PRE_DEC;
        default: return OP_NULL;
    }
}

AstNode *parse_unary(Parser *p, ParseError *err) {
    Token *token = current_token(p);
    if (token && (token->type == TOK_PLUS || token->type == TOK_MINUS || token->type == TOK_BANG || token->type == TOK_STAR || token->type == TOK_AMP || token->type == TOK_PLUSPLUS || token->type == TOK_MINUSMINUS)) {
        Token *op_token = consume(p, token->type);
        if (!op_token) { if (err) create_parse_error(err, p, "failed to consume prefix operator", token); return NULL; }
        AstNode *operand = parse_unary(p, err);
        if (!operand) return NULL;

        AstNode *unary = new_node_or_err(p, AST_UNARY_EXPR, err, "out of memory creating unary node");
        if (!unary) return NULL;

        unary->data.unary_expr.expr = operand;
        unary->data.unary_expr.op = map_unary_op(op_token);
        unary->span = span_join(&op_token->span, &operand->span);
        return unary;
    }
    return parse_postfix(p, err);
}

AstNode *parse_postfix(Parser *p, ParseError *err) {
    AstNode *primary = parse_primary(p, err);
    if (!primary) return NULL;

    Token *token = current_token(p);
    while (token && (token->type == TOK_PLUSPLUS || token->type == TOK_MINUSMINUS || token->type == TOK_LBRACKET || token->type == TOK_LPAREN || token->type == TOK_DOT || token->type == TOK_LBRACE || token->type == TOK_LT)) {
        if (token->type == TOK_PLUSPLUS || token->type == TOK_MINUSMINUS) {
            Token *op_tok = token;
            AstNode *postfix = new_node_or_err(p, AST_UNARY_EXPR, err, "out of memory creating postfix node");
            if (!postfix) return NULL;

            postfix->data.unary_expr.expr = primary;
            postfix->data.unary_expr.op = (op_tok->type == TOK_PLUSPLUS) ? OP_POST_INC : OP_POST_DEC;
            postfix->span = span_join(&primary->span, &op_tok->span);

            consume(p, op_tok->type);
            primary = postfix;

        } else if (token->type == TOK_LT) {
            size_t checkpoint = p->current;
            ParseError ignored_err = {0};
            consume(p, TOK_LT);

            DynArray *type_args = alloc_dynarray(p, NULL, sizeof(AstNode*), 2, "out of memory");
            bool success = true;

            // Try to parse at least one type
            AstNode *first_arg = parse_type(p, &ignored_err);
            if (!first_arg) {
                success = false;
            } else {
                dynarray_push_value(type_args, &first_arg);
                while (parser_match(p, TOK_COMMA)) {
                    AstNode *next_arg = parse_type(p, &ignored_err);
                    if (!next_arg) { success = false; break; }
                    dynarray_push_value(type_args, &next_arg);
                }
            }

            Token *rgt = current_token(p);
            if (success && rgt && rgt->type == TOK_GT) {
                consume(p, TOK_GT);
                // Potential generic instantiation. 
                // Heuristic: Must be followed by (, {, or . (for methods)
                Token *after = current_token(p);
                if (after && (after->type == TOK_LPAREN || after->type == TOK_LBRACE || after->type == TOK_DOT)) {
                    AstNode *inst = new_node_or_err(p, AST_GENERIC_INST_EXPR, err, "out of memory");
                    if (!inst) return NULL;
                    inst->data.generic_inst_expr.base = primary;
                    inst->data.generic_inst_expr.type_args = type_args;
                    inst->span = span_join(&primary->span, &rgt->span);
                    primary = inst;
                } else {
                    // It was a comparison like `a < b > c`
                    p->current = checkpoint;
                    break; 
                }
            } else {
                // Not a valid type list or no closing >
                p->current = checkpoint;
                break; // Let binary expression parser handle it
            }

        } else if (token->type == TOK_LBRACKET) {
            consume(p, TOK_LBRACKET);
            AstNode *index = parse_expression(p, err);
            if (!index) return NULL;
            Token *rbr = consume(p, TOK_RBRACKET);
            if (!rbr) { if (err) create_parse_error(err, p, "expected ']' after array index expression", token); return NULL; }

            AstNode *array_access = new_node_or_err(p, AST_SUBSCRIPT_EXPR, err, "out of memory creating subscript node");
            if (!array_access) return NULL;

            array_access->data.subscript_expr.target = primary;
            array_access->data.subscript_expr.index = index;
            array_access->span = span_join(&primary->span, &rbr->span);
            primary = array_access;

        } else if (token->type == TOK_LPAREN) {
            consume(p, TOK_LPAREN);

            AstNode *func_call = new_node_or_err(p, AST_CALL_EXPR, err, "out of memory creating function call node");
            if (!func_call) return NULL;

            func_call->data.call_expr.args = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory creating function call args array");
            if (!func_call->data.call_expr.args) return NULL;

            if (!parse_argument_list(p, func_call, err)) return NULL;

            Token *rparen = consume(p, TOK_RPAREN);
            if (!rparen) { if (err) create_parse_error(err, p, "expected ')' after function call arguments", token); return NULL; }

            func_call->data.call_expr.callee = primary;
            func_call->span = span_join(&primary->span, &rparen->span);
            primary = func_call;
        } else if (token->type == TOK_DOT) {
            consume(p, TOK_DOT);
            Token *name_tok = consume(p, TOK_IDENTIFIER);
            if (!name_tok) {
                if (err) create_parse_error(err, p, "expected identifier after '.'", current_token(p));
                return NULL;
            }

            AstNode *member_access = new_node_or_err(p, AST_MEMBER_EXPR, err, "out of memory creating member access node");
            if (!member_access) return NULL;

            member_access->data.member_expr.target = primary;
            member_access->data.member_expr.member = name_tok->record;
            member_access->span = span_join(&primary->span, &name_tok->span);
            primary = member_access;
        } else if (token->type == TOK_LBRACE) {
            // Struct literal: Name { field: expr, ... }
            if (primary->node_type != AST_MEMBER_EXPR) {
                break;
            }

            Token *peek_2 = peek(p, 1);
            Token *peek_3 = peek(p, 2);
            bool is_struct_lit = false;
            
            if (peek_2 && peek_2->type == TOK_RBRACE) {
                is_struct_lit = true; // Name {}
            } else if (peek_2 && peek_2->type == TOK_IDENTIFIER && peek_3 && peek_3->type == TOK_COLON) {
                is_struct_lit = true; // Name { field: ... }
            }

            if (!is_struct_lit) break; // Not a struct literal

            AstNode *struct_lit = new_node_or_err(p, AST_STRUCT_LITERAL, err, "out of memory creating struct literal node");
            if (!struct_lit) return NULL;
            struct_lit->data.struct_literal.type_node = primary;
            
            consume(p, TOK_LBRACE);     /* consume '{' */
            
            struct_lit->data.struct_literal.fields = arena_alloc(p->arena, sizeof(DynArray));
            if (!struct_lit->data.struct_literal.fields) {
                if (err) create_parse_error(err, p, "out of memory allocating struct literal fields", current_token(p));
                return NULL;
            }
            dynarray_init_in_arena(struct_lit->data.struct_literal.fields, p->arena, sizeof(AstFieldInit), 8);
            
            Token *current = current_token(p);
            while (current && current->type != TOK_RBRACE && current->type != TOK_EOF) {
                AstFieldInit init = {0};
                Token *field_name = consume(p, TOK_IDENTIFIER);
                if (!field_name) {
                    if (err) create_parse_error(err, p, "expected field name in struct literal", current_token(p));
                    return NULL;
                }
                init.name = field_name->record;
                
                if (!consume(p, TOK_COLON)) {
                    if (err) create_parse_error(err, p, "expected ':' after field name", current_token(p));
                    return NULL;
                }
                
                init.expr = parse_expression(p, err);
                if (!init.expr) return NULL;
                dynarray_push_value(struct_lit->data.struct_literal.fields, &init);
                
                current = current_token(p);
                if (current && current->type == TOK_COMMA) {
                    consume(p, TOK_COMMA);
                    current = current_token(p);
                } else {
                    break;
                }
            }
            
            Token *rbrace = consume(p, TOK_RBRACE);
            if (!rbrace) {
                if (err) create_parse_error(err, p, "expected '}' or ',' in struct literal", current_token(p));
                return NULL;
            }
            
            struct_lit->span = span_join(&primary->span, &rbrace->span);
            primary = struct_lit;
        }

        token = current_token(p);
    }

    return primary;
}

static LiteralType get_literal_type(TokenKind type) {
    switch (type) {
        case TOK_INT_LIT:    return INT_LITERAL;
        case TOK_FLOAT_LIT:  return FLOAT_LITERAL;
        case TOK_TRUE:
        case TOK_FALSE:      return BOOL_LITERAL;
        case TOK_STRING_LIT: return STRING_LITERAL;
        case TOK_CHAR_LIT:   return CHAR_LITERAL;
        case TOK_NULL:       return NULL_LITERAL;
        default: ICE("Unexpected token type %d in get_literal_type", (int)type);
    }
}

int parse_argument_list(Parser *p, AstNode* call, ParseError *err) {
    Token *tok = current_token(p);
    if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in argument list", NULL); return 0; }
    if (tok->type == TOK_RPAREN) return 1; /* empty arglist */

    while (1) {
        tok = current_token(p);
        if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in argument list", NULL); return 0; }

        AstNode *argument = NULL;
        if (tok->type == TOK_LBRACE) {
            argument = parse_initializer_list(p, err);
            if (!argument) return 0;
        } else {
            argument = parse_expression(p, err);
            if (!argument) return 0;
        }

        if (dynarray_push_value(call->data.call_expr.args, &argument) != 0) { if (err) create_parse_error(err, p, "out of memory adding argument to call", NULL); return 0; }

        tok = current_token(p);
        if (!tok) { if (err) create_parse_error(err, p, "unexpected end of input in argument list", NULL); return 0; }
        if (tok->type == TOK_RPAREN) break;
        
        if (!consume(p, TOK_COMMA)) { if (err) create_parse_error(err, p, "expected a ',' or ')'", tok); return 0; }
        
        // Support trailing comma
        tok = current_token(p);
        if (tok && tok->type == TOK_RPAREN) break;
    }

    return 1;
}

AstNode *parse_primary(Parser *p, ParseError *err) {
    Token *token = current_token(p);
    if (!token) { if (err) create_parse_error(err, p, "unexpected end of input, expected primary expression", NULL); return NULL; }

    switch (token->type) {
        
        case TOK_AT: {
            consume(p, TOK_AT);
            Token *name_tok = consume(p, TOK_IDENTIFIER);
            if (!name_tok) {
                if (err) create_parse_error(err, p, "expected intrinsic name after '@'", current_token(p));
                return NULL;
            }
            
            AstNode *intrinsic = new_node_or_err(p, AST_INTRINSIC, err, "out of memory creating intrinsic node");
            if (!intrinsic) return NULL;
            
            if (name_tok->slice.len == 5 && memcmp(name_tok->slice.ptr, "alloc", 5) == 0) {
                intrinsic->data.intrinsic.kind = INTRINSIC_ALLOC;
            } else if (name_tok->slice.len == 4 && memcmp(name_tok->slice.ptr, "free", 4) == 0) {
                intrinsic->data.intrinsic.kind = INTRINSIC_FREE;
            } else {
                if (err) create_parse_error(err, p, "unknown compiler intrinsic", name_tok);
                return NULL;
            }
            
            intrinsic->data.intrinsic.args = alloc_dynarray(p, err, sizeof(AstNode*), 4, "out of memory");
            if (!consume(p, TOK_LPAREN)) {
                if (err) create_parse_error(err, p, "expected '(' after intrinsic name", current_token(p));
                return NULL;
            }
            
            if (current_token(p)->type != TOK_RPAREN) {
                while (1) {
                    // Peek to see if this argument is a type
                    Token *peek_tok = current_token(p);
                    AstNode *arg = NULL;
                    
                    if (peek_tok && (peek_tok->type >= TOK_I8 && peek_tok->type <= TOK_VOID)) {
                         arg = parse_type(p, err);
                    } else {
                         arg = parse_expression(p, err);
                    }
                    
                    if (!arg) return NULL;
                    dynarray_push_value(intrinsic->data.intrinsic.args, &arg);
                    
                    if (current_token(p)->type == TOK_RPAREN) break;
                    if (!consume(p, TOK_COMMA)) {
                        if (err) create_parse_error(err, p, "expected ',' or ')'", current_token(p));
                        return NULL;
                    }
                }
            }
            
            Token *rparen = consume(p, TOK_RPAREN);
            intrinsic->span = span_join(&name_tok->span, &rparen->span);
            return intrinsic;
        }

        case TOK_INT_LIT: case TOK_FLOAT_LIT: case TOK_TRUE: case TOK_FALSE: case TOK_CHAR_LIT: case TOK_STRING_LIT: case TOK_NULL: {
            AstNode *literal = new_node_or_err(p, AST_LITERAL, err, "out of memory creating literal node");
            if (!literal) return NULL;

            literal->data.literal.type = get_literal_type(token->type);

            switch (token->type) {
                case TOK_INT_LIT: {
                    unsigned long long v;
                    if (parse_int_lit(token->slice.ptr, token->slice.len, &v)) {
                        literal->data.literal.value.int_val = (long long)v;
                    } else {
                        create_parse_error(err, p, "invalid integer literal or overflow", token);
                        return NULL;
                    }
                    break;
                }
                case TOK_FLOAT_LIT: {
                    double v;
                    if (parse_float_lit(token->slice.ptr, token->slice.len, &v)) {
                        literal->data.literal.value.float_val = v;
                    } else {
                        create_parse_error(err, p, "invalid float literal or overflow", token);
                        return NULL;
                    }
                    break;
                }
                case TOK_TRUE:
                    literal->data.literal.value.bool_val = 1;
                    break;
                case TOK_FALSE:
                    literal->data.literal.value.bool_val = 0;
                    break;
                case TOK_CHAR_LIT:
                    literal->data.literal.value.char_val = (char)(uintptr_t)token->record;
                    break;
                case TOK_STRING_LIT:
                    literal->data.literal.value.string_val = token->record;
                    break;
                case TOK_NULL:
                    literal->data.literal.value.int_val = 0;
                    break;
                default:
                    literal->data.literal.value.int_val = 0;
                    break;
            }

            literal->is_foldable_const = 0;
            literal->is_llvm_const_safe = 0;
            literal->span = token->span;
            consume(p, token->type);
            return literal;
        }

        case TOK_IDENTIFIER: {
            Token *peek_tok = peek(p, 1);
            if (peek_tok && peek_tok->type == TOK_LBRACE) {
                Token *peek_2 = peek(p, 2);
                Token *peek_3 = peek(p, 3);
                bool is_struct_lit = false;

                if (peek_2 && peek_2->type == TOK_RBRACE) {
                    is_struct_lit = true; // Name {}
                } else if (peek_2 && peek_2->type == TOK_IDENTIFIER && peek_3 && peek_3->type == TOK_COLON) {
                    is_struct_lit = true; // Name { field: ... }
                }

                if (is_struct_lit) {
                    AstNode *struct_lit = new_node_or_err(p, AST_STRUCT_LITERAL, err, "out of memory creating struct literal node");
                    if (!struct_lit) return NULL;
                    
                    AstNode *type_ident = new_node_or_err(p, AST_IDENTIFIER, err, "out of memory");
                    if (!type_ident) return NULL;
                    type_ident->data.identifier.intern_result = token->record;
                    type_ident->span = token->span;
                    struct_lit->data.struct_literal.type_node = type_ident;
                    
                    Span start_span = token->span;
                    consume(p, TOK_IDENTIFIER); /* consume name */
                    consume(p, TOK_LBRACE);     /* consume '{' */

                    struct_lit->data.struct_literal.fields = arena_alloc(p->arena, sizeof(DynArray));
                    if (!struct_lit->data.struct_literal.fields) {
                        if (err) create_parse_error(err, p, "out of memory allocating struct literal fields", current_token(p));
                        return NULL;
                    }
                    dynarray_init_in_arena(struct_lit->data.struct_literal.fields, p->arena, sizeof(AstFieldInit), 8);

                    Token *current = current_token(p);
                    while (current && current->type != TOK_RBRACE && current->type != TOK_EOF) {
                        AstFieldInit init = {0};
                        Token *field_name = consume(p, TOK_IDENTIFIER);
                        if (!field_name) {
                            if (err) create_parse_error(err, p, "expected field name in struct literal", current_token(p));
                            return NULL;
                        }
                        init.name = field_name->record;

                        if (!consume(p, TOK_COLON)) {
                            if (err) create_parse_error(err, p, "expected ':' after field name", current_token(p));
                            return NULL;
                        }

                        init.expr = parse_expression(p, err);
                        if (!init.expr) return NULL;

                        dynarray_push_value(struct_lit->data.struct_literal.fields, &init);

                        current = current_token(p);
                        if (current && current->type == TOK_COMMA) {
                            consume(p, TOK_COMMA);
                            current = current_token(p);
                        } else {
                            break;
                        }
                    }

                    Token *rbrace = consume(p, TOK_RBRACE);
                    if (!rbrace) {
                        if (err) create_parse_error(err, p, "expected '}' or ',' in struct literal", current_token(p));
                        return NULL;
                    }

                    struct_lit->span = span_join(&start_span, &rbrace->span);
                    return struct_lit;
                }
            }

            AstNode *identifier = new_node_or_err(p, AST_IDENTIFIER, err, "out of memory creating identifier node");
            if (!identifier) return NULL;
            identifier->data.identifier.intern_result = token->record;
            identifier->span = token->span;
            consume(p, TOK_IDENTIFIER);
            return identifier;
        }

        case TOK_LPAREN: {
            Token *lpar = consume(p, TOK_LPAREN);
            AstNode *expr = parse_expression(p, err);
            if (!expr) return NULL;
            Token *r = consume(p, TOK_RPAREN);
            if (!r) { if (err) err->use_prev_token = true; create_parse_error(err, p, "expected ')' after expression", current_token(p)); return NULL; }
            expr->span = span_join(&lpar->span, &r->span);
            return expr;
        }

        case TOK_LBRACE:
            return parse_initializer_list(p, err);

        default:
            if (err) {err->use_prev_token = true; create_parse_error(err, p, "expected primary expression (literal, identifier, or parenthesized expression)", current_token(p));}
            return NULL;
    }
}

AstNode *parse_initializer_list(Parser *p, ParseError *err) {
    if (!p) return NULL;

    Token *start_tok = consume(p, TOK_LBRACE);
    if (!start_tok) {
        if (err) create_parse_error(err, p, "expected '{' to start initializer list", current_token(p));
        return NULL;
    }
    Span start_span = start_tok->span;

    AstNode *init = ast_create_node(AST_INITIALIZER_LIST, p->arena, p->filename);
    if (!init) {
        if (err) create_parse_error(err, p, "out of memory creating initializer node", NULL);
        return NULL;
    }
    init->data.initializer_list.elements = arena_alloc(p->arena, sizeof(DynArray));
    if (!init->data.initializer_list.elements) {
        if (err) create_parse_error(err, p, "out of memory creating initializer elements", NULL);
        return NULL;
    }
    dynarray_init_in_arena(init->data.initializer_list.elements, p->arena, sizeof(AstNode*), 4);

    Token *tok = current_token(p);
    if (!tok) {
        create_parse_error(err, p, "unexpected end of input in initializer list", NULL);
        return NULL;
    }

    if (tok->type == TOK_RBRACE) {
        Token *rbrace = consume(p, TOK_RBRACE);
        init->span = span_join(&start_span, &rbrace->span);
        return init;
    }

    while (1) {
        tok = current_token(p);
        if (!tok) {
            create_parse_error(err, p, "unexpected end of input in initializer list", NULL);
            return NULL;
        }

        AstNode *element = NULL;
        if (tok->type == TOK_LBRACE) {
            element = parse_initializer_list(p, err);
            if (!element) return NULL;
        } else {
            element = parse_expression(p, err);
            if (!element) return NULL;
        }

        if (dynarray_push_value(init->data.initializer_list.elements, &element) != 0) {
            create_parse_error(err, p, "out of memory adding element to initializer list", NULL);
            return NULL;
        }

        tok = current_token(p);
        if (!tok) {
            create_parse_error(err, p, "unexpected end of input in initializer list", NULL);
            return NULL;
        }

        if (tok->type == TOK_COMMA) {
            consume(p, TOK_COMMA);
            tok = current_token(p);
            if (tok && tok->type == TOK_RBRACE) {
                Token *rbrace = consume(p, TOK_RBRACE);
                init->span = span_join(&start_span, &rbrace->span);
                return init;
            }
            continue;
        } else if (tok->type == TOK_RBRACE) {
            Token *rbrace = consume(p, TOK_RBRACE);
            init->span = span_join(&start_span, &rbrace->span);
            return init;
        } else {
            err->use_prev_token = true;
            create_parse_error(err, p, "expected ',' or '}' in initializer list", tok);
            return NULL;
        }
    }

    return init;
}
