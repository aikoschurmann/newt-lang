#include "../harness/test_harness.h"
#include "../helpers/compiler_helpers.h"

#define SEMA_VALID(name, src) \
    TEST_CASE_PRIO("Sema/Valid/" name, 30) { ASSERT(test_is_sema_valid(src)); return 1; }
#define SEMA_ERROR(name, src, kind) \
    TEST_CASE_PRIO("Sema/Error/" name, 30) { ASSERT(test_check_sema_error(src, kind)); return 1; }

#include "sema_basics.inc"
#include "sema_shadowing.inc"
#include "sema_structs.inc"
#include "sema_functions.inc"
#include "sema_intrinsics.inc"
#include "sema_casts.inc"
#include "sema_overload_forbidden.inc"
#include "sema_overload.inc"
#include "sema_ops.inc"
#include "sema_slices.inc"

#undef SEMA_VALID
#undef SEMA_ERROR
