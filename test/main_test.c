#include "test_harness.h"
#include "codegen/codegen.h"

// Lexer Tests
int test_lexer_basic();
int test_lexer_comments(); 
int test_lexer_operators(); 
int test_lexer_float_literals();
int test_lexer_bad_strings();
int test_lexer_identifiers();
int test_lexer_empty();
int test_lexer_whitespace_only();
int test_lexer_as_keyword();

// Parser Tests
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
int test_parser_member_access();
int test_parser_flow_control();
int test_parser_cast();
int test_parser_void_pointer();

// Sema Tests
int test_sema_implicit_widening();
int test_sema_literal_inference();
int test_sema_strict_binary_ops();
int test_sema_as_casting();
int test_sema_pointer_rules();
int test_sema_array_slice_rules();
int test_sema_shadowing();
int test_sema_arg_mismatch_regression();
int test_sema_undeclared();
int test_sema_struct_basic();
int test_sema_const_folding();

// Codegen Tests
int test_codegen_basic_arithmetic();
int test_codegen_float_math();
int test_codegen_complex_loop();
int test_codegen_nested_control_flow();
int test_codegen_recursion_edge();
int test_codegen_multi_dim_arrays();
int test_codegen_array_len_casting();
int test_codegen_slice_decay_func();
int test_codegen_nested_structs();
int test_codegen_struct_pointers();
int test_codegen_void_ptr_roundtrip();
int test_codegen_ptr_to_int_cast();
int test_codegen_char_math();
int test_codegen_boolean_logic();
int test_codegen_huge_stack_array();
int test_codegen_shadowing_scoping();
int test_codegen_bool_cast_branch();
int test_codegen_struct_value_passing();
int test_codegen_string_indexing();

// Exceptions
int test_exception_long_identifier();
int test_exception_unclosed_comment();
int test_exception_deep_blocks();
int test_exception_weird_chars();


int main() {
    codegen_initialize();
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
    run_test("Lexer: 'as' Keyword", test_lexer_as_keyword);

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
    run_test("Parser: Member Access", test_parser_member_access);
    run_test("Parser: Flow Control", test_parser_flow_control);
    run_test("Parser: 'as' Cast", test_parser_cast);
    run_test("Parser: 'void' Pointer", test_parser_void_pointer);

    // Sema Tests
    run_test("Sema: Implicit Widening", test_sema_implicit_widening);
    run_test("Sema: Literal Inference", test_sema_literal_inference);
    run_test("Sema: Strict Binary Ops", test_sema_strict_binary_ops);
    run_test("Sema: 'as' Casting", test_sema_as_casting);
    run_test("Sema: Pointer Rules", test_sema_pointer_rules);
    run_test("Sema: Array & Slice Rules", test_sema_array_slice_rules);
    run_test("Sema: Shadowing & Scoping", test_sema_shadowing);
    run_test("Sema: Arg Count Mismatch", test_sema_arg_mismatch_regression);
    run_test("Sema: Undeclared Variable", test_sema_undeclared);
    run_test("Sema: Struct Basic", test_sema_struct_basic);
    run_test("Sema: Const Folding", test_sema_const_folding);

    // Codegen Tests
    run_test("Codegen: Basic Arithmetic", test_codegen_basic_arithmetic);
    run_test("Codegen: Float Math", test_codegen_float_math);
    run_test("Codegen: Complex Loop", test_codegen_complex_loop);
    run_test("Codegen: Nested Control Flow", test_codegen_nested_control_flow);
    run_test("Codegen: Recursion Edge", test_codegen_recursion_edge);
    run_test("Codegen: Multi-dim Arrays", test_codegen_multi_dim_arrays);
    run_test("Codegen: Array Len Casting", test_codegen_array_len_casting);
    run_test("Codegen: Slice Decay Func", test_codegen_slice_decay_func);
    run_test("Codegen: Nested Structs", test_codegen_nested_structs);
    run_test("Codegen: Struct Pointers", test_codegen_struct_pointers);
    run_test("Codegen: Void Ptr Roundtrip", test_codegen_void_ptr_roundtrip);
    run_test("Codegen: Ptr to Int Cast", test_codegen_ptr_to_int_cast);
    run_test("Codegen: Char Math", test_codegen_char_math);
    run_test("Codegen: Boolean Logic", test_codegen_boolean_logic);
    run_test("Codegen: Huge Stack Array", test_codegen_huge_stack_array);
    run_test("Codegen: Shadowing & Scoping", test_codegen_shadowing_scoping);
    run_test("Codegen: Bool Cast Branch", test_codegen_bool_cast_branch);
    run_test("Codegen: Struct Value Passing", test_codegen_struct_value_passing);
    run_test("Codegen: String Indexing", test_codegen_string_indexing);

    // Exceptions & Stress
    run_test("Exception: Long Identifier", test_exception_long_identifier);
    run_test("Exception: Unclosed Comment", test_exception_unclosed_comment);
    run_test("Exception: Deep Blocks", test_exception_deep_blocks);
    run_test("Exception: Weird Chars", test_exception_weird_chars);
    
    return print_test_summary();
}
