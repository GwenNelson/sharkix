#pragma once

#define SHARKIX_CAP_GENERIC_RIGHT(name, bit)
#define SHARKIX_CAP_RESERVED_RIGHT(bit)
#define SHARKIX_CAP_RIGHT(type, name, bit)
#define SHARKIX_CAP_TYPE_END(name)

#define SHARKIX_CAP_TYPE_BEGIN(name, number) \
    CAP_TYPE_##name = number,

typedef enum cap_type_t {
#include <sharkix/caps.inc>
} cap_type_t;

#undef SHARKIX_CAP_GENERIC_RIGHT
#undef SHARKIX_CAP_RESERVED_RIGHT
#undef SHARKIX_CAP_TYPE_BEGIN
#undef SHARKIX_CAP_RIGHT
#undef SHARKIX_CAP_TYPE_END
