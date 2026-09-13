#pragma once

// standard syscall enum
#define SHARKIX_SYSCALL(name, number) SYSCALL_##name = number,
typedef enum syscall_enum_t {
#include <sharkix/syscalls.inc>
} syscall_enum_t;
#undef SHARKIX_SYSCALL
