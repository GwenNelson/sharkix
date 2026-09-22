#include <stdint.h>
#include "console.h"
#include "thread.h"
#include "syscall.h"
#include "errno.h"
#include "ipc.h"
#include "caps.h"
#include "portio.h"
#include "vmo.h"
#include "irq.h"

static uint64_t announced_a;
static uint64_t announced_b;
static uint64_t block_test_thread_id;
static uint64_t block_test_invocation_count;
static uint64_t block_test_wake_count;
static uint64_t benchmark_ready_count;
static uint64_t benchmark_start_cycles;
static uint64_t benchmark_stop_cycles;

static inline uint64_t benchmark_tsc_start(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence\nrdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t benchmark_tsc_stop(void)
{
    uint32_t lo, hi;
    /* qemu64 does not expose RDTSCP; LFENCE/RDTSC/LFENCE is serialized. */
    __asm__ volatile("lfence\nrdtsc\nlfence" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

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

static portio_status_t syscall_portio_resolve(syscall_ctx_t *ctx,
                                              cap_rights_t required_rights,
                                              bool require_offset,
                                              portio_handle_t *out)
{
    thread_t *caller;
    kobject_handle_t object_handle;
    portio_t portio;
    cap_handle_t cap;

    if (!out)
        return PORTIO_ERR_INVALID;

    caller = thread_current();
    if (!caller || !caller->address_space)
        return PORTIO_ERR_INVALID;

    cap = (cap_handle_t)ctx->rdi;
    if (cap == CAP_INVALID_HANDLE)
        return PORTIO_ERR_INVALID;

    if (require_offset && ctx->rsi > UINT32_MAX)
        return PORTIO_ERR_INVALID;

    if (kcapset_resolve_handle(caller->address_space->capset,
                               cap,
                               CAP_TYPE_PORTIO,
                               required_rights,
                               &object_handle) != 0)
        return PORTIO_ERR_PERMISSION;

    *out = (portio_handle_t)object_handle;
    if (kportio_get(*out, &portio) != 0)
        return PORTIO_ERR_NOT_FOUND;

    return PORTIO_OK;
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

/*
 * input:
 *     RDI = VMO cap
 *     RSI = virtual address
 *
 * return:
 *     RAX = status
 */
SHARKIX_SYSCALL_IMPL(VM_UNMAP) {
    thread_t *caller;

    cap_handle_t vmo_cap;
    kobject_handle_t vmo_obj_handle;
    vmo_handle_t vmo_handle;

    caller = thread_current();

    vmo_cap       = (cap_handle_t)ctx->rdi;
    uintptr_t va  = (uintptr_t)ctx->rsi;

    if (caller == NULL || caller->address_space == NULL) {
        ctx->rax = VM_ERR_INVALID;
        goto done;
    }

    if (vmo_cap == CAP_INVALID_HANDLE) {
        ctx->rax = VM_ERR_INVALID;
        goto done;
    }
    /*
     * Resolve the exact supplied capability in the caller's address
     * space. Userspace never gets the underlying VMO handle.
     */
    if (kcapset_resolve_handle(caller->address_space->capset,
                               vmo_cap,
                               CAP_TYPE_VMO,
                               CAP_RIGHT_VMO_MAP,
                               &vmo_obj_handle) != 0) {
        ctx->rax = VM_ERR_PERMISSION;
        goto done;
    }

    vmo_handle = (vmo_handle_t)vmo_obj_handle;
    // try and grab the VMO descriptor
    vmo_t vmo_desc;
    if (kvmo_get(vmo_handle,&vmo_desc) != 0) {
        ctx->rax =VM_ERR_NOT_MAPPED;
	goto done;
    }

    if (kvmo_unmap_at(vmo_handle, caller->address_space, va) != 0) {
        ctx->rax = VM_ERR_ADDRESS;
        goto done;
    }

    ctx->rax = VM_OK;
done:
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
    thread_t *caller = thread_current();
    cap_handle_t cap = (cap_handle_t)ctx->rdi;

    cap_t resolved_cap;

    if(kcapset_resolve_record(caller->address_space->capset,cap,CAP_RIGHT_DESTROY,&resolved_cap) != 0) {
      ctx->rax = (uint64_t)EPERM; // TODO: do we want to consider returning a different value if the cap DOES exist but not with that flag vs doesn't etc?
      return syscall_return();
    }

    // if we get here, they actually have the cap AND they have the right to destroy it

    // first, try to destroy the object
    if(kcap_destroy_obj(cap) != 0) {
       ctx->rax = (uint64_t)EINTERNAL; // TODO: find a better set of error values to use
       return syscall_return();
    }

    // now we got here, we should be able to remove it from the caller's capset
    // if THIS fails somehow, fuckery is afoot

    if (kcapset_delcap(caller->address_space->capset, cap) != 0) {
        ctx->rax = (uint64_t)EINTERNAL;
        return syscall_return();
    }

    // if we got here, kcapset_delcap() should have handled global removal for us

    ctx->rax = 0; // TODO: again, we should have some better error numbers - perhaps a set specifically for the caps subsystem
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_REMOVE) {
	thread_t*    caller = thread_current();
	cap_handle_t cap    = (cap_handle_t)ctx->rdi;

	cap_t resolved_cap;

        if(kcapset_resolve_record(caller->address_space->capset,cap,CAP_RIGHT_REMOVE,&resolved_cap) != 0) {
           ctx->rax = (uint64_t)EBADHANDLE;
           return syscall_return();
        }

	// if we got here, we can remove the cap globally, but we should NOT destroy the underlying object!
	if(kcap_destroy(cap) != 0) {
	   ctx->rax = (uint64_t)EINTERNAL;
	   return syscall_return();
	}

	// now we've removed it globally, drop it from the caller's capset too
	if(kcapset_delcap(caller->address_space->capset, cap) != 0) {
           ctx->rax = (uint64_t)EINTERNAL;
	   return syscall_return();
	}

	// and now we're done
	ctx->rax = 0;
	return syscall_return();
}

/*
 * Naming applies to the exact capability handle, regardless of its object
 * type.  Resolve the capset member first so an otherwise-global handle cannot
 * be named by an address space that does not hold it.
 *
 * CAP_SETNAME: RDI=cap, RSI=len, RDX/R10/R8/R9=name bytes.
 * CAP_GETNAME: RDI=cap, RSI=output capacity; returns RSI=len and the bytes in
 * RDX/R10/R8/R9.  No userspace pointers cross this ABI.
 */
SHARKIX_SYSCALL_IMPL(CAP_SETNAME) {
    thread_t *caller = thread_current();
    cap_t cap;
    char name[KCAP_NAME_MAX] = { 0 };
    size_t len = (size_t)ctx->rsi;

    if (!caller || !caller->address_space || len > KCAP_NAME_MAX ||
        kcapset_resolve_record(caller->address_space->capset,
                               (cap_handle_t)ctx->rdi,
                               CAP_RIGHT_SETNAME, &cap) != 0) {
        ctx->rax = (uint64_t)-1;
        return syscall_return();
    }

    memcpy(name + 0, &ctx->rdx, sizeof(ctx->rdx));
    memcpy(name + 8, &ctx->r10, sizeof(ctx->r10));
    memcpy(name + 16, &ctx->r8, sizeof(ctx->r8));
    memcpy(name + 24, &ctx->r9, sizeof(ctx->r9));
    ctx->rax = (uint64_t)kcap_set_name((cap_handle_t)ctx->rdi, name, len);
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(CAP_GETNAME) {
    thread_t *caller = thread_current();
    cap_t cap;
    char name[KCAP_NAME_MAX] = { 0 };
    size_t len = 0;
    size_t out_size = (size_t)ctx->rsi;
    int status;

    ctx->rdx = 0;
    ctx->r10 = 0;
    ctx->r8 = 0;
    ctx->r9 = 0;
    ctx->rsi = 0;

    if (!caller || !caller->address_space ||
        kcapset_resolve_record(caller->address_space->capset,
                               (cap_handle_t)ctx->rdi,
                               CAP_RIGHT_GETNAME, &cap) != 0) {
        ctx->rax = (uint64_t)-1;
        return syscall_return();
    }

    status = kcap_get_name((cap_handle_t)ctx->rdi, name, out_size, &len);
    ctx->rsi = len;
    if (status == 0) {
        memcpy(&ctx->rdx, name + 0, sizeof(ctx->rdx));
        memcpy(&ctx->r10, name + 8, sizeof(ctx->r10));
        memcpy(&ctx->r8, name + 16, sizeof(ctx->r8));
        memcpy(&ctx->r9, name + 24, sizeof(ctx->r9));
    }
    ctx->rax = (uint64_t)status;
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

SHARKIX_SYSCALL_IMPL(PORT_INB) {
    portio_handle_t handle;
    uint8_t value;
    portio_status_t status;

    ctx->rdx = 0;
    status = syscall_portio_resolve(ctx, CAP_RIGHT_PORTIO_READ, true, &handle);
    if (status != PORTIO_OK) {
        ctx->rax = (uint64_t)status;
        return syscall_return();
    }

    if (kportio_inb(handle, (uint32_t)ctx->rsi, &value) != 0) {
        ctx->rax = PORTIO_ERR_INVALID;
        return syscall_return();
    }

    ctx->rax = PORTIO_OK;
    ctx->rdx = value;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PORT_INW) {
    portio_handle_t handle;
    uint16_t value;
    portio_status_t status;

    ctx->rdx = 0;
    status = syscall_portio_resolve(ctx, CAP_RIGHT_PORTIO_READ, true, &handle);
    if (status != PORTIO_OK) {
        ctx->rax = (uint64_t)status;
        return syscall_return();
    }

    if (kportio_inw(handle, (uint32_t)ctx->rsi, &value) != 0) {
        ctx->rax = PORTIO_ERR_INVALID;
        return syscall_return();
    }

    ctx->rax = PORTIO_OK;
    ctx->rdx = value;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PORT_INL) {
    portio_handle_t handle;
    uint32_t value;
    portio_status_t status;

    ctx->rdx = 0;
    status = syscall_portio_resolve(ctx, CAP_RIGHT_PORTIO_READ, true, &handle);
    if (status != PORTIO_OK) {
        ctx->rax = (uint64_t)status;
        return syscall_return();
    }

    if (kportio_inl(handle, (uint32_t)ctx->rsi, &value) != 0) {
        ctx->rax = PORTIO_ERR_INVALID;
        return syscall_return();
    }

    ctx->rax = PORTIO_OK;
    ctx->rdx = value;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PORT_OUTB) {
    portio_handle_t handle;
    portio_status_t status;

    status = syscall_portio_resolve(ctx, CAP_RIGHT_PORTIO_WRITE, true, &handle);
    if (status != PORTIO_OK) {
        ctx->rax = (uint64_t)status;
        return syscall_return();
    }

    if (kportio_outb(handle, (uint32_t)ctx->rsi, (uint8_t)ctx->rdx) != 0) {
        ctx->rax = PORTIO_ERR_INVALID;
        return syscall_return();
    }

    ctx->rax = PORTIO_OK;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PORT_OUTW) {
    portio_handle_t handle;
    portio_status_t status;

    status = syscall_portio_resolve(ctx, CAP_RIGHT_PORTIO_WRITE, true, &handle);
    if (status != PORTIO_OK) {
        ctx->rax = (uint64_t)status;
        return syscall_return();
    }

    if (kportio_outw(handle, (uint32_t)ctx->rsi, (uint16_t)ctx->rdx) != 0) {
        ctx->rax = PORTIO_ERR_INVALID;
        return syscall_return();
    }

    ctx->rax = PORTIO_OK;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(PORT_OUTL) {
    portio_handle_t handle;
    portio_status_t status;

    status = syscall_portio_resolve(ctx, CAP_RIGHT_PORTIO_WRITE, true, &handle);
    if (status != PORTIO_OK) {
        ctx->rax = (uint64_t)status;
        return syscall_return();
    }

    if (kportio_outl(handle, (uint32_t)ctx->rsi, (uint32_t)ctx->rdx) != 0) {
        ctx->rax = PORTIO_ERR_INVALID;
        return syscall_return();
    }

    ctx->rax = PORTIO_OK;
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(IRQ_WAIT) {
    irq_handle_t handle = IRQ_INVALID_HANDLE;
    thread_t* caller = thread_current();

    cap_handle_t requested_cap = ((cap_handle_t)ctx->rdi);
    if(kcapset_resolve_handle(caller->address_space->capset,requested_cap,CAP_TYPE_IRQ,CAP_RIGHT_IRQ_WAIT,&handle) != 0) {
       ctx->rax = (uint64_t)IRQ_ERR_PERMISSION;
       return syscall_return();
    }
    ctx->rax = kirq_wait(handle);
    return syscall_return();
}

SHARKIX_SYSCALL_IMPL(IRQ_ACK) {
    irq_handle_t handle = IRQ_INVALID_HANDLE;
    thread_t* caller = thread_current();

    cap_handle_t requested_cap = ((cap_handle_t)ctx->rdi);
    if(kcapset_resolve_handle(caller->address_space->capset,requested_cap,CAP_TYPE_IRQ,CAP_RIGHT_IRQ_ACK,&handle) != 0) {
       ctx->rax = (uint64_t)IRQ_ERR_PERMISSION;
       return syscall_return();
    }
    ctx->rax = kirq_ack(handle);
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

SHARKIX_SYSCALL_IMPL(TEST_BENCHMARK) {
    switch (ctx->rdi) {
    case SYSCALL_BENCHMARK_READY:
        ++benchmark_ready_count;
        ctx->rax = 0;
        break;
    case SYSCALL_BENCHMARK_START:
        benchmark_start_cycles = benchmark_tsc_start();
        ctx->rax = 0;
        break;
    case SYSCALL_BENCHMARK_STOP:
        benchmark_stop_cycles = benchmark_tsc_stop();
        ctx->rax = 0;
        break;
    default:
        ctx->rax = UINT64_MAX;
        break;
    }
    return syscall_return();
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
        ctx->rax = ENOSYS;
        return syscall_return();
    }
}

uint64_t syscall_block_test_invocations(void) { return block_test_invocation_count; }
uint64_t syscall_block_test_wakes(void) { return block_test_wake_count; }
void syscall_benchmark_reset(void) { benchmark_ready_count = 0; benchmark_start_cycles = 0; benchmark_stop_cycles = 0; }
uint64_t syscall_benchmark_ready_count(void) { return benchmark_ready_count; }
uint64_t syscall_benchmark_start_tsc(void) { return benchmark_start_cycles; }
uint64_t syscall_benchmark_stop_tsc(void) { return benchmark_stop_cycles; }
