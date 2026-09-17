#pragma once

#define IPC_REGNAME_MAX 256

#include <sharkix/kernel/ipc.h>

void kipc_registry_init(void);

int kipc_registry_register(const char* name, ipc_handle_t endpoint);

int kipc_registry_lookup(const char* name, ipc_handle_t *out);

int kipc_registry_unregister(const char* name);
