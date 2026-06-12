#pragma once

#include <stddef.h>

typedef enum {
    INTRINSIC_NONE = 0,
    INTRINSIC_PRINT,
    INTRINSIC_PRINT_NEWLINE,
    INTRINSIC_ALLOC,
    INTRINSIC_FREE,
    INTRINSIC_UNKNOWN
} IntrinsicKind;