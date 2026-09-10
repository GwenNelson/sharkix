#include <stdint.h>
#include "FreeRTOS.h"
#include "console.h"
#include "thread.h"
#include "syscall.h"
#include "errno.h"
#include "ipc.h"
#include "caps.h"
#include "vmo.h"

static uint64_t announced_a;
static uint64_t announced_b;
static uint64_t block_test_thread_id;
static uint64_t block_test_invocation_count;
static uint64_t block_test_wake_count;

#define SHARKIX_SYSCALL_DECL(name) static syscall_disposition_t syscall_##name(syscall_ctx_t *ctx)

#define SHARKIX_SYSCALL(name,num) SHARKIX_SYSCALL_DECL(name);
#include <sharkix/syscalls.inc>
#undef SHARKIX_SYSCALL

#define SHARKIX_SYSCALL_IMPL(name) SHARKIX_SYSCALL_DECL(name)

static syscall_disposition_t syscall_return(void)
{
    return SYSCALL_DISPOSITION_RETURN;
}

static syscall_disposition_t syscall_block(void)
{
    return SYSCALL_DISPOSITION_BLOCK;
}

SHARKIX_SYSCALL_IMPL(IPC_CREATE) {
	thread_t* caller = thread_current();
	ipc_handle_t endpoint;
	cap_handle_t cap;
	ipc_status_t status = ipc_create(&endpoint);
	ctx->rax = (uint64_t)status; // shove the IPC error code into rax
	if(status != IPC_OK) {
	   ctx->rdi = (uint64_t)IPC_INVALID_HANDLE;
	   return syscall_return();
	}

	if(kcap_create((kobject_handle_t)endpoint,
		         CAP_TYPE_IPC_ENDPOINT,
			 CAP_IPC_VALID_RIGHTS,
			 &cap) != 0) {
		ipc_destroy(endpoint);
		ctx->rax = IPC_ERR_FAILED_CAP_CREATE;
		ctx->rdi = (uint64_t)IPC_INVALID_HANDLE;
		return syscall_return();
	}

	if(kcapset_addcap(caller->address_space->capset, cap) != 0) {
		ipc_destroy(endpoint);
		kcap_destroy(cap);
		ctx->rax = IPC_ERR_FAILED_CAP_CREATE;
		ctx->rdi = (uint64_t)IPC_INVALID_HANDLE;
		return syscall_return();
	}

	if(status==IPC_OK) {
		ctx->rdi = (uint64_t)cap;
	} else {
		ctx->rdi = (uint64_t)IPC_INVALID_HANDLE;
	}
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(IPC_SEND) {
	thread_t* caller = thread_current();
	ipc_message_t msg;

	msg.type       = IPC_MSGTYPE_SEND;
	msg.sender_tid = caller->id;
	msg.words[0]   = ctx->rsi;
	msg.words[1]   = ctx->rdx;
	msg.words[2]   = ctx->r10;
	msg.words[3]   = ctx->r8;
	msg.words[4]   = ctx->r9;

	cap_handle_t dest_cap = (cap_handle_t)ctx->rdi;
	kobject_handle_t obj_handle;

	if (kcapset_resolve_handle(thread_current()->address_space->capset,
				   dest_cap,
				   CAP_TYPE_IPC_ENDPOINT,
				   CAP_RIGHT_IPC_SEND,
				   &obj_handle) != 0) {
		ctx->rax = IPC_ERR_PERMISSION;
		return syscall_return();
	}

	ipc_handle_t dest_endpoint = (ipc_handle_t)obj_handle;
	ipc_status_t status      = ipc_send(caller,dest_endpoint,&msg);

	ctx->rax = (uint64_t)status;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(IPC_RECV) {
	thread_t* caller = thread_current();
	ipc_message_t msg;

	cap_handle_t src_cap = (cap_handle_t)ctx->rdi;
	kobject_handle_t obj_handle;

	if (kcapset_resolve_handle(caller->address_space->capset,
				   src_cap,
				   CAP_TYPE_IPC_ENDPOINT,
				   CAP_RIGHT_IPC_RECV,
				   &obj_handle) != 0) {
		ctx->rax = IPC_ERR_PERMISSION;
		return syscall_return();
	}

	ipc_handle_t endpoint = (ipc_handle_t)obj_handle;
	ipc_status_t status   = ipc_recv(endpoint,&msg);

	if(status == IPC_OK) {
		ctx->rax = (uint64_t)msg.type;
		ctx->rdi = (uint64_t)msg.sender_tid;
		ctx->rsi = (uint64_t)msg.words[0];
		ctx->rdx = (uint64_t)msg.words[1];
		ctx->r10 = (uint64_t)msg.words[2];
		ctx->r8  = (uint64_t)msg.words[3];
		ctx->r9  = (uint64_t)msg.words[4];
	} else {
		ctx->rax = (uint64_t)status;
	}
	return syscall_return();
}

// still need to implement the below
// should also look at how to integrate the scheduler properly - wake up the other thread and switch to it when something is sent to a thread that's currently blocked on a receive

SHARKIX_SYSCALL_IMPL(IPC_CALL) {
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(IPC_REPLY) {
	return syscall_return();
}

/*
 * input:
 *     RDI = VMO cap
 *     RSI = virtual address
 *     RDX = offset into VMO
 *     R10 = length
 *     R8  = requested mapping rights (VMO_READ/WRITE/EXEC)
 *     R9  = flags (must currently be 0)
 *
 * return:
 *     RAX = status
 *     RDX = mapped virtual address on success, 0 on failure
 */
SHARKIX_SYSCALL_IMPL(VM_MAP)
{
    thread_t *caller;

    cap_handle_t vmo_cap;
    cap_rights_t required_cap_rights;
    kobject_handle_t vmo_obj_handle;
    vmo_handle_t vmo_handle;

    uintptr_t va;
    size_t offset;
    size_t length;
    vmo_rights_t rights;
    uint64_t flags;

    caller = thread_current();

    if (caller == NULL || caller->address_space == NULL) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    vmo_cap = (cap_handle_t)ctx->rdi;
    va      = (uintptr_t)ctx->rsi;
    offset  = (size_t)ctx->rdx;
    length  = (size_t)ctx->r10;
    rights  = (vmo_rights_t)ctx->r8;
    flags   = ctx->r9;

    /*
     * No mapping flags are defined yet.
     */
    if (flags != 0) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    if (vmo_cap == CAP_INVALID_HANDLE) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    if (length == 0) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    /*
     * VMO_MAP is an operation right, not a mapping permission.
     * Callers request only READ/WRITE/EXEC here.
     */
    if (rights & ~(VMO_READ | VMO_WRITE | VMO_EXEC)) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    /*
     * Ordinary x86-64 paging cannot represent a present mapping which
     * isn't readable, so unreadable mappings aren't currently supported.
     */
    if (!(rights & VMO_READ)) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    /*
     * For now the caller must supply an actual virtual address.
     * A va of zero can later acquire "kernel chooses" semantics.
     */
    if (va == 0) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    /*
     * Translate the requested operation into capability authority.
     */
    required_cap_rights = CAP_RIGHT_VMO_MAP;

    if (rights & VMO_READ)
        required_cap_rights |= CAP_RIGHT_VMO_READ;

    if (rights & VMO_WRITE)
        required_cap_rights |= CAP_RIGHT_VMO_WRITE;

    if (rights & VMO_EXEC)
        required_cap_rights |= CAP_RIGHT_VMO_EXEC;

    /*
     * Resolve the exact supplied capability in the caller's address
     * space. Userspace never gets the underlying VMO handle.
     */
    if (kcapset_resolve_handle(caller->address_space->capset,
                               vmo_cap,
                               CAP_TYPE_VMO,
                               required_cap_rights,
                               &vmo_obj_handle) != 0) {
        ctx->rax = VM_ERR_PERMISSION;
        goto fail;
    }

    vmo_handle = (vmo_handle_t)vmo_obj_handle;

    /*
     * kvmo_map() performs intrinsic VMO-rights checking, PMEM bounds
     * checking, page-table mapping and vmoset bookkeeping.
     */
    if (kvmo_map(vmo_handle,
                 caller->address_space,
                 va,
                 offset,
                 length,
                 rights) != 0) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    ctx->rax = VM_OK;
    ctx->rdx = (uint64_t)va;
    return syscall_return();

fail:
    ctx->rdx = 0;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(VM_UNMAP) {
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_TRANSFER) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_FORWARD) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_DERIVE) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_DESTROY) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_REMOVE) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(VM_PROTECT) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PMEM_ALLOC) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PMEM_DERIVE) {
	(void)ctx;
	return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PMEM_MERGE) {
	(void)ctx;
	return syscall_return();
}

/*
 * input:
 * 	RDI = pmem cap
 * 	RSI = requested rights mask
 *
 * return:
 * 	RAX = status (VM_OK on success)
 * 	RDI = pmem cap (invalid handle on failure)
 *
 */
/*
 * input:
 *     RDI = PMEM cap
 *     RSI = requested VMO rights mask
 *
 * return:
 *     RAX = status (VM_OK on success)
 *     RDX = new VMO cap (CAP_INVALID_HANDLE on failure)
 */
SHARKIX_SYSCALL_IMPL(PMEM_NEW_VMO) {
    thread_t *caller;
    vmo_rights_t requested_rights;
    cap_rights_t required_pmem_rights;
    cap_rights_t new_cap_rights;
    cap_handle_t pmem_cap;

    vmo_handle_t vmo;
    cap_handle_t vmo_cap;

    kobject_handle_t pmem_obj_handle;
    pmem_handle_t pmem_handle;

    caller = thread_current();

    if (caller == NULL || caller->address_space == NULL) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    requested_rights = (vmo_rights_t)ctx->rsi;
    pmem_cap = (cap_handle_t)ctx->rdi;

    if (requested_rights & ~VMO_VALID_RIGHTS) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    if (!(requested_rights & VMO_MAP)) {
        /* An unmappable VMO is currently useless. */
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    if (!(requested_rights & VMO_READ)) {
        /* x86-64 paging cannot provide a present unreadable mapping. */
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    required_pmem_rights = 0;

    if (requested_rights & VMO_MAP)
        required_pmem_rights |= CAP_RIGHT_PMEM_MAP;

    if (requested_rights & VMO_READ)
        required_pmem_rights |= CAP_RIGHT_PMEM_READ;

    if (requested_rights & VMO_WRITE)
        required_pmem_rights |= CAP_RIGHT_PMEM_WRITE;

    if (requested_rights & VMO_EXEC)
        required_pmem_rights |= CAP_RIGHT_PMEM_EXEC;

    if (kcapset_resolve_handle(caller->address_space->capset,
                               pmem_cap,
                               CAP_TYPE_PMEM,
                               required_pmem_rights,
                               &pmem_obj_handle) != 0) {
        ctx->rax = VM_ERR_PERMISSION;
        goto fail;
    }

    pmem_handle = (pmem_handle_t)pmem_obj_handle;

    if (kvmo_create(&vmo, pmem_handle, requested_rights) != 0) {
        ctx->rax = VM_ERR_INVALID;
        goto fail;
    }

    new_cap_rights = CAP_GENERIC_VALID_RIGHTS;

    if (requested_rights & VMO_MAP)
        new_cap_rights |= CAP_RIGHT_VMO_MAP;

    if (requested_rights & VMO_READ)
        new_cap_rights |= CAP_RIGHT_VMO_READ;

    if (requested_rights & VMO_WRITE)
        new_cap_rights |= CAP_RIGHT_VMO_WRITE;

    if (requested_rights & VMO_EXEC)
        new_cap_rights |= CAP_RIGHT_VMO_EXEC;

    if (kcap_create((kobject_handle_t)vmo,
                    CAP_TYPE_VMO,
                    new_cap_rights,
                    &vmo_cap) != 0) {
        kvmo_destroy(vmo);
        ctx->rax = VM_ERR_FAILED_CAP_CREATE;
        goto fail;
    }

    if (kcapset_addcap(caller->address_space->capset, vmo_cap) != 0) {
        kcap_destroy(vmo_cap);
        kvmo_destroy(vmo);
        ctx->rax = VM_ERR_FAILED_CAP_CREATE;
        goto fail;
    }

    ctx->rax = VM_OK;
    ctx->rdx = (uint64_t)vmo_cap;
    return syscall_return();

fail:
    ctx->rdx = (uint64_t)CAP_INVALID_HANDLE;
    return syscall_return();
}


/* Existing observable syscall 0: write one character and return the trusted
 * caller's SharkKernel ID in RAX. */
SHARKIX_SYSCALL_IMPL(TEST_WRITE) {
    thread_t *caller = thread_current();
    if (!caller) { ctx->rax = UINT64_MAX; return syscall_return(); }
    if (caller->id != announced_a && caller->id != announced_b) {
        if (!announced_a) announced_a = caller->id;
        else announced_b = caller->id;
//        console_write("syscall caller thread "); console_decimal(caller->id); console_write("\n");
    }
    console_putc((char)ctx->rdi);
    ctx->rax = caller->id;
    return syscall_return();
}

/* Temporary test only: retain no policy or wait queue.  The assembly entry
 * performs the generic block after this returns BLOCK. */
SHARKIX_SYSCALL_IMPL(TEST_BLOCK) {
    thread_t *caller = thread_current();
    if (!caller || block_test_thread_id) { ctx->rax = UINT64_MAX; return syscall_return(); }
    block_test_thread_id = caller->id;
    ++block_test_invocation_count;
    return syscall_block();
}

/* Temporary test only: provide every eventual register result through the
 * blocked caller's one authoritative syscall-frame context, then wake it. */
SHARKIX_SYSCALL_IMPL(TEST_WAKE) {
    thread_t *blocked = thread_lookup(block_test_thread_id);
    syscall_ctx_t *result;
    if (!blocked || thread_get_state(block_test_thread_id) != THREAD_STATE_BLOCKED ||
        !(result = thread_get_blocked_syscall_context(blocked))) {
        ctx->rax = UINT64_MAX;
        return syscall_return();
    }
    result->rax = 0x000000000000b10cULL;
    result->rdi = 0x1111111111111111ULL;
    result->rsi = 0x2222222222222222ULL;
    result->rdx = 0x3333333333333333ULL;
    result->r10 = 0x4444444444444444ULL;
    result->r8  = 0x5555555555555555ULL;
    result->r9  = 0x6666666666666666ULL;
    if (thread_wake(blocked) != 0) { ctx->rax = UINT64_MAX; return syscall_return(); }
    ++block_test_wake_count;
    console_write("syscall_block wake issued\n");
    ctx->rax = 0;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(TEST_EXIT) {
	thread_exit_current();
	syscall_return(); // pointless, but keeps the compiler happy
}

syscall_disposition_t dispatch_syscall(syscall_ctx_t *ctx)
{
    switch (ctx->rax) {
#define SHARKIX_SYSCALL(name,num) case SYSCALL_##name: \
	    return syscall_##name(ctx); \
	    break;
#include <sharkix/syscalls.inc>
#undef SHARKIX_SYSCALL

	
/*    case SYSCALL_TEST_WRITE: 
	 return sys_test(ctx);
	 break;
    case SYSCALL_TEST_EXIT:
	 thread_exit_current();
	 break;
    case SYSCALL_TEST_BLOCK:
	 return sys_test_block(ctx);
    case SYSCALL_TEST_WAKE:
	 return sys_test_wake(ctx);
	 break;*/
    default:
        ctx->rax = UINT64_MAX;
        return syscall_return();
    }
}

uint64_t syscall_block_test_invocations(void) { return block_test_invocation_count; }
uint64_t syscall_block_test_wakes(void) { return block_test_wake_count; }
