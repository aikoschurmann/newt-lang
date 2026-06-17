# Titan Engineering Audit (v3 - Enhanced Metrics)

## 📊 Global Summary
- **Total SLOC:** 10,119
- **Total Functions:** 78
- **Average CC:** 4.74
- **Average Maintainability Index:** 79.67/100

## 🌋 Top 15 God Functions (By Cyclomatic Complexity)
| CC | MI | Vol | LOC | Recursive | Function | File |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 31 | 30.1 | 2091 | 89 | No | `parse_options` | src/cli/cli.c |
| 31 | 27.8 | 1516 | 125 | Yes | `count_nodes_recursive` | src/cli/metrics.c |
| 30 | 40.8 | 726 | 41 | No | `node_type_to_string` | src/parsing/ast.c |
| 26 | 30.8 | 1213 | 105 | Yes | `type_print_internal` | src/sema/type_print.c |
| 25 | 31.5 | 1688 | 89 | No | `codegen_decl_proto` | src/codegen/codegen_decl.c |
| 21 | 43.8 | 470 | 39 | No | `op_kind_to_str` | src/sema/type_report.c |
| 21 | 38.2 | 939 | 56 | No | `parse_int_lit` | src/parsing/parse_expressions.c |
| 17 | 38.3 | 714 | 64 | No | `parse_program` | src/parsing/parse_declarations.c |
| 15 | 38.0 | 798 | 66 | No | `parse_block` | src/parsing/parse_statements.c |
| 12 | 43.5 | 521 | 44 | No | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 11 | 57.1 | 144 | 16 | No | `type_is_integer` | src/sema/type_utils.c |
| 11 | 32.9 | 393 | 150 | No | `print_parse_error` | src/parsing/parser.c |
| 10 | 44.4 | 535 | 41 | No | `parse_path` | src/parsing/parse_types.c |
| 8 | 47.4 | 513 | 31 | No | `get_llvm_function_type` | src/codegen/codegen_types.c |
| 7 | 43.3 | 569 | 47 | No | `read_file` | src/core/file.c |

## 📉 Least Maintainable Functions (Lowest MI)
| MI | CC | Vol | Function | File |
| :--- | :--- | :--- | :--- | :--- |
| 27.8 | 31 | 1516 | `count_nodes_recursive` | src/cli/metrics.c |
| 30.1 | 31 | 2091 | `parse_options` | src/cli/cli.c |
| 30.8 | 26 | 1213 | `type_print_internal` | src/sema/type_print.c |
| 31.5 | 25 | 1688 | `codegen_decl_proto` | src/codegen/codegen_decl.c |
| 32.9 | 11 | 393 | `print_parse_error` | src/parsing/parser.c |
| 38.0 | 15 | 798 | `parse_block` | src/parsing/parse_statements.c |
| 38.2 | 21 | 939 | `parse_int_lit` | src/parsing/parse_expressions.c |
| 38.3 | 17 | 714 | `parse_program` | src/parsing/parse_declarations.c |
| 40.8 | 30 | 726 | `node_type_to_string` | src/parsing/ast.c |
| 41.8 | 4 | 884 | `codegen_materialize_slice` | src/codegen/codegen_ops.c |
| 43.3 | 7 | 569 | `read_file` | src/core/file.c |
| 43.5 | 12 | 521 | `codegen_lvalue_identifier` | src/codegen/codegen_lvalue.c |
| 43.8 | 21 | 470 | `op_kind_to_str` | src/sema/type_report.c |
| 44.4 | 10 | 535 | `parse_path` | src/parsing/parse_types.c |
| 46.9 | 7 | 229 | `fold_unary_op` | src/sema/typecheck_expr.c |

## 🎛 Switch Statement Audit
| Breadth (Cases) | Function | File |
| :--- | :--- | :--- |
| 29 | `node_type_to_string` | src/parsing/ast.c |
| 20 | `op_kind_to_str` | src/sema/type_report.c |
| 16 | `count_nodes_recursive` | src/cli/metrics.c |
| 12 | `type_print_internal` | src/sema/type_print.c |
| 8 | `type_print_internal` | src/sema/type_print.c |
| 1 | `type_is_integer` | src/sema/type_utils.c |

## 🏗 Struct Bloat Audit
| Fields | Type | Name | File |
| :--- | :--- | :--- | :--- |
| 8 | struct | `<anonymous>` | src/main.c |
| 4 | struct | `<anonymous>` | src/core/module_loader.c |
| 2 | struct | `<anonymous>` | src/codegen/codegen_stmt.c |
| 2 | struct | `<anonymous>` | src/lexing/lexer.c |