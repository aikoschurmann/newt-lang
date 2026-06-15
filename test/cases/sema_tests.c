#include "../harness/test_harness.h"
#include "../helpers/compiler_helpers.h"

// 1. Basic Table-driven valid/error cases
#define SEMA_VALID(name, src) \
    TEST_CASE("Sema/Valid/" name) { ASSERT(test_is_sema_valid(src)); return 1; }
#define SEMA_ERROR(name, src, kind) \
    TEST_CASE("Sema/Error/" name) { ASSERT(test_check_sema_error(src, kind)); return 1; }

#include "sema_basics.inc"
#include "sema_casts.inc"
#include "sema_shadowing.inc"
#include "sema_structs.inc"
#include "sema_functions.inc"
#include "sema_intrinsics.inc"

#undef SEMA_VALID
#undef SEMA_ERROR
