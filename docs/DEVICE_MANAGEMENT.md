Basic idea:
    Kernel manages these resources:
    Physical memory pages / regions
    VM objects (shared memory objects)
    IRQs
    Threads
    Address spaces
    IPC handles & pubsub objects etc
    Notifications
        A bitmask of events that can be used for single "a thing has happened"
        Conceptually a simple bitmap is setup for every notification object - usually should be just one per process, though any thread in the process can bind any owned notification object
            Though in this case, it usually isn't a good idea for threads to wait on the same bits at the same time as other threads
        When an event occurs, the relevant bit is set in the bitmask and based on the priority system, the kernel schedules any thread waiting on the notification with the particular mask and unblocks it
        After being unblocked, the thread's wait_on(notification_handle) returns the current bitmap
        Usually a notification object is used by the kernel to route IRQs - the thread asks the kernel "bind this IRQ i have the caps for to this bit of this notification object"
        Once a particular event is handled, usually the thread should set the bits for whatever event it's handled and inform the kernel
        So the call is basically:
            wait_on(notification_handle,events_interested_mask,events_handled_mask)
                events_interested_mask is of course just a bitmask for whatever events are being bound
                events_handled_mask is a bitmap of whatever it's just handled, the kernel will do "pending_events &= ~events_handled"
                if not all pending events are handled, the next call might immediately return, or another thread might pick it up
        IRQs can be bound by doing:
            bind_irq(notification_handle,irq,bitmask)
                when the IRQ occurs, the bitmask is then ORed into the pending_events field in the notification object, and anything waiting on those bits gets unblocked and scheduled
                the kernel does NOT guarantee that only one thread will be unblocked, it's up to the process to handle thread safety
                that said, this is done using atomic_fetch_and(&pending_events, ~handled_events), and the next thread should get it via atomic_load(&pending_events)
                    BUT.... there's still potential race conditions after the atomic load - that's up to the process
                    The kernel will lock the notification whenever doing any change to the state, but it's a simple spinlock
        And of course if a process has the relevant NOTIFY permission to the object it can do this:
                notify(notification_handle,event_mask)
                    basically does atomic_fetch_or(&pending_events, event_mask)
                notify_and_wait(notification_handle,event_mask)
                    same as notify() but blocks until ((pending_events & event_mask)==0)
                    not to be confused with wait_on() from above

    Global registry (tree format - maps to permissions/owners etc)
        A namespace similar to a VFS, organized as a hierarchial tree
        For example
            sys.hw.bus.pci.00.02.0
            sys.hw.ioports.com1
            sys.hw.irqs.4
            sys.hw.physmem.vga-text

            sys.drivers.video.vga0
            sys.drivers.input.keyboard0
            sys.drivers.network.eth0

            sys.services.console
            sys.services.filesystem
            sys.services.network
            sys.services.process

        By default sys.* is highly privileged/restricted at startup and intended for system services, drivers and hardware

        the address/name "*" alone is equivalent to the root of the whole tree when delegating or searching
        while "" is equivalent to "zero access to anything in the registry" when delegating

        user.* is meant for userspace-created objects
        For example
            user.ipc.pipes.*
                used for creating named pipes - how this space is cut up depends on the particular OS "personality"
                for example, it may just be "first-come first-served", or the OS might enforce that creating a named pipe is done by a syscall to it and the OS defines unique names
            user.ipc.notify.*
                used for named notification objects - up to the OS personality
            user.ipc.pubsub.*
                used for creating pubsub publications - again, up to the OS "personality" how this is further cut up
            user.ipc.shm.*
                used for shared memory objects, as above
            user.*
                the rest of this namespace is all up to the OS personality layer to manage how it chooses

        Every node in the tree can optionally have properties (generic K/V store) and up to 32 verbs
            properties:
                typed as:
                    char
                    uint8
                    int8
                    uint16
                    int16
                    uint32
                    int32
                    uint64
                    int64
                    bool
                    string
                    bytes
                    object handle
            verbs:
                verb     0 is always DELEGATE
                verbs  1-9 are reserved at present (for standard verbs)
                verbs  10+ are defined by whatever the node itself is
        Delegations of capabilities consist of two 64-bit words:
            uint64 object_handle
            uint64 mask of verbs allowed
                Standard verb 0 is DELEGATE - you must have this to delegate the cap to other processes
            you call a single syscall to grant or revoke whatever permissions you want
            Basically:
                Assuming you yourself have the permissions needed for delegation, you do something like this:
                    delegate(other_task,handle,rights)
                        other_task is the thread_id
                        handle is the object you're delegating
                        rights is the bitmask you're giving the other object
                    revoke(other_task,handle,rights)
                        inverse of delegate
                        but note, this is NOT transitive - if the child already delegated something, then revoking it doesn't stop it being used by the child's child
                        to stop this, don't delegate DELEGATE unless it absolutely must be passed down for some reason
                            of course, the child can still proxy stuff itself in theory, to stop THAT you can instead not delegate any acccess at all to anything except a very very tightly restricted set of caps
                            this might be useful for example if you want to give only the ability to run compute and a couple of IPC primitives - this could be used for sandboxing for example


Anonymous capabilities:
    Anonymous capabilities exist of just an object handle and bitmask of rights
    All caps are basically just anonymous capabilities anyway
    Some happen to also be in the registry too
    Basic syscalls/API - still undecided, but a couple that make sense:
        SYS_CAP_CREATE
            not sure if this even makes sense actually? except as an actual kernel API call in kernel space
            perhaps we only allow userspace to do this by merging together caps they already have, creating a new cap that's the bitwise OR of the older caps, but only for the same underlying object
        SYS_CAP_MASK
            update the permissions in the mask by applying a mask, just a bitwise AND op - it is NOT possible to obtain more perms with this
        SYS_CAP_MERGE
            create a new cap by merging multiple existing caps together into one, if they all refer to the same underlying object
            sys_cap_merge(src0,src1,src2,src3)
                returns a new merged cap
                src0,src1,src2 and src3 are deleted
        SYS_CAP_UPDATE
            like SYS_CAP_MERGE, but doesn't delete the source caps

Capability sets:
    Helper object used to make the syscall ABI simpler
    Just holds an array of capability handles, not local copies of caps
    Syscalls/API:
        SYS_CAPSET_CREATE
            creates a set of caps
        SYS_CAPSET_ADDCAPS
            adds caps to the set
            these can actually refer to the same underlying objects if wanted
            sys_capset_addcap(set,cap0,cap1,cap2,cap3)
                adds the caps to the set, duh
        SYS_CAPSET_MERGE
            merges up to 4 sets together into one
                sys_capset_merge(dest,src0,src1,src2,src3)
                    src0,src1,src2,src3 are set to either valid capset handles or SHARKIX_INVALID_HANDLE
                    if all 4 are SHARKIX_INVALID_HANDLE, this is absolutely pointless
                    dest now has the union of src0,src1,src2,src3
                    src0,src1,src2,src3 no longer exist after this
        SYS_CAPSET_UPDATE
                sys_capset_update(dest,src0,src1,src2,src3)
                    same as SYS_CAPSET_MERGE, but non-destructive
                    e.g src0,src1,src2,src3 still exist after this
        SYS_CAPSET_REVOKE_CAPS
                sys_capset_revoke(set,cap0,cap1,cap2,cap3)
                    removes the specified caps from the set
        SYS_CAPSET_REVOKE_MERGE
                sys_capset_revoke(dest,src0,src1,src2,src3)
                    same as SYS_CAPSET_MERGE, but inverse
                    in other words, any caps in the specified source sets are removed from this set, and then those other sets are deleted
        SYS_CAPSET_REVOKE_UPDATE
                as above, but doesn't delete the other capsets afterwards
        SYS_CAPSET_DELETE
                deletes the set - this is NOT the same thing as revoking the underlying caps
        SYS_CAPSET_TRANSFER
                THE method for transferring a set of caps to another task
                sys_capset_transfer(set,other_thread_id)
    Another syscall for destroying the set
    Perhaps merge with the kernel's representation of what caps a task has?



Changes needed to the kernel:
    Current objects need to be mapped to a set of handles (similar to how endpoints currently work)
    Not sure if the SYS_IPC_* and SYS_VM_* syscalls should be reworked into something like a set of generic "operate verbs on caps" API
        The alternative is to keep the syscalls as-is, but make them look at capabilities - but that's messy
        Or.... have the syscalls map to just attempting to invoke a capability verb
            this is probably cleaner
            the current representation of unique handle space per object type can be kept for the kernel API alone
            the capability layer then acts like a proxy for userspace - SYS_IPC_* just takes a capability handle and checks the bitmask of rights for that cap first
            perhaps also a generic way to invoke verbs on caps?
                this makes FacetOS-style interfaces far cleaner

Drivers:
    Split into privileged "probers" and lower-privileged userspace drivers or child probers
    As few as possible probers should exist in actual kernel space - ideally perhaps one root prober per bus for example, where possible
    The unprivileged side is a userspace process that then gets permissions/capabilities on the needed hardware resources and nodes under sys.drivers.* (but only what the prober/config allows it)
        Usually, the unprivileged side handles only 1 device - though it might be desireable in some cases for the actual bus manager to be in userland
            For example, the PS2 bus is so simple, it makes sense to have the in-kernel prober just transfer the needed IO ports and bindings in sys.drivers over immediately to a userspace process
            Whereas the PCI bus might be too complicated to do this, so the in-kernel prober will probably have to stick around
            A USB bus prober can probably be immediately delegated all it needs for talking to the controller and the namespace bindings for sys.hw.bus.usb so it can add detected devices there
    Userland probers (or indeed any userland process) can use the kernel's process manager service to spawn any needed child processes


