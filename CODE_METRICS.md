# Engineering Audit

## Global Summary
- **Total SLOC:** 11,605
- **Total Analyzed Functions:** 79
- **Average Cyclomatic Complexity (CC):** 2.78
- **Average Maintainability Index (MI):** 82.70/100

## Top 15 God Functions (By Cyclomatic Complexity)
| CC | MI | Vol | LOC | Recursive | Function | File |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 21 | 43.8 | 470 | 39 | No | `op_kind_to_str` | src/sema/type_report.c |
| 20 | 37.5 | 1192 | 57 | No | `find_struct_method` | src/sema/typecheck_expr.c |
| 12 | 41.6 | 968 | 44 | No | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 11 | 56.4 | 183 | 16 | No | `type_is_integer` | src/sema/type_utils.c |
| 10 | 43.2 | 776 | 41 | No | `parse_path` | src/parsing/parse_types.c |
| 9 | 45.5 | 448 | 39 | No | `parse_block_statements` | src/parsing/parse_statements.c |
| 8 | 48.3 | 317 | 33 | No | `parse_program_decls` | src/parsing/parse_declarations.c |
| 8 | 46.2 | 764 | 31 | No | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 7 | 47.2 | 357 | 36 | No | `hashmap_create` | src/datastructures/hash_map.c |
| 7 | 43.2 | 579 | 47 | No | `read_file` | src/core/file.c |
| 7 | 50.7 | 280 | 27 | No | `define_symbol_or_error` | src/sema/symbol_utils.c |
| 7 | 37.6 | 536 | 87 | No | `fold_unary_op` | src/sema/typecheck_expr.c |
| 7 | 51.6 | 232 | 26 | No | `detect_base` | src/parsing/parse_expressions.c |
| 6 | 49.2 | 385 | 29 | No | `scope_create` | src/datastructures/scope.c |
| 5 | 50.0 | 434 | 26 | No | `collect_llvm_param_types` | src/codegen/codegen_decl.c |

## Least Maintainable Functions (Lowest MI)
| MI | CC | Vol | Function | File |
| :--- | :--- | :--- | :--- | :--- |
| 37.5 | 20 | 1192 | `find_struct_method` | src/sema/typecheck_expr.c |
| 37.6 | 7 | 536 | `fold_unary_op` | src/sema/typecheck_expr.c |
| 41.6 | 12 | 968 | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 43.2 | 10 | 776 | `parse_path` | src/parsing/parse_types.c |
| 43.2 | 7 | 579 | `read_file` | src/core/file.c |
| 43.8 | 21 | 470 | `op_kind_to_str` | src/sema/type_report.c |
| 45.5 | 9 | 448 | `parse_block_statements` | src/parsing/parse_statements.c |
| 46.2 | 4 | 173 | `print_primitive_kind` | src/sema/type_print.c |
| 46.2 | 8 | 764 | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 47.2 | 7 | 357 | `hashmap_create` | src/datastructures/hash_map.c |
| 48.3 | 8 | 317 | `parse_program_decls` | src/parsing/parse_declarations.c |
| 49.2 | 6 | 385 | `scope_create` | src/datastructures/scope.c |
| 49.6 | 4 | 81 | `node_type_to_string` | src/parsing/ast.c |
| 50.0 | 5 | 434 | `collect_llvm_param_types` | src/codegen/codegen_decl.c |
| 50.5 | 1 | 210 | `ice_impl` | src/core/error.c |

## Switch Statement Audit (Potential Refactors)
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