#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

// TODO - i really really need to begin moving more of this stuff into libsharkix, probably want to centralize a lot of the ABI values too etc
//        also, all the various helper functions in these ring3 tests should go into libsharkix eventually
#define VMO_READ  0x2
#define VMO_WRITE 0x4
#define VMO_EXEC  0x8

// this isn't the usual convention, but these are tiny asm stubs, so....
#define TASK_ENTRY      0x400000 
#define TASK_STACK_BASE 0x801000
#define TASK_STACK_TOP  0x802000 

int get_vmo_pagelen(uint64_t vmo, uint64_t *outlen) {
        sharkix_syscall_regs_t regs = { 0 };
        regs.rax = (uint64_t)SYSCALL_VM_GETLEN;
        regs.rdi = (uint64_t)vmo;
        (void)sharkix_syscall(&regs);
        if(regs.rax == 0) {
                *outlen = regs.rdx; // SUCCESS - correct page-rounded length of the VMO is here
                return 0;
        } else {
                *outlen = 0; // FAIL
                return -1; // TODO - again, i need to sort out the fucking errno situation
        }
}

static void test_write(const char *message)
{
    for (size_t i = 0; message[i]; ++i) {
        sharkix_syscall_regs_t regs = { 0 };
        regs.rax = SYSCALL_TEST_WRITE;
        regs.rdi = (uint64_t)(unsigned char)message[i];
        (void)sharkix_syscall(&regs);
    }
}

static void test_exit(void)
{
    sharkix_syscall_regs_t regs = { 0 };
    regs.rax = SYSCALL_TEST_EXIT;
    (void)sharkix_syscall(&regs);
    for (;;) __asm__ volatile ("pause");
}

int create_as(uint64_t as_factory, uint64_t *new_as) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = SYSCALL_AS_CREATE;
	regs.rdi = as_factory;
	(void)sharkix_syscall(&regs);
	if(regs.rax != 0) {
		return -1;
	} else {
		*new_as = regs.rdi;
		return 0;
	}
}


int map_vmo_target(uint64_t as, uint64_t vmo, uint64_t vaddr, uint64_t offs, uint64_t len, uint64_t map_rights) {
        sharkix_syscall_regs_t regs = { 0 };

        regs.rax = (uint64_t)SYSCALL_AS_MAP;
        regs.rdi = as;
        regs.rsi = vmo;
        regs.rdx = vaddr;
        regs.r10 = offs;
        regs.r8  = len;
        regs.r9  = map_rights;

        (void)sharkix_syscall(&regs);
        return regs.rax;
}

int create_anon(uint64_t vmo_factory, uint64_t req_size, uint64_t map_rights, uint64_t *actual_size, uint64_t *new_vmo) {
        sharkix_syscall_regs_t regs = { 0 };

        regs.rax = (uint64_t)SYSCALL_VM_CREATE_ANON;
        regs.rdi = vmo_factory;
        regs.rsi = req_size;
        regs.rdx = map_rights;

        (void)sharkix_syscall(&regs);
        if(regs.rax == 0) {
                *new_vmo     = regs.rsi;
                *actual_size = regs.rdx;
                return 0;
        } else {
                *actual_size = 0;
                return -1;
        }
}

int create_thread(uint64_t thread_factory, uint64_t as, uint64_t entry, uint64_t stack_pointer, uint64_t *new_thread) {
	sharkix_syscall_regs_t regs = { 0 };

	regs.rax = (uint64_t)SYSCALL_THREAD_CREATE;
	regs.rdi = thread_factory;
	regs.rsi = as;
	regs.rdx = entry;
	regs.r10 = stack_pointer;

	(void)sharkix_syscall(&regs);
	if(regs.rax == 0) {
		*new_thread = regs.rdi;
		return 0;
	} else {
		return -1;
	}
}

int start_thread(uint64_t thread) {
	sharkix_syscall_regs_t regs = { 0 };
	
	regs.rax = (uint64_t)SYSCALL_THREAD_START;
	regs.rdi = thread;
	(void)sharkix_syscall(&regs);
	return regs.rax;
}

static void do_the_test(uint64_t as_factory, uint64_t thread_factory, uint64_t vmo_factory, uint64_t payload_a_vmo, uint64_t payload_b_vmo) {
	// first i need to create the AS for both
	test_write("Creating ThreadA AS...\n");
	uint64_t as_a;
	if(create_as(as_factory,&as_a) != 0) {
		test_write("ERROR! Couldn't create the AS!\n");
		test_exit();
		for(;;);
	}

	test_write("Creating ThreadB AS...\n");
	uint64_t as_b;
	if(create_as(as_factory,&as_b) != 0) {
		test_write("ERROR! Couldn't create the AS!\n");
		test_exit();
		for(;;);
	}

	// now let's map the VMOs appropriately - get lengths first
	test_write("Getting length for ThreadA VMO...\n");
	uint64_t taskA_len = 0;
	if(get_vmo_pagelen(payload_a_vmo,&taskA_len) != 0) {
		test_write("ERROR! Failed to get the length!\n");
		test_exit();
		for(;;);
	}

	test_write("Getting length for ThreadB VMO...\n");
	uint64_t taskB_len = 0;
	if(get_vmo_pagelen(payload_b_vmo,&taskB_len) != 0) {
		test_write("ERROR! Failed to get the length!\n");
		test_exit();
		for(;;);
	}

	// now we can map the fuckers
	test_write("Mapping ThreadA VMO...\n");
	if(map_vmo_target(as_a,payload_a_vmo,TASK_ENTRY,0,taskA_len,VMO_READ|VMO_EXEC) != 0) {
		test_write("ERROR! Failed to map the VMO!\n");
		test_exit();
		for(;;);
	}

	test_write("Mapping ThreadB VMO...\n");
	if(map_vmo_target(as_b,payload_b_vmo,TASK_ENTRY,0,taskB_len,VMO_READ|VMO_EXEC) != 0) {
		test_write("ERROR! Failed to map the VMO!\n");
		test_exit();
		for(;;);
	}

	// and setup some stacks, these are tiny asm stubs so a single page is probably plenty, that's 4096kb
	test_write("Creating ThreadA stack anon VMO...\n");
	uint64_t taskA_stacklen;
	uint64_t taskA_stackVMO;
	if(create_anon(vmo_factory,4096,VMO_WRITE|VMO_READ,&taskA_stacklen,&taskA_stackVMO) != 0) {
		test_write("ERROR! Failed to create new stack VMO!\n");
		test_exit();
		for(;;);
	}

	test_write("Creating ThreadB stack anon VMO...\n");
	uint64_t taskB_stacklen;
	uint64_t taskB_stackVMO;
	if(create_anon(vmo_factory,4096,VMO_WRITE|VMO_READ,&taskB_stacklen,&taskB_stackVMO) != 0) {
		test_write("ERROR! Failed to create new stack VMO!\n");
		test_exit();
		for(;;);
	}

	// and map the stacks
	test_write("Mapping ThreadA stack...\n");
	if(map_vmo_target(as_a,taskA_stackVMO,TASK_STACK_BASE,0,taskA_stacklen,VMO_WRITE|VMO_READ) != 0) {
		test_write("Mapping stack failed!\n");
		test_exit();
		for(;;);
	}

	test_write("Mapping ThreadB stack...\n");
	if(map_vmo_target(as_b,taskB_stackVMO,TASK_STACK_BASE,0,taskB_stacklen,VMO_WRITE|VMO_READ) != 0) {
		test_write("Mapping stack failed!\n");
		test_exit();
		for(;;);
	}

	// now for the main event, we can now actually create threads!
	test_write("Creating threads, finally!\n");
	test_write("If this goes well, your console should be flooded with A/B/M characters....\n");

	uint64_t threadA_cap;
	if(create_thread(thread_factory,as_a,TASK_ENTRY,TASK_STACK_TOP,&threadA_cap) != 0) {
		test_write("FAILED CREATE A!\n");
		test_exit();
		for(;;);
	}

	uint64_t threadB_cap;
	if(create_thread(thread_factory,as_b,TASK_ENTRY,TASK_STACK_TOP,&threadB_cap) != 0) {
		test_write("FAILED CREATE B!\n");
		test_exit();
		for(;;);
	}

	if(start_thread(threadA_cap) != 0) {
		test_write("FAILED START A!\n");
		test_exit();
		for(;;);
	}

	if(start_thread(threadB_cap) != 0) {
		test_write("FAILED START B\n");
		test_exit();
		for(;;);
	}

	// M for "main"
	for(;;) {
		test_write("M"); 
		 __asm__ volatile ("int $0x90" ::: "memory");
	}
}

void kthread_test_main(uint64_t *bootstrap)
{
    char *names[] = {
        "factory.as",
        "factory.thread",
        "factory.vmo",
        "vmo.payload.a",
        "vmo.payload.b"
    };
    uint64_t caps[5];

    if (sharkix_get_bootstrap(caps, names, 5, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK) {
        test_write("[kthread_test] bootstrap failed\n");
        test_exit();
    }

    uint64_t as_factory = caps[0];
    uint64_t thread_factory = caps[1];
    uint64_t vmo_factory = caps[2];
    uint64_t payload_a_vmo = caps[3];
    uint64_t payload_b_vmo = caps[4];

    do_the_test(as_factory, thread_factory, vmo_factory,
                payload_a_vmo, payload_b_vmo);
    test_exit();
}
