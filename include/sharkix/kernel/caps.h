#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <sharkix/kernel/sync.h>



// handle for one individual cap within the global table
typedef uint64_t cap_handle_t;

// handle for a generic kernel object
typedef uint64_t kobject_handle_t;

// handle for a capset
typedef uint64_t capset_handle_t;

// a bitmask/bitmap for rights granted to a cap
typedef uint64_t cap_rights_t;

// standard handles

#define CAP_INVALID_HANDLE ((cap_handle_t)UINT64_MAX) /* invalid handle                     */
#define CAPSET_HANDLE_SELF 0                          /* the capset for the current process */


#include <sharkix/kernel/uthash.h>

// standard perms (used on all object types)
//
// FUTURE GWEN, FUTURE DEVS, AI AGENTS, READ THIS CAREFULLY:
//
// NOTE - the address space is the security domain, not threads
//        threads in the same address space can fuck with each other
//        THERE IS NO SUCH THING AS A CAPSET THAT IS UNIQUE TO ONE THREAD
//        IF ONE THREAD IN AN ADDRESS SPACE CAN DO SOMETHING, SO CAN ALL OTHERS
//        EVEN IF A THREAD SOMEHOW ACQUIRES A LOCAL CAPSET, OTHER THREADS IN THE SAME ADDRESS SPACE CAN JUST STEAL IT
//        THE KERNEL ENFORCES AT ADDRESS SPACE LEVEL, NOT THE THREAD LEVEL OR THE CAPSET LEVEL
//        BY CONVENTION, EVERY ADDRESS SPACE GETS ONE CAPSET
//        WE MIGHT LATER ALLOW CONSTRUCTING CAPSETS IN USERSPACE, BUT THEY WILL BE UNIQUE kobject OBJECTS
//        AND THEY WILL ALSO BE TRANSFERRABLE
//
//
#define CAP_RIGHT_NONE		 UINT64_C(0)	    /* No rights at all                                      */
#define CAP_RIGHT_TRANSFER	(UINT64_C(1) << 0)  /* Can transfer this cap to another task's capset        */
#define CAP_RIGHT_FORWARD	(UINT64_C(1) << 1)  /* Can forward this cap as-is OR use it locally not both */
#define CAP_RIGHT_DERIVE	(UINT64_C(1) << 2)  /* Can derive another cap from this cap                  */
#define CAP_RIGHT_DESTROY	(UINT64_C(1) << 3)  /* Can destroy the underlying object                     */
#define CAP_RIGHT_REMOVE	(UINT64_C(1) << 4)  /* Can remove the cap - this removes it globally         */

// mask defining all valid rights for any generic object
// this should be updated if any reserved bits get used
#define CAP_GENERIC_VALID_RIGHTS	(CAP_RIGHT_TRANSFER | \
					 CAP_RIGHT_FORWARD | \
					 CAP_RIGHT_DERIVE | \
					 CAP_RIGHT_DESTROY | \
					 CAP_RIGHT_REMOVE)

// reserved for future standard perms
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

// physical-memory specific perms
#define CAP_RIGHT_PMEM_MAP	(UINT64_C(1) << 10)
#define CAP_RIGHT_PMEM_READ	(UINT64_C(1) << 11)
#define CAP_RIGHT_PMEM_WRITE	(UINT64_C(1) << 12)
#define CAP_RIGHT_PMEM_EXEC	(UINT64_C(1) << 13)
#define CAP_RIGHT_PMEM_DERIVE	(UINT64_C(1) << 14)
#define CAP_RIGHT_PMEM_MERGE	(UINT64_C(1) << 15)

// all rights that make sense for a physical memory region
#define CAP_PMEM_VALID_RIGHTS	(CAP_GENERIC_VALID_RIGHTS | \
				 CAP_RIGHT_PMEM_MAP | \
				 CAP_RIGHT_PMEM_READ | \
				 CAP_RIGHT_PMEM_WRITE | \
				 CAP_RIGHT_PMEM_EXEC | \
				 CAP_RIGHT_PMEM_DERIVE | \
				 CAP_RIGHT_PMEM_MERGE)

// virtual-memory-object specific perms
#define CAP_RIGHT_VMO_MAP       (UINT64_C(1) << 10)
#define CAP_RIGHT_VMO_READ      (UINT64_C(1) << 11)
#define CAP_RIGHT_VMO_WRITE     (UINT64_C(1) << 12)
#define CAP_RIGHT_VMO_EXEC      (UINT64_C(1) << 13)

#define CAP_VMO_VALID_RIGHTS    (CAP_GENERIC_VALID_RIGHTS | \
                                 CAP_RIGHT_VMO_MAP | \
                                 CAP_RIGHT_VMO_READ | \
                                 CAP_RIGHT_VMO_WRITE | \
                                 CAP_RIGHT_VMO_EXEC)

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

typedef enum cap_type_t {
	CAP_TYPE_IPC_ENDPOINT = 1,
	CAP_TYPE_PMEM         = 2,
	CAP_TYPE_VMO          = 3,
} cap_type_t;

// represents an inividual cap
typedef struct cap_t {
	cap_handle_t	 cap_handle; // handle for this particular cap
	cap_type_t       type;       // what kind of object is this cap for?
	kobject_handle_t obj_handle; // handle for the underlying object
	cap_rights_t     rights;     // bitmap of rights held to that underlying object

        UT_hash_handle   hh;	     // uthash stuff
} cap_t;


typedef struct capset_entry_t {
    cap_handle_t   cap_handle;
    UT_hash_handle hh;
} capset_entry_t;

typedef struct capset_t {
    capset_handle_t capset_handle;
    capset_entry_t *caps;

    kspinlock_t spinlock;
    UT_hash_handle  hh;
} capset_t;

// setup the global caps table
void kinit_caps(void);

// create a new cap
int  kcap_create(kobject_handle_t obj_handle, cap_type_t cap_type, cap_rights_t init_rights, cap_handle_t* new_cap);

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

// get a copy of a cap's descriptive data from the global table
int kcap_getcap(cap_handle_t handle, cap_t *out);

// allocate and create a new capset
int  kcapset_new(capset_handle_t *new_set);

// destroy a capset and its membership entries; referenced caps remain valid
int  kcapset_destroy(capset_handle_t set);

// add a cap to a capset
int  kcapset_addcap(capset_handle_t set, cap_handle_t cap);

// delete a cap from a capset - the cap remains globally valid!
int  kcapset_delcap(capset_handle_t set, cap_handle_t cap);

// check if a capset has a particular cap
bool kcapset_hascap(capset_handle_t set, cap_handle_t cap);

// check if a capset has any caps of a particular type with particular rights
// if so, return it in *out
int kcapset_resolve_cap(capset_handle_t set, cap_type_t req_type, cap_rights_t req_rights, cap_t *out);

// check if a capset has this exact cap, of this exact type, with these required rights
// if so, return the object handle it points to in *out, otherwise *out is left unaltered
int kcapset_resolve_handle(capset_handle_t set, cap_handle_t cap, cap_type_t required_type, cap_rights_t required_rights, kobject_handle_t *out);

// check if a capset has any caps allowing the specified operations on a particular type, without actually obtaining it
int kcapset_check_perms(capset_handle_t set, cap_type_t req_type, cap_rights_t req_rights);
