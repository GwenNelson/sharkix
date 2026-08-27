Basic FreeRTOS-derived core for x86_64

Kernel has only Tasks and Address spaces - Tasks can run in any address space at either ring0 or ring3, with one exception - ring3 tasks can't run in plain kernel address space

syscalls:
    int64_t is used for syscall numbers
    negative numbers are for microkernel/IPC layer
    positive numbers are dependent on the personality/subsystem layer

IPC:
    very simply, tasks can create endpoints, endpoints are simple uint64_t handles
    handles are monotonic - they only ever increase
    IPC endpoints are NOT the same thing as capabilities

    IPC messages are all arrays of 5 words passed around via registers, arbitrary buffers (for example strings etc) require other syscalls first

    for IPC syscalls sending a message:
        RAX syscall number
        RDI endpoint handle
        RSI word0
        RDX word1
        R10 word2
        R8  word3
        R9  word4

    on return, RAX is the status

    for syscalls returning a message:
        RDI = sender ID
        RSI = word 0
        RDX = word 1
        R10 = word 2
        R8  = word 3
        R9  = word 4

    SYS_IPC_CREATE
        RAX = SYS_IPC_CREATE
        (other registers reserved for now)

        returns IPC_OK in RAX on success and the new endpoint handle in RDI


    SYS_IPC_SEND
        RAX = SYS_IPC_SEND
        RDI = endpoint handle
        RSI = word0
        RDX = word1
        R10 = word2
        R8  = word3
        R9  = word4

        Semantics:
            Handle is validated
                If not valid, return IPC_ERR_ENDPOINT_INVALID
            If the FIFO has room, append the message
            Wake up a blocked receiver - if >1 receiver, unblock only 1
            Return IPC_OK
            If FIFO is full, block util space becomes available
                While blocking, if the endpoint disappears return IPC_ERR_ENDPOINT_CLOSED

        Queued message inside the FIFO contains:
            typedef struct ipc_message_t {
                enum ipc_message_type type; // SEND or CALL
                uint64_t sender_tid;
                uint64_t words[5];
            } ipc_message_t;

    SYS_IPC_RECV
        RAX = SYS_IPC_RECV
        RDI = endpoint handle
        (other registers ignored for now)
    
        Semantics:
            Handle is validated
                If not valid, return IPC_ERR_ENDPOINT_INVALID
                If valid but not owned by the thread, return IPC_ERR_NOPERM
            If there are messages on the FIFO, immediately pop one and return
                RAX = IPC_RECV_SEND or IPC_RECV_CALL
                RDI = sender thread ID
                RSI = word0
                RDX = word1
                R10 = word2
                R8  = word3
                R9  = word4

    SYS_IPC_CALL
        RAX = SYS_IPC_CALL
        RDI = endpoint handle
        RSI = word0
        RDX = word1
        R10 = word2
        R8  = word3
        R9  = word4

        Semantics:
            Handle is validated
                If not valid, return IPC_ERR_ENDPOINT_INVALID
            If the FIFO has room, append the message, otherwise block as with SYS_IPC_SEND
            Wake up the blocked receiver - if >1 receiver, unblock only 1
            Unlike SYS_IPC_SEND, block waiting for the owner to do SYS_IPC_REPLY
                While blocking, if the endpoint disappears, return IPC_ERR_ENDPOINT_CLOSED
            Wait for the other end to SYS_IPC_REPLY
            Once the other end does SYS_IPC_REPLY, return the IPC context (RAX is 0, RDI is the replying thread's ID, RSI-R9 are the words)

    SYS_IPC_REPLY
        RAX = SYS_IPC_REPLY
        RDI = thread ID to reply to
        RSI = word0
        RDX = word1
        R10 = word2
        R8  = word3
        R9  = word4

        Semantics:
            thread ID is checked as valid
                If that thread doesn't exist, return IPC_ERR_CALLER_GONE
                If that thread isn't waiting, return IPC_ERR_NOT_WAITING
                If the thread attempting SYS_IPC_REPLY isn't the owner of the endpoint the caller is blocked on, return IPC_ERR_NOPERM

            Otherwise, copy the words into the other thread's reply context and unblock it, return IPC_OK


typedef int64_t sys_result_t;

enum {
    IPC_OK                   = 0,

    IPC_ERR_INVALID          = -1,
    IPC_ERR_NOT_FOUND        = -2,
    IPC_ERR_PERMISSION       = -3,
    IPC_ERR_ENDPOINT_CLOSED  = -4,
    IPC_ERR_CALLER_GONE      = -5,
    IPC_ERR_RECEIVER_GONE    = -6,
    IPC_ERR_CANCELLED        = -7,
    IPC_ERR_NO_MEMORY        = -8,
};

VM:
    VM objects are arbitrary page-backed buffers represented by uint64_t handles
    handles are monotonic - they only ever increase

    VM objects can be mapped into one or more address spaces
    each mapping can have read/write/execute permissions

    every userspace address space has a region reserved for VM mappings
    if no address is explicitly requested, the kernel chooses a free address
    from this region

    VM objects are reference counted
    when the final reference disappears, the VM object and its backing pages
    are automatically destroyed

    a VM object can be granted to an endpoint
    for now, this means any thread running in the same address space as the
    thread which created the endpoint can map the object

    eventually endpoints will belong to processes rather than individual
    threads/address spaces, but this does not need to change the VM syscall ABI


    VM permissions:

        VM_READ
        VM_WRITE
        VM_EXEC


    VM grant flags:

        VM_GRANT_ALWAYS

            the endpoint's address space can map the object until the grant is
            explicitly revoked or the object is destroyed

        VM_GRANT_WHILE_CALLING

            the grant is only active while the granting thread is blocked in
            SYS_IPC_CALL waiting for a reply from that endpoint

            when the call finishes for any reason, all mappings created through
            this grant are immediately unmapped and the grant expires

            this includes:
                successful SYS_IPC_REPLY
                endpoint destruction
                call cancellation
                receiver failure
                caller termination


    SYS_VM_MAP
        RAX = SYS_VM_MAP
        RDI = VM object handle
              0 means create a new VM object
        RSI = requested size
              used when RDI == 0
              ignored when mapping an existing object
        RDX = requested permissions
        R10 = flags
        R8  = requested virtual address
              0 means choose automatically
        R9  = reserved

        returns:
            RAX = status
            RDI = VM object handle
            RSI = mapped virtual address
            RDX = actual size

        Semantics:
            If handle == 0:
                Validate size and permissions
                Round size up to the page size
                Create a new VM object
                Allocate its backing pages
                Allocate a handle
                Map it into the calling thread's address space
                Return its handle, virtual address and actual size

            If handle != 0:
                Validate the VM object handle
                    If not valid, return VM_ERR_INVALID

                Ignore the requested size

                Check whether the caller is allowed to map the object:
                    The object's owner may map it

                    Otherwise, there must be a valid grant to an endpoint
                    whose creator belongs to the caller's address space

                Requested permissions must be a subset of the permissions
                allowed by the object/grant
                    Otherwise return VM_ERR_PERMISSION

                Map the existing backing pages into the calling thread's
                address space

                Return the same handle, mapped virtual address and actual
                object size

            If requested virtual address == 0:
                Find a suitable free range in the address space's reserved
                VM mapping region

            If requested virtual address != 0:
                Validate that the complete mapping fits in an allowed free
                userspace VM range
                    Otherwise return VM_ERR_ADDRESS

            A successful mapping holds a reference to the VM object


    SYS_VM_UNMAP
        RAX = SYS_VM_UNMAP
        RDI = mapped virtual address
        (other registers reserved for now)

        Semantics:
            Find the VM mapping beginning at the supplied virtual address
                If no mapping begins there, return VM_ERR_NOT_MAPPED

            Unmap the entire mapping
            Invalidate the relevant TLB entries
            Release the mapping's reference to the VM object

            Partial unmapping is not supported for now

            Unmapping does NOT inherently revoke grants to the VM object

            If releasing the mapping causes the VM object's refcount to
            reach zero, automatically destroy the VM object and free its
            backing pages

            Return VM_OK


    SYS_VM_GRANT
        RAX = SYS_VM_GRANT
        RDI = VM object handle
        RSI = endpoint handle
        RDX = permissions
        R10 = grant flags
        R8  = reserved
        R9  = reserved

        Semantics:
            Validate the VM object
                If invalid, return VM_ERR_INVALID

            Validate the endpoint
                If invalid, return VM_ERR_ENDPOINT_INVALID

            Caller must own/control the VM object
                Otherwise return VM_ERR_PERMISSION

            Permissions must not exceed the permissions the caller is
            permitted to grant
                Otherwise return VM_ERR_PERMISSION

            Create a grant associating:
                VM object
                endpoint
                permissions
                grant flags

            The grant authorises any thread whose address space is the same
            address space in which the endpoint was created

            VM_GRANT_ALWAYS:
                grant remains until SYS_VM_REVOKE, SYS_VM_DESTROY, or other
                object teardown

            VM_GRANT_WHILE_CALLING:
                grant becomes usable while the granting thread is blocked
                waiting for SYS_IPC_REPLY from that endpoint

                when that call ceases to be pending, revoke the grant
                automatically and immediately unmap mappings derived from it

            Return VM_OK


    SYS_VM_REVOKE
        RAX = SYS_VM_REVOKE
        RDI = VM object handle
        RSI = endpoint handle
        (other registers reserved for now)

        Semantics:
            Validate the VM object
                If invalid, return VM_ERR_INVALID

            Caller must own/control the VM object
                Otherwise return VM_ERR_PERMISSION

            Find the grant for the supplied endpoint
                If none exists, return VM_ERR_NOT_GRANTED

            Remove the grant

            Immediately unmap every mapping whose authority was derived
            from that grant

            Invalidate the relevant TLB entries

            Release the references held by those mappings

            After SYS_VM_REVOKE returns, attempting to access any of those
            old virtual mappings behaves exactly like accessing any other
            unmapped address and causes a page fault

            If removal of mappings causes the VM object's refcount to reach
            zero, automatically destroy the object

            Return VM_OK


    SYS_VM_DESTROY
        RAX = SYS_VM_DESTROY
        RDI = VM object handle
        (other registers reserved for now)

        Semantics:
            Validate the VM object
                If invalid, return VM_ERR_INVALID

            Caller must own/control the VM object
                Otherwise return VM_ERR_PERMISSION

            Mark the VM object as being destroyed so no new mappings, grants
            or references can be created

            Remove every grant associated with the VM object

            Immediately unmap every mapping of the VM object from every
            address space, including the caller's own mappings

            Invalidate all relevant TLB entries

            Invalidate the VM object handle

            Release all mapping references and the owning reference

            Once teardown is complete:
                refcount must reach zero
                free all backing pages
                free all mapping metadata
                free all grant metadata
                free the VM object

            After SYS_VM_DESTROY returns:
                the handle is invalid
                no mappings of the object remain
                no grants to the object remain
                accessing any former mapping causes a normal page fault

            Return VM_OK


    VM object lifetime:

        creating an object creates an owning reference

        every live mapping holds a reference

        grants do not by themselves need to hold references

        SYS_VM_UNMAP:
            removes one mapping from the calling address space
            drops that mapping's reference
            does not revoke grants or affect mappings in other address spaces

        SYS_VM_REVOKE:
            removes one endpoint grant
            removes every mapping derived from that grant
            drops those mapping references

        SYS_VM_DESTROY:
            forcibly destroys the object globally
            removes every mapping from every address space
            removes every grant
            invalidates the handle
            drops all references as part of teardown

        when ordinary reference release causes refcount to reach 0:
            implicitly destroy the VM object
            remove any remaining grants and mappings
            backing pages are freed
            VM object metadata is freed

        destroying an address space implicitly removes all of its mappings
        and releases their references

        destruction of whatever owns a VM object implicitly releases its
        owning reference


    VM grant/mapping relationship:

        every mapping created using a grant records which grant authorised
        that mapping

        therefore SYS_VM_REVOKE can identify and immediately remove all
        mappings derived from that grant

        mappings created by the object's owner do not depend on a grant


typedef int64_t vm_result_t;

enum {
    VM_OK                    = 0,

    VM_ERR_INVALID           = -1,
    VM_ERR_NOT_FOUND         = -2,
    VM_ERR_PERMISSION        = -3,
    VM_ERR_NO_MEMORY         = -4,
    VM_ERR_ADDRESS           = -5,
    VM_ERR_NOT_MAPPED        = -6,
    VM_ERR_NOT_GRANTED       = -7,
    VM_ERR_ENDPOINT_INVALID  = -8,
};
