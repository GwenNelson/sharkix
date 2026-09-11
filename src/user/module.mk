USER_MODULE_ROOT := $(USER_SRC_ROOT)
USER_ASM_SRCS += \
 $(USER_MODULE_ROOT)/taskA.s $(USER_MODULE_ROOT)/taskB.s \
 $(USER_MODULE_ROOT)/tests/ud.s $(USER_MODULE_ROOT)/tests/pagefault.s \
 $(USER_MODULE_ROOT)/tests/kernel_access.s $(USER_MODULE_ROOT)/tests/exit.s \
 $(USER_MODULE_ROOT)/tests/syscall_blocker.s $(USER_MODULE_ROOT)/tests/syscall_waker.s \
 $(USER_MODULE_ROOT)/task_IPC_consumer.s $(USER_MODULE_ROOT)/task_IPC_producer.s \
 $(USER_MODULE_ROOT)/task_IPC_benchmark.s
USER_LINKER_SCRIPT := $(USER_MODULE_ROOT)/task.ld
USER_EMBED_SPECS += \
 taskA|taskA|taskA \
 taskB|taskB|taskB \
 ud|tests/ud|ud \
 pagefault|tests/pagefault|pagefault \
 kernel_access|tests/kernel_access|kernel_access \
 exit|tests/exit|exit \
 syscall_blocker|tests/syscall_blocker|syscall_blocker \
 syscall_waker|tests/syscall_waker|syscall_waker \
 ipc_consumer|task_IPC_consumer|ipc_consumer \
 ipc_producer|task_IPC_producer|ipc_producer \
 ipc_benchmark|task_IPC_benchmark|ipc_benchmark
