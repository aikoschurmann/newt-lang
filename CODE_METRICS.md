# Engineering Audit

## Global Summary
- **Total SLOC:** 10,114
- **Total Functions:** 76
- **Average CC:** 2.61
- **Average Maintainability Index:** 83.20/100

## Top 15 God Functions (By Cyclomatic Complexity)
| CC | MI | Vol | LOC | Recursive | Function | File |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 21 | 43.8 | 470 | 39 | No | `op_kind_to_str` | src/sema/type_report.c |
| 12 | 43.5 | 521 | 44 | No | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 11 | 57.1 | 144 | 16 | No | `type_is_integer` | src/sema/type_utils.c |
| 10 | 44.4 | 535 | 41 | No | `parse_path` | src/parsing/parse_types.c |
| 9 | 46.3 | 343 | 39 | No | `parse_block_statements` | src/parsing/parse_statements.c |
| 8 | 47.4 | 513 | 31 | No | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 8 | 49.1 | 241 | 33 | No | `parse_program_decls` | src/parsing/parse_declarations.c |
| 7 | 43.3 | 569 | 47 | No | `read_file` | src/core/file.c |
| 7 | 48.1 | 271 | 36 | No | `hashmap_create` | src/datastructures/hash_map.c |
| 7 | 46.9 | 229 | 43 | No | `fold_unary_op` | src/sema/typecheck_expr.c |
| 7 | 51.4 | 221 | 27 | No | `define_symbol_or_error` | src/sema/symbol_utils.c |
| 7 | 52.0 | 206 | 26 | No | `detect_base` | src/parsing/parse_expressions.c |
| 6 | 50.2 | 280 | 29 | No | `scope_create` | src/datastructures/scope.c |
| 5 | 51.1 | 301 | 26 | No | `collect_llvm_param_types` | src/codegen/codegen_decl.c |
| 4 | 46.4 | 162 | 53 | No | `print_primitive_kind` | src/sema/type_print.c |

## Least Maintainable Functions (Lowest MI)
| MI | CC | Vol | Function | File |
| :--- | :--- | :--- | :--- | :--- |
| 43.3 | 7 | 569 | `read_file` | src/core/file.c |
| 43.5 | 12 | 521 | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 43.8 | 21 | 470 | `op_kind_to_str` | src/sema/type_report.c |
| 44.4 | 10 | 535 | `parse_path` | src/parsing/parse_types.c |
| 46.3 | 9 | 343 | `parse_block_statements` | src/parsing/parse_statements.c |
| 46.4 | 4 | 162 | `print_primitive_kind` | src/sema/type_print.c |
| 46.9 | 7 | 229 | `fold_unary_op` | src/sema/typecheck_expr.c |
| 47.4 | 8 | 513 | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 48.1 | 7 | 271 | `hashmap_create` | src/datastructures/hash_map.c |
| 49.1 | 8 | 241 | `parse_program_decls` | src/parsing/parse_declarations.c |
| 50.2 | 6 | 280 | `scope_create` | src/datastructures/scope.c |
| 50.2 | 4 | 72 | `node_type_to_string` | src/parsing/ast.c |
| 50.5 | 1 | 210 | `ice_impl` | src/core/error.c |
| 51.1 | 5 | 301 | `collect_llvm_param_types` | src/codegen/codegen_decl.c |
| 51.4 | 7 | 221 | `define_symbol_or_error` | src/sema/symbol_utils.c |

## Switch Statement Audit
| Breadth (Cases) | Function | File |
| :--- | :--- | :--- |
| 20 | `op_kind_to_str` | src/sema/type_report.c |
| 1 | `type_is_integer` | src/sema/type_utils.c |

## Struct Bloat Audit
| Fields | Type | Name | File |
| :--- | :--- | :--- | :--- |
| 8 | struct | `<anonymous>` | src/main.c |
| 4 | struct | `<anonymous>` | src/core/module_loader.c |
| 2 | struct | `<anonymous>` | src/codegen/codegen_stmt.c |
| 2 | struct | `<anonymous>` | src/lexing/lexer.c |