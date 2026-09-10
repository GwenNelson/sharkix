/* include/sharkix/kernel/kmalloc.h */

#pragma once

#include <stddef.h>

void *kmalloc(size_t size);
void  kfree(void *ptr);
