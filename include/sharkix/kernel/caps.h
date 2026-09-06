#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <libfifo/sync.h>

#include <sharkix/kernel/uthash.h>


// handle for one individual cap within the global table
typedef uint64_t cap_handle_t;

// handle for a generic kernel object
typedef uint64_t kobject_handle_t;

// handle for a capset
typedef uint64_t capset_handle_t;

// a bitmask/bitmap for rights granted to a cap
typedef uint64_t cap_rights_t;


// standard perms (used on all object types)
#define CAP_RIGHT_NONE		 UINT64_C(0)	    /* No rights at all                      */
#define CAP_RIGHT_DELEGATE	(UINT64_C(1) << 0)  /* Can delegate this cap to another task */
#define CAP_RIGHT_DESTROY	(UINT64_C(1) << 1)  /* Can destroy the underlying object     */

// mask defining all valid rights for any generic object
// this should be updated if any reserved bits get used
#define CAP_GENERIC_VALID_RIGHTS	(CAP_RIGHT_DELEGATE | \
					 CAP_RIGHT_DESTROY)

// reserved for future standard perms
#define CAP_RIGHT_RESV2		(UINT64_C(1) << 2)
#define CAP_RIGHT_RESV3		(UINT64_C(1) << 3)
#define CAP_RIGHT_RESV4		(UINT64_C(1) << 4)
#define CAP_RIGHT_RESV5		(UINT64_C(1) << 5)
#define CAP_RIGHT_RESV6		(UINT64_C(1) << 6)
#define CAP_RIGHT_RESV7		(UINT64_C(1) << 7)
#define CAP_RIGHT_RESV8		(UINT64_C(1) << 8)
#define CAP_RIGHT_RESV9		(UINT64_C(1) << 9)

// IPC specific perms - these can overlap with other object's bits, to be precise
#define CAP_RIGHT_IPC_SEND	(UINT64_C(1) << 10)
#define CAP_RIGHT_IPC_CALL	(UINT64_C(1) << 11)
#define CAP_RIGHT_IPC_RECV	(UINT64_C(1) << 12)

// all rights that make sense for an IPC endpoint
#define CAP_IPC_VALID_RIGHTS	(CAP_GENERIC_VALID_RIGHTS | \
				 CAP_RIGHT_IPC_SEND | \
				 CAP_RIGHT_IPC_CALL | \
				 CAP_RIGHT_IPC_RECV)

// helpers
#define CAP_HAS_ALL(cap, required) \
    ((((cap)->rights) & (required)) == (required))

#define CAP_HAS_ANY(cap, required) \
    ((((cap)->rights) & (required)) != 0)

#define CAP_LACKS_ANY(cap, required) \
    (!CAP_HAS_ALL((cap), (required)))

#define CAP_LACKS_ALL(cap, required) \
    (!CAP_HAS_ANY((cap), (required)))

// "right" must be only one particular right
#define CAP_HAS(cap, right) \
    ((((cap)->rights) & (right)) != 0)

#define CAP_LACKS(cap, right) \
    ((((cap)->rights) & (right)) == 0)

#define CAP_IS_TYPE(cap, wanted_type) \
    (((cap)->type) == (wanted_type))

// is this cap this particular underlying object?
#define CAP_IS_OBJECT(cap, needed_handle) \
    (((cap)->obj_handle) == (needed_handle))

// are these two caps referring to the same underlying object?
#define CAPS_SAME_OBJECT(a, b) \
    ((a)->obj_handle == (b)->obj_handle)

// eventually this will have all the different kernel object types, but for now it's only about IPC endpoints
typedef enum cap_type_t {
	CAP_TYPE_IPC_ENDPOINT = 1,
} cap_type_t;

// represents an inividual cap
typedef struct cap_t {
	cap_handle_t	 cap_handle; // handle for this particular cap
	cap_type_t       type;       // what kind of object is this cap for?
	kobject_handle_t obj_handle; // handle for the underlying object
	cap_rights_t     rights;     // bitmap of rights held to that underlying object

	fifo_spinlock_t  spinlock;   // used for critical updates (duh)
        UT_hash_handle   hh;	     // uthash stuff
} cap_t;

// represents a set of caps, such as those held by a particular task
typedef struct capset_t {
	capset_handle_t	capset_handle;  // handle for this set
	cap_handle_t*	cap_handles;    // the actual caps inside it, or at least their handles
	fifo_spinlock_t spinlock;	// duh
	UT_hash_handle  hh;		// duh
} capset_t;

// setup the global caps table
void kinit_caps(void);

// create a new cap
int  kcap_create(kobject_handle_t, cap_rights_t init_rights, cap_handle_t* new_cap);

// destroy a cap - this is NOT the same thing as destroying the underlying object, which must be implemented by the underlying subsystem
int  kcap_destroy(cap_handle_t cap);

// derive a new cap from an existing one with only the specified subset of rights
int  kcap_derive(cap_handle_t source, cap_rights_t new_rights, cap_handle_t *new_cap);

// derive a new cap from an existing one while copying over ALL rights
int  kcap_clone(cap_handle_t source, cap_handle_t *new_cap);

// create a new cap from two old ones, assuming both point to the same underlying object
// this is non-destructive, the two old caps continue to be valid unless destroyed
int  kcap_merge(cap_handle_t a, cap_handle_t b, cap_handle_t *new_cap);

// check if a cap exists in the global caps table
bool kcap_cap_exists(cap_handle_t cap);

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
