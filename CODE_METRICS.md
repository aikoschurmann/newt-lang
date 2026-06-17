# Titan Engineering Audit (v3 - Enhanced Metrics)

## 📊 Global Summary
- **Total SLOC:** 9,958
- **Total Functions:** 76
- **Average CC:** 3.57
- **Average Maintainability Index:** 82.25/100

## 🌋 Top 15 God Functions (By Cyclomatic Complexity)
| CC | MI | Vol | LOC | Recursive | Function | File |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 30 | 40.8 | 726 | 41 | No | `node_type_to_string` | src/parsing/ast.c |
| 21 | 43.8 | 470 | 39 | No | `op_kind_to_str` | src/sema/type_report.c |
| 21 | 38.2 | 939 | 56 | No | `parse_int_lit` | src/parsing/parse_expressions.c |
| 17 | 37.1 | 1131 | 63 | No | `codegen_func_proto` | src/codegen/codegen_decl.c |
| 17 | 38.3 | 714 | 64 | No | `parse_program` | src/parsing/parse_declarations.c |
| 13 | 43.8 | 426 | 45 | No | `print_primitive_kind` | src/sema/type_print.c |
| 12 | 43.5 | 521 | 44 | No | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 11 | 57.1 | 144 | 16 | No | `type_is_integer` | src/sema/type_utils.c |
| 10 | 44.4 | 535 | 41 | No | `parse_path` | src/parsing/parse_types.c |
| 9 | 46.3 | 343 | 39 | No | `parse_block_statements` | src/parsing/parse_statements.c |
| 8 | 47.4 | 513 | 31 | No | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 7 | 43.3 | 569 | 47 | No | `read_file` | src/core/file.c |
| 7 | 48.1 | 271 | 36 | No | `hashmap_create` | src/datastructures/hash_map.c |
| 7 | 46.9 | 229 | 43 | No | `fold_unary_op` | src/sema/typecheck_expr.c |
| 7 | 51.4 | 221 | 27 | No | `define_symbol_or_error` | src/sema/symbol_utils.c |

## 📉 Least Maintainable Functions (Lowest MI)
| MI | CC | Vol | Function | File |
| :--- | :--- | :--- | :--- | :--- |
| 37.1 | 17 | 1131 | `codegen_func_proto` | src/codegen/codegen_decl.c |
| 38.2 | 21 | 939 | `parse_int_lit` | src/parsing/parse_expressions.c |
| 38.3 | 17 | 714 | `parse_program` | src/parsing/parse_declarations.c |
| 40.8 | 30 | 726 | `node_type_to_string` | src/parsing/ast.c |
| 41.8 | 4 | 884 | `codegen_materialize_slice` | src/codegen/codegen_ops.c |
| 43.3 | 7 | 569 | `read_file` | src/core/file.c |
| 43.5 | 12 | 521 | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 43.8 | 21 | 470 | `op_kind_to_str` | src/sema/type_report.c |
| 43.8 | 13 | 426 | `print_primitive_kind` | src/sema/type_print.c |
| 44.4 | 10 | 535 | `parse_path` | src/parsing/parse_types.c |
| 46.3 | 9 | 343 | `parse_block_statements` | src/parsing/parse_statements.c |
| 46.9 | 7 | 229 | `fold_unary_op` | src/sema/typecheck_expr.c |
| 47.4 | 8 | 513 | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 48.1 | 7 | 271 | `hashmap_create` | src/datastructures/hash_map.c |
| 50.2 | 6 | 280 | `scope_create` | src/datastructures/scope.c |

## 🎛 Switch Statement Audit
| Breadth (Cases) | Function | File |
| :--- | :--- | :--- |
| 29 | `node_type_to_string` | src/parsing/ast.c |
| 20 | `op_kind_to_str` | src/sema/type_report.c |
| 12 | `print_primitive_kind` | src/sema/type_print.c |
| 1 | `type_is_integer` | src/sema/type_utils.c |

## 🏗 Struct Bloat Audit
| Fields | Type | Name | File |
| :--- | :--- | :--- | :--- |
| 8 | struct | `<anonymous>` | src/main.c |
| 4 | struct | `<anonymous>` | src/core/module_loader.c |
| 2 | struct | `<anonymous>` | src/codegen/codegen_stmt.c |
| 2 | struct | `<anonymous>` | src/lexing/lexer.c |