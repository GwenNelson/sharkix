#pragma once

/*
 * Capability rights derived from <sharkix/caps.inc>.
 *
 * Generates:
 *
 *   - all generic capability rights
 *   - reserved capability right bits
 *   - all type-specific capability rights
 *   - CAP_GENERIC_VALID_RIGHTS
 *   - CAP_<TYPE>_VALID_RIGHTS for every capability type
 */


/*
 * No rights.
 */

#define CAP_RIGHT_NONE UINT64_C(0)


/*
 * Pass 1:
 *
 * Generate the bitmask constant for every individual capability right.
 */

#define SHARKIX_CAP_GENERIC_RIGHT(name, bit) \
    CAP_RIGHT_##name = (UINT64_C(1) << (bit)),

#define SHARKIX_CAP_RESERVED_RIGHT(bit) \
    CAP_RIGHT_RESV##bit = (UINT64_C(1) << (bit)),

#define SHARKIX_CAP_TYPE_BEGIN(name, number)

#define SHARKIX_CAP_RIGHT(type, name, bit) \
    CAP_RIGHT_##type##_##name = (UINT64_C(1) << (bit)),

#define SHARKIX_CAP_TYPE_END(name)

enum {
#include <sharkix/caps.inc>
};

#undef SHARKIX_CAP_GENERIC_RIGHT
#undef SHARKIX_CAP_RESERVED_RIGHT
#undef SHARKIX_CAP_TYPE_BEGIN
#undef SHARKIX_CAP_RIGHT
#undef SHARKIX_CAP_TYPE_END


/*
 * Pass 2:
 *
 * Generate the mask containing every valid generic capability right.
 *
 * Reserved bits are deliberately excluded.
 */

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


/*
 * Pass 3:
 *
 * Generate the valid-rights mask for every capability type.
 *
 * Every capability type permits all generic rights plus any
 * type-specific rights declared for that type.
 */

#define SHARKIX_CAP_GENERIC_RIGHT(name, bit)
#define SHARKIX_CAP_RESERVED_RIGHT(bit)

#define SHARKIX_CAP_TYPE_BEGIN(name, number) \
    CAP_##name##_VALID_RIGHTS = CAP_GENERIC_VALID_RIGHTS

#define SHARKIX_CAP_RIGHT(type, name, bit) \
    | (UINT64_C(1) << (bit))

#define SHARKIX_CAP_TYPE_END(name) \
    ,

enum {
#include <sharkix/caps.inc>
};

#undef SHARKIX_CAP_GENERIC_RIGHT
#undef SHARKIX_CAP_RESERVED_RIGHT
#undef SHARKIX_CAP_TYPE_BEGIN
#undef SHARKIX_CAP_RIGHT
#undef SHARKIX_CAP_TYPE_END
