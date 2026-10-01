#include <sharkix/kernel/thread.h>
#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/subsystems/kthread.h>

#include <sharkix/kernel/kmalloc.h>

static kthread_t        *kthreads;
static kthread_handle_t  next_kthread_handle;
static kmutex_t          kthreads_lock;

void kthread_init(void) {
	kthreads = NULL;
	next_kthread_handle = 1;
	kmutex_init(&kthreads_lock);
}



// TODO: this is intentionally a very lazy, unfinished quick hack to be rewritten as soon as i have the energy
//       it is intended to be JUST enough for a prototype test, no more
int kthread_create(as_handle_t as, uintptr_t entry, uintptr_t stack, kthread_handle_t *out) {
	if(!out) {
		return -1;
	}

	address_space_t* aspace = NULL;
	if(kas_acquire(as,&aspace) != 0) {
		return -1;
	}

	kthread_t* new_kthread = kmalloc(sizeof(kthread_t));
	if(!new_kthread) {
		kas_release(aspace);
		return -1;
	}

	thread_create_params_t new_params = { 0 };
	new_params.entry_rip             = entry;
	new_params.initial_stack_pointer = stack;
	new_params.kernel_stack_size     = 0;  // use the default
	new_params.name                  = ""; // we don't use this yet
	new_params.priority              = THREAD_PRIORITY_NORMAL;
	new_params.argument              = NULL;

	*out = KTHREAD_INVALID_HANDLE;

	thread_t *thread = thread_create(aspace, THREAD_PRIVILEGE_USER, &new_params);
	if (!thread) {
		kas_release(aspace);
		kfree(new_kthread);
		return -1;
	}

	kas_release(aspace);
	new_kthread->thread = thread;
	new_kthread->as_handle = as;

	kmutex_lock(&kthreads_lock);
	if (next_kthread_handle == KTHREAD_INVALID_HANDLE) {
		kmutex_unlock(&kthreads_lock);
		thread_destroy_unstarted(thread);
		kfree(new_kthread);
		return -1;
	}

	new_kthread->handle = next_kthread_handle++;
	uint32_t hashv = (uint32_t)new_kthread->handle;
	HASH_ADD_BYHASHVALUE(hh,
			     kthreads,
			     handle,
			     sizeof(new_kthread->handle),
			     hashv,
			     new_kthread);
	*out = new_kthread->handle;
	kmutex_unlock(&kthreads_lock);
	return 0;
}

int kthread_start(kthread_handle_t handle) {
	kthread_t *kthread = NULL;
	uint32_t hashv = (uint32_t)handle;

	if (handle == KTHREAD_INVALID_HANDLE) {
		return -1;
	}

	kmutex_lock(&kthreads_lock);
	HASH_FIND_BYHASHVALUE(hh, kthreads, &handle, sizeof(handle), hashv, kthread);
	if (!kthread) {
		kmutex_unlock(&kthreads_lock);
		return -1;
	}

	int result = thread_start(kthread->thread);
	kmutex_unlock(&kthreads_lock);
	return result == 0 ? 0 : -1;
}

void kthread_destroy_unstarted(kthread_handle_t handle) {
	kthread_t *kthread = NULL;
	uint32_t hashv = (uint32_t)handle;

	kmutex_lock(&kthreads_lock);
	HASH_FIND_BYHASHVALUE(hh, kthreads, &handle, sizeof(handle), hashv, kthread);
	if (kthread) {
		HASH_DEL(kthreads, kthread);
	}
	kmutex_unlock(&kthreads_lock);

	if (kthread) {
		thread_destroy_unstarted(kthread->thread);
		kfree(kthread);
	}
}
