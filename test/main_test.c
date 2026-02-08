#include "test_harness.h"

// Declare external test functions
int test_lexer_basic();
int test_lexer_comments(); 
int test_lexer_operators(); 
// Extended Lexer
int test_lexer_float_literals();
int test_lexer_bad_strings();
int test_lexer_identifiers();
int test_lexer_empty();
int test_lexer_whitespace_only();

int test_parser_expression();
int test_parser_unary_precedence();
int test_parser_comparison_precedence();
int test_parser_paren_expression(); 
int test_parser_if_statement(); 
int test_parser_unclosed_paren();
int test_parser_missing_semicolon();
int test_parser_bad_stmt_start();
int test_parser_nested_parens();
int test_parser_extra_tokens();
int test_parser_empty_file();
int test_parser_for_statement(); 
int test_parser_while_statement();
int test_parser_arrays_and_pointers();
int test_parser_flow_control();

int test_sema_arg_mismatch_regression();
int test_sema_type_mismatch(); 
int test_sema_undeclared(); 
int test_sema_valid_program();
// Extended Sema
int test_sema_call_arg_count();
int test_sema_call_arg_type();
int test_sema_full_features(); // Full integration test
int test_sema_type_promotion();
int test_sema_array_inference_1d();
int test_sema_array_inference_mixed_types();
int test_sema_multidimensional_arrays();
int test_sema_initializer_errors();
int test_sema_const_folding();

// Exceptions
int test_exception_long_identifier();
int test_exception_unclosed_comment();
int test_exception_deep_blocks();
int test_exception_weird_chars();

int main() {
    fprintf(stderr, "=== Running Compiler Test Suite ===\n");
    
    // Lexer Tests
    run_test("Lexer: Basic", test_lexer_basic);
    run_test("Lexer: Comments", test_lexer_comments);
    run_test("Lexer: Operators", test_lexer_operators);
    run_test("Lexer: Float Literals", test_lexer_float_literals);
    run_test("Lexer: Bad Strings", test_lexer_bad_strings);
    run_test("Lexer: Identifiers", test_lexer_identifiers);
    run_test("Lexer: Empty File", test_lexer_empty);
    run_test("Lexer: Whitespace", test_lexer_whitespace_only);

    // Parser Tests
    run_test("Parser: Expression Precedence", test_parser_expression);
    run_test("Parser: Unary Precedence", test_parser_unary_precedence);
    run_test("Parser: Comparison Precedence", test_parser_comparison_precedence);
    run_test("Parser: Parentheses", test_parser_paren_expression);
    run_test("Parser: If Statement", test_parser_if_statement);
    run_test("Parser: Unclosed Paren", test_parser_unclosed_paren);
    run_test("Parser: Missing Semicolon", test_parser_missing_semicolon);
    run_test("Parser: Bad Stmt Start", test_parser_bad_stmt_start);
    run_test("Parser: Nested Parens", test_parser_nested_parens);
    run_test("Parser: Extra Tokens", test_parser_extra_tokens);
    run_test("Parser: Empty File", test_parser_empty_file);
    run_test("Parser: For Statement", test_parser_for_statement);
    run_test("Parser: While Statement", test_parser_while_statement);
    run_test("Parser: Arrays & Pointers", test_parser_arrays_and_pointers);
    run_test("Parser: Flow Control", test_parser_flow_control);

    // Sema Tests
    run_test("Sema: Arg Count Mismatch", test_sema_arg_mismatch_regression);
    run_test("Sema: Type Mismatch", test_sema_type_mismatch);
    run_test("Sema: Undeclared Variable", test_sema_undeclared);
    run_test("Sema: Valid Program", test_sema_valid_program);
    run_test("Sema: Call Arg Count", test_sema_call_arg_count);
    run_test("Sema: Call Arg Type", test_sema_call_arg_type);
    run_test("Sema: Full Features", test_sema_full_features);
    run_test("Sema: Type Promotion", test_sema_type_promotion);
    run_test("Sema: Array Inference (1D)", test_sema_array_inference_1d);
    run_test("Sema: Array Inference (Mixed)", test_sema_array_inference_mixed_types);
    run_test("Sema: 2D Arrays", test_sema_multidimensional_arrays);
    run_test("Sema: Initializer Errors", test_sema_initializer_errors);
    run_test("Sema: Const Folding", test_sema_const_folding);

    // Exceptions & Stress
    run_test("Exception: Long Identifier", test_exception_long_identifier);
    run_test("Exception: Unclosed Comment", test_exception_unclosed_comment);
    run_test("Exception: Deep Blocks", test_exception_deep_blocks);
    run_test("Exception: Weird Chars", test_exception_weird_chars);
    
    return print_test_summary();
}
