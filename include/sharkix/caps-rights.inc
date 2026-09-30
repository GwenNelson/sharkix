#pragma once

/*
 * Generic capability rights.
 */

#define SHARKIX_CAP_GENERIC_RIGHT(name, bit) \
    CAP_RIGHT_##name = (UINT64_C(1) << (bit)),

#define SHARKIX_CAP_RESERVED_RIGHT(bit) \
    CAP_RIGHT_RESV##bit = (UINT64_C(1) << (bit)),

#define SHARKIX_CAP_TYPE_BEGIN(name, number)
#define SHARKIX_CAP_RIGHT(type, name, bit)
#define SHARKIX_CAP_TYPE_END(name)

enum {
#include <sharkix/caps.inc>
};

#undef SHARKIX_CAP_GENERIC_RIGHT
#undef SHARKIX_CAP_RESERVED_RIGHT
#undef SHARKIX_CAP_TYPE_BEGIN
#undef SHARKIX_CAP_RIGHT
#undef SHARKIX_CAP_TYPE_END


#define SHARKIX_CAP_GENERIC_RIGHT(name, bit) \
    | (UINT64_C(1) << (bit))

#define SHARKIX_CAP_RESERVED_RIGHT(bit)
#define SHARKIX_CAP_TYPE_BEGIN(name, number)
#define SHARKIX_CAP_RIGHT(type, name, bit)
#define SHARKIX_CAP_TYPE_END(name)

enum {
    CAP_GENERIC_VALID_RIGHTS =
        UINT64_C(0)
#include <sharkix/caps.inc>
};

#undef SHARKIX_CAP_GENERIC_RIGHT
#undef SHARKIX_CAP_RESERVED_RIGHT
#undef SHARKIX_CAP_TYPE_BEGIN
#undef SHARKIX_CAP_RIGHT
#undef SHARKIX_CAP_TYPE_END
