#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "thread.h"
#include "syscall.h"

uint64_t dispatch_syscall(uint64_t number, uint64_t arg0, uint64_t arg1,
                          uint64_t arg2, uint64_t arg3, uint64_t arg4,
                          uint64_t arg5)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    thread_t *caller = thread_current();
    if (!caller) return UINT64_MAX;
    if (number == 0) {
        static uint64_t announced_a, announced_b;
        if (caller->id != announced_a && caller->id != announced_b) {
            if (!announced_a) announced_a = caller->id; else announced_b = caller->id;
            console_write("syscall caller thread "); console_decimal(caller->id); console_write("\n");
        }
        console_putc((char)arg0);
        return caller->id;
    }
    if (number == 1) thread_exit_current();
    return UINT64_MAX;
}
