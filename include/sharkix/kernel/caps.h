#pragma once

#include <stdint.h>

#include <libfifo/sync.h>

#include <sharkix/kernel/uthash.h>


// handle for one individual cap within the global table
typedef uint64_t cap_handle_t;

// handle for a generic kernel object
typedef uint64_t kobject_handle_t;

// a bitmask/bitmap for rights granted to a cap
typedef uint64_t cap_rights_t;


// standard perms (used on all object types)
#define CAP_RIGHT_NONE		 UINT64_C(0)	    /* No rights at all                      */
#define CAP_RIGHT_DELEGATE	(UINT64_C(1) << 0)  /* Can delegate this cap to another task */
#define CAP_RIGHT_DESTROY	(UINT64_C(1) << 1)  /* Can destroy the underlying object     */

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
	cap_t*		caps;		// the actual caps inside it
	fifo_spinlock_t spinlock;	// duh
	UT_hash_handle  hh;		// duh
} capset_t;
