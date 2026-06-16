#include "../harness/test_harness.h"
#include "../helpers/compiler_helpers.h"

#define PARSE_VALID(name, src) \
    TEST_CASE_PRIO("Parser/Valid/" name, 20) { ASSERT(test_is_parse_valid(src)); return 1; }
#include "parser_cases.inc"
#include "parser_stress.inc"
#undef PARSE_VALID

TEST_CASE_PRIO("Parser: Invalid Syntax", 20) {
    ASSERT(!test_is_parse_valid("fn main() { x: i32 = ; }"));
    ASSERT(!test_is_parse_valid("fn main() { if true return 1; }")); // missing parens
    return 1;
}
