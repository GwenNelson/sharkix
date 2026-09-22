#pragma once

typedef enum sharkix_status_t {
#define SHARKIX_ERRNO(name,value,msg) name = value,
#include <sharkix/kernel/generic_errno.inc>
#undef SHARKIX_ERRNO
} sharkix_status_t;


