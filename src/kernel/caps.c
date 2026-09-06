#include <sharkix/kernel/caps.h>
#include <stdint.h>
#include <stdbool.h>
 
#include <libfifo/sync.h>
#include <sharkix/kernel/uthash.h>
 
static cap_handle_t next_cap_handle = 0;
static cap_t*       global_caps_table = NULL;
static fifo_mutex_t global_caps_table_lock;

// setup the global caps table
void kinit_caps(void) {
     global_caps_table = NULL;
     next_cap_handle = 1;
     fifo_mutex_init(&global_caps_table_lock);
}

// create a new cap
int kcap_create(kobject_handle_t obj_handle, cap_type_t cap_type, cap_rights_t init_rights, cap_handle_t* new_cap) {
    cap_t* cap;

    if(!new_cap) return -1;
    // TODO - find a cleaner way to handle this too
    switch(cap_type) {
 	case CAP_TYPE_IPC_ENDPOINT:
	     if(init_rights & ~CAP_IPC_VALID_RIGHTS) {  // at least one right being requested is invalid, so don't allow it!
		return -1;
	     }
	break;
	default:
		return -1;
	break;
    }

    cap = kmalloc(sizeof(*cap));
    if(!cap) return -1; // TODO - add the proper errno stuff

    memset(cap, 0, sizeof(*cap));

    fifo_spinlock_init(&cap->spinlock);
    cap->cap_handle = next_cap_handle++;
    cap->type       = cap_type;
    cap->obj_handle = obj_handle;
    cap->rights     = init_rights;

    fifo_mutex_lock(&global_caps_table_lock);
    HASH_ADD(hh, global_caps_table, cap_handle, sizeof(cap->cap_handle), cap);
    fifo_mutex_unlock(&global_caps_table_lock);
    *new_cap = cap->handle;
}

// check if a cap exists in the global caps table
bool kcap_cap_exists(cap_handle_t cap) {
     cap_t* found_cap;
     fifo_mutex_lock(&global_caps_table_lock);
     HASH_FIND(hh, global_caps_table, &cap, sizeof(cap), found_cap);
     fifo_mutex_unlock(&global_caps_table);
     if(!found_cap) return false;
     return true;
}

// destroy a cap - this is NOT the same thing as destroying the underlying object, which must be implemented by the underlying subsystem
int kcap_destroy(cap_handle_t cap) {
    cap_t* found_cap;
    retval = 0;
    fifo_mutex_lock(&global_caps_table_lock);
    HASH_FIND(hh, global_caps_table, &cap, sizeof(cap), found_cap);
    if(!found_cap) {
       retval = -1;
       goto finish;
    }
    HASH_DEL(global_caps_table, found_cap);
    kfree(found_cap);
finish:
    fifo_mutex_unlock(&global_caps_table_lock);
    return retval;
}

// derive a new cap from an existing one with only the specified subset of rights
int kcap_derive(cap_handle_t source, cap_rights_t new_rights, cap_handle_t *new_cap);

// derive a new cap from an existing one while copying over ALL rights
int  kcap_clone(cap_handle_t source, cap_handle_t *new_cap);

// create a new cap from two old ones, assuming both point to the same underlying object
// this is non-destructive, the two old caps continue to be valid unless destroyed
int  kcap_merge(cap_handle_t a, cap_handle_t b, cap_handle_t *new_cap);



// try and get the actual cap itself from the global table - or rather, a pointer to it
int kcap_getcap(cap_handle_t handle, cap_t **cap);

// allocate and create a new capset
int  kcapset_new(capset_handle_t *new_set);

// add a cap to a capset
int  kcapset_addcap(capset_handle_t set, cap_handle_t cap);

// delete a cap from a capset - the cap remains globally valid!
int  kcapset_delcap(capset_handle_t set, cap_handle_t cap);

// check if a capset has a particular cap
bool kcapset_hascap(capset_handle_t set, cap_handle_t cap);

// check if a capset has any caps of a particular type with particular rights
// if so, return it in *out
int kcapset_resolve_cap(capset_handle_t set, cap_type_t req_type, cap_rights_t req_rights, cap_t *out);

// check if a capset has any caps allowing the specified operations on a particular type, without actually obtaining it
int kcapset_check_perms(capset_handle_t set, cap_type_t req_type, cap_rights_t req_rights);
