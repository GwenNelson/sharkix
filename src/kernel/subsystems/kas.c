#include <sharkix/kernel/subsystems/kas.h>
#include <sharkix/kernel/memory.h>
#include <sharkix/kernel/startup.h>
#include <sharkix/kernel/thread.h>
#include <sharkix/kernel/console.h>
#include <libfifo/fifo.h>
#include <sharkix/kernel/kmalloc.h>

static as_t *address_spaces;
static as_handle_t next_as_handle;
static kmutex_t address_spaces_lock;

// 64 ought to be enough for anyone...
#define KAS_REAPER_CAPACITY 64

static fifo_t  *reaper_queue;
static as_t   **reaper_queue_storage; 


static as_t *kas_find_locked(as_handle_t handle)
{
	as_t *as = NULL;
	uint32_t hashv = (uint32_t)handle;

	HASH_FIND_BYHASHVALUE(hh, address_spaces, &handle, sizeof(handle), hashv, as);
	return as;
}

static void kas_reaper_task(void* argument) {
	(void)argument;
	for(;;) {
		as_t* dead = fifo_pop_wait(reaper_queue);
		address_space_release(dead->address_space);
		kfree(dead);
	}
}

void kas_init(void) {
	address_spaces = NULL;
	next_as_handle = 1;
	kmutex_init(&address_spaces_lock);

	reaper_queue         = kmalloc(sizeof(fifo_t));
	reaper_queue_storage = kmalloc(sizeof(as_t*)*KAS_REAPER_CAPACITY);
	if(!reaper_queue || !reaper_queue_storage) {
		console_write("kas.c:kas_init() - could not kmalloc the reaper queue! will not continue\n");
		for(;;);
	}

	fifo_init(reaper_queue,(void**)reaper_queue_storage,KAS_REAPER_CAPACITY);

	if(!startup_kernel_thread(kas_reaper_task,"kas-reaper",THREAD_PRIORITY_NORMAL)) {
		console_write("kas.c:kas_init() - could not launch reaper! will not continue\n");
		for(;;); // TODO - we really really need a proper kpanic
	}
}

int kas_register(as_handle_t *out, address_space_t *address_space) {
	as_t *as;

	if (!out || !address_space) {
		return -1;
	}

	as = kmalloc(sizeof(*as));
	if (!as) {
		return -1;
	}

	as->address_space = address_space;

	kmutex_lock(&address_spaces_lock);
	if (next_as_handle == AS_INVALID_HANDLE) {
		kmutex_unlock(&address_spaces_lock);
		kfree(as);
		return -1;
	}

	address_space_retain(address_space);
	as->handle = next_as_handle++;
	uint32_t hashv = (uint32_t)as->handle;
	HASH_ADD_BYHASHVALUE(hh,
			     address_spaces,
			     handle,
			     sizeof(as->handle),
			     hashv,
			     as);

	*out = as->handle;
	kmutex_unlock(&address_spaces_lock);
	return 0;
}

int kas_acquire(as_handle_t handle, address_space_t **out) {
	as_t *as;

	if (!out || handle == AS_INVALID_HANDLE) {
		return -1;
	}

	kmutex_lock(&address_spaces_lock);
	as = kas_find_locked(handle);
	if (!as) {
		kmutex_unlock(&address_spaces_lock);
		return -1;
	}

	address_space_retain(as->address_space);
	*out = as->address_space;
	kmutex_unlock(&address_spaces_lock);
	return 0;
}

void kas_release(address_space_t *address_space) {
	if(!address_space) {
		// TODO - maybe add debug logging here? it should never happen, perhaps just make it return an errno?
		//        for now, i'm just going to make it return immediately
		return;
	}
	address_space_release(address_space);
}

int kas_unregister(as_handle_t handle) {
	as_t *as;

	if (handle == AS_INVALID_HANDLE) {
		return -1;
	}
	
	kmutex_lock(&address_spaces_lock);
	as = kas_find_locked(handle);
	if (!as) {
		kmutex_unlock(&address_spaces_lock);
		return -1;
	}

	HASH_DEL(address_spaces, as);

	// deferred free, reaper will handle it for us, no need to free it here
	fifo_push_wait(reaper_queue, as);
	
	kmutex_unlock(&address_spaces_lock);

	// i'm leaving these here and commented out
	// to future me, other potential future devs and AI agents working on this code:
	// DO NOT UNCOMMENT THESE LINES UNLESS YOU KNOW WHAT YOU'RE DOING, DOUBLE FREE MADNESS AWAITS
	// AND IT WON'T IMMEDIATELY BE OBVIOUS IN SOME EDGE CASES EITHER
	// THIS IS HERE JUST TO DOCUMENT IT, DON'T ASK WHY
	//address_space_release(as->address_space); /* SERIOUSLY, in case you grep for just this, DO NOT UNCOMMENT IT */
	//kfree(as);				    /* I MEAN IT - DO NOT UNCOMMENT THIS LINE, AND DON'T ASK WHY IT REMAINS - I HAVE MY REASONS*/
	return 0;
}
