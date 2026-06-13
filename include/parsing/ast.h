#pragma once

#include <stddef.h>
#include "token.h"
#include "dynamic_array.h"
#include "utils.h"
#include "dense_arena_interner.h"
#include "arena.h"
#include "sema/intrinsics.h"

/* ----------------------- Forward Declarations ----------------------- */
typedef struct AstNode AstNode;
typedef struct Type Type; /* Moved up to fix "unknown type name" error */

/* ----------------------- AST node kinds ----------------------- */

typedef enum {
    AST_PROGRAM,

    /* declarations */
    AST_VARIABLE_DECLARATION,
    AST_FUNCTION_DECLARATION,
    AST_PARAM,
    AST_STRUCT_DECLARATION,
    AST_IMPORT_DECLARATION,
    AST_INTRINSIC,

    /* statements */
    AST_BLOCK,
    AST_IF_STATEMENT,
    AST_WHILE_STATEMENT,
    AST_FOR_STATEMENT,
    AST_RETURN_STATEMENT,
    AST_BREAK_STATEMENT,
    AST_CONTINUE_STATEMENT,
    AST_EXPR_STATEMENT,

    /* expressions */
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_POSTFIX_EXPR,
    AST_ASSIGNMENT_EXPR,
    AST_CALL_EXPR,
    AST_SUBSCRIPT_EXPR,
    AST_MEMBER_EXPR,
    AST_STRUCT_LITERAL,
    
    AST_CAST, /* Explicit cast node (inserted by semantic analysis) */

    /* types */
    AST_TYPE,

    AST_INITIALIZER_LIST

} AstNodeType;

typedef enum {
    OP_NULL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR, OP_NOT,
    OP_ASSIGN, OP_PLUS_EQ, OP_MINUS_EQ, OP_MUL_EQ, OP_DIV_EQ, OP_MOD_EQ,
    OP_DEREF, OP_ADRESS,
    OP_POST_INC, OP_POST_DEC, OP_PRE_INC, OP_PRE_DEC
} OpKind;

typedef enum {
    INT_LITERAL,
    FLOAT_LITERAL,
    BOOL_LITERAL,
    STRING_LITERAL,
    CHAR_LITERAL,
    NULL_LITERAL,
    LIT_UNKNOWN /* used for error handling, not a real literal type */
} LiteralType;

typedef struct {
    LiteralType type;
    union {
        long long       int_val;
        double          float_val;
        int             bool_val;   /* 0 or 1 */
        char            char_val;
        InternResult   *string_val; /* interned string */
    } value;
} ConstValue;


/* ----------------------- AST payload structs ----------------------- */

typedef struct {
    DynArray *decls; /* contains AstNode* (declarations) */
} AstProgram;

typedef struct {
    AstNode *type;           /* type node */
    InternResult *intern_result;  /* interned record for the variable name */
    int is_const;            /* boolean: 0 or 1 */
    int is_pub;              /* visibility */
    AstNode *initializer;    /* optional */
} AstVariableDeclaration;

typedef struct {
    AstNode *return_type;    /* AstNode of AST_TYPE */
    InternResult *intern_result;  /* interned record for the function name */
    DynArray *params;        /* AstParam nodes */
    AstNode *body;           /* AstBlock, may be NULL for @link */
    InternResult *link_name; /* Optional: for @link("name") */
    int is_pub;              /* visibility */
} AstFunctionDeclaration;

typedef struct {
    int name_idx;            /* interned dense index for the parameter name; -1 for anonymous */
    AstNode *type;           /* AST_TYPE node */
} AstParam;

typedef struct {
    InternResult *name; // The field name
    AstNode *type;      // AST_TYPE node
} AstFieldDecl;

typedef struct {
    InternResult *module_name;   /* interned module path, e.g. "std.memory" */
} AstImportDeclaration;

typedef struct {
    InternResult *intern_result; // Struct name
    DynArray *fields;            // Contains AstFieldDecl*
    int is_pub;                  // visibility
} AstStructDeclaration;

typedef struct {
    DynArray *statements; /* contains AstNode* */
} AstBlock;

typedef struct {
    AstNode *condition;
    AstNode *then_branch;
    AstNode *else_branch; /* may be NULL */
} AstIfStatement;

typedef struct {
    AstNode *condition;
    AstNode *body;
} AstWhileStatement;

typedef struct {
    AstNode *init;       /* may be NULL */
    AstNode *condition;  /* may be NULL */
    AstNode *post;       /* may be NULL */
    AstNode *body;
} AstForStatement;

typedef struct {
    AstNode *expression;
} AstReturnStatement;

typedef struct { } AstBreakStatement;
typedef struct { } AstContinueStatement;

typedef struct {
    AstNode *expression;
} AstExprStatement;

/* small expression structs */
typedef ConstValue AstLiteral;
typedef struct { InternResult *intern_result; } AstIdentifier; /* interned name index */
typedef struct { AstNode *left; AstNode *right; OpKind op; } AstBinaryExpr;
typedef struct { OpKind op; AstNode *expr; } AstUnaryExpr;
typedef struct { AstNode *expr; OpKind op; } AstPostfixExpr;
typedef struct { AstNode *lvalue; AstNode *rvalue; OpKind op; } AstAssignmentExpr;
typedef struct { AstNode *callee; DynArray *args; } AstCallExpr;
typedef struct { AstNode *target; AstNode *index; } AstSubscriptExpr;
typedef struct { AstNode *target; InternResult *member; int field_index; } AstMemberExpr;

typedef struct {
    InternResult *name; // The field name
    AstNode *expr;      // Field initialization expression
    int field_index;    // Resolved at Sema, used by Codegen
} AstFieldInit;

typedef struct {
    InternResult *intern_result; // Struct type name
    DynArray *fields;            // Contains AstFieldInit*
} AstStructLiteral;

/* New Cast Struct */
typedef struct {
    AstNode *expr;
    Type *target_type;
    AstNode *target_type_node;
} AstCastExpr;


typedef enum {
    AST_TYPE_PRIMITIVE,   // primitive type: i32, foo, etc. (holds interned id)
    AST_TYPE_PTR,    // pointer to inner type
    AST_TYPE_ARRAY,  // array of inner type with optional size expression
    AST_TYPE_FUNC    // function type: params list + return type
} AstTypeKind;

typedef struct AstType {
    AstTypeKind kind;
    Span span;               /* source range for this type expression */

    union {
        /* AST_TYPE_NAME */
        struct { InternResult *intern_result; } base;  // record from interner for the type name

        /* AST_TYPE_PTR */
        struct { AstNode *target; } ptr;

        /* AST_TYPE_ARRAY */
        struct { AstNode *elem; AstNode *size_expr; /* may be NULL for [] */ } array;

        /* AST_TYPE_FUNC */
        struct { DynArray *param_types; AstNode *return_type; } func;
    } u;
} AstType;


typedef struct {
    DynArray *elements; /* contains AstNode* */
} AstInitializeList;


/* ----------------------- AstNode ----------------------- */
struct AstNode {
    AstNodeType node_type;
    Span span;
    const char *filename; // Originating module
    Type *type;  // semantic type information

    /* constant folding / evaluation helper: inline value to avoid small allocations */
    int is_const_expr;       /* boolean: 0 or 1 */
    ConstValue const_value;  /* can still be used for non-const expressions, constant folding will set this */

    union {
        AstProgram program;
        AstVariableDeclaration variable_declaration;
        AstFunctionDeclaration function_declaration;
        AstParam param;
        AstStructDeclaration struct_declaration;
        AstImportDeclaration import_declaration;
        struct {
            IntrinsicKind kind;
            DynArray *args; // Array of AstNode*
        } intrinsic;
        AstBlock block;
        AstIfStatement if_statement;
        AstWhileStatement while_statement;
        AstForStatement for_statement;
        AstReturnStatement return_statement;
        AstBreakStatement break_statement;
        AstContinueStatement continue_statement;
        AstExprStatement expr_statement;

        AstLiteral literal;
        AstIdentifier identifier;
        AstBinaryExpr binary_expr;
        AstUnaryExpr unary_expr;
        AstPostfixExpr postfix_expr;
        AstAssignmentExpr assignment_expr;
        AstCallExpr call_expr;
        AstSubscriptExpr subscript_expr;
        AstMemberExpr member_expr;
        AstStructLiteral struct_literal;
        AstCastExpr cast_expr;

        AstType ast_type;
        AstInitializeList initializer_list;
    } data;
};


/* ----------------------- Helpers & prototypes ----------------------- */

/* AST helper prototypes (implementations are up to you) */
AstNode *ast_create_node(AstNodeType type, Arena *arena, const char *filename);
void print_ast(AstNode *node, int depth, DenseArenaInterner *keywords, DenseArenaInterner *identifiers, DenseArenaInterner *strings);
void print_ast_with_prefix(AstNode *node, int depth, int is_last, DenseArenaInterner *keywords, DenseArenaInterner *identifiers, DenseArenaInterner *strings);
int is_lvalue_node(AstNode *node);
int is_assignment_op(TokenType type);