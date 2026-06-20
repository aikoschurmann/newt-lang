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
    TE_VOID_PARAMETER,
    TE_VOID_VARIABLE,
    TE_NOT_GENERIC,
    TE_INVALID_ALLOCATOR,
    TE_INCOMPLETE_TYPE,    // Variable declared with incomplete type (e.g. missing array size)
    TE_EXPECTED_TYPE_ARG,  // Expected a type argument (e.g., for @alloc)
    
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
    TE_NOT_LVALUE,
    TE_RECURSIVE_CONST,
    TE_AMBIGUOUS_OVERLOAD,
    TE_NO_MATCHING_OVERLOAD,
    TE_MISSING_TYPE_ARGS,
    TE_GENERIC_ARG_MISMATCH,
    TE_ALLOCATOR_SHAPE_INVALID,
    TE_INSTANTIATION_DEPTH
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
        
        struct {
            const char *name;
            size_t expected;
            size_t provided;
        } generic_mismatch;
        
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