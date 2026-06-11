#pragma once

#include "type.h"
#include "token.h" 
#include "ast.h"   

typedef enum {
    TE_NONE = 0,
    
    // Simple errors
    TE_UNKNOWN_TYPE,       
    TE_REDECLARATION,      
    TE_UNDECLARED,         
    TE_INCOMPLETE_TYPE,    // New: Variable declared with incomplete type (e.g. missing array size)
    
    // Type mismatch errors
    TE_TYPE_MISMATCH,      
    TE_RETURN_MISMATCH,    
    TE_VARIABLE_TYPE_RESOLUTION_FAILED, 
    
    // Structure/Dimension errors (NEW)
    TE_DIMENSION_MISMATCH, // "Expected 3 dimensions, found 2"
    TE_ARRAY_SIZE_MISMATCH,// "Expected size 10, found 2"
    TE_EXPECTED_ARRAY,     // "Expected array type, found scalar"
    TE_UNEXPECTED_LIST,    // "Expected scalar type, found initializer list"
    
    // Operator errors
    TE_BINOP_MISMATCH,     
    TE_UNOP_MISMATCH,      
    
    // Structure/Array errors
    TE_NOT_CALLABLE,       
    TE_NOT_INDEXABLE,      
    TE_FIELD_ACCESS,       
    TE_NOT_MEMBER_ACCESSIBLE, // New: Type does not support member access
    TE_INDEX_OUT_OF_BOUNDS, // New: Array index out of bounds
    
    TE_CONST_ASSIGN,       
    TE_ARG_COUNT_MISMATCH,  

    TE_NOT_CONST,          
    TE_NOT_LVALUE          
} TypeErrorKind;

typedef struct {
    TypeErrorKind kind;
    Span span;             
    const char *filename;  
    
    union {
        struct { const char *name; } name;

        struct {
            Type *expected;
            Type *actual;
        } mismatch;

        // TE_DIMENSION_MISMATCH
        struct {
            int expected_ndim;
            int actual_ndim;
        } dims;

        // TE_ARRAY_SIZE_MISMATCH & TE_INDEX_OUT_OF_BOUNDS
        // For Bounds: expected_size = Limit, actual_size = Index
        struct {
            size_t expected_size;
            size_t actual_size;
        } size;

        struct {
            OpKind op;      
            Type *left;
            Type *right;
        } binop;

        struct {
            OpKind op;      
            Type *operand;
        } unop;
        
        struct { Type *actual; } bad_usage;

        struct { size_t expected; size_t actual; } arg_count;

        struct {
            const char *name;
            Type *type;
        } field;

    } as;
} TypeError;

void print_type_error(TypeError *err);