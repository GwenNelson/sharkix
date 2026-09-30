#include <sharkix/kernel/subsystems/as.h>

#include <sharkix/kernel/kmalloc.h>

static as_t *address_spaces;
static as_handle_t next_as_handle;
static kmutex_t address_spaces_lock;

static as_t *kas_find_locked(as_handle_t handle)
{
	as_t *as = NULL;
	uint32_t hashv = (uint32_t)handle;

	HASH_FIND_BYHASHVALUE(hh, address_spaces, &handle, sizeof(handle), hashv, as);
	return as;
}

void kas_init(void) {
	address_spaces = NULL;
	next_as_handle = 1;
	kmutex_init(&address_spaces_lock);
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
	kmutex_unlock(&address_spaces_lock);

	*out = as->handle;
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
	kmutex_unlock(&address_spaces_lock);

	address_space_release(as->address_space);
	kfree(as);
	return 0;
}
