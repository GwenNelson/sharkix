# Sharkix TODO / Architecture Notes

## CURRENT STATE

This file tracks remaining work only. Completed work is removed rather
than marked done.

# NEXT CODING SESSION

# 1. Continue lifetime audit / fixes

Do this object-by-object.

Do NOT invent a universal kobject framework merely because everything
happens to have a handle.

Different object types are allowed to have different lifetime policies.

Useful distinction:

``` text
handles identify
caps authorize
refs keep REFCOUNTED objects alive
```

Not every object needs refs.

## IPC endpoints

Core endpoint lifetime/refcounting has been audited and is working.

Remaining lifetime work is around persistent relationships introduced by
PUBSUB:

-   subscription-list lifetime
-   unsubscribe
-   publisher destruction / subscription cleanup
-   eventual pending-publication state once backpressure policies exist

Do not make ordinary cap removal implicitly destroy an endpoint target.

## IPC registry

Registry names IPC rendezvous points.

For now it deals only in IPC endpoint handles, not arbitrary caps.

Current behaviour may return a stale handle after an endpoint has been
destroyed; the subsequent IPC operation safely returns NOT_FOUND.
Registry removal is a separate operation.

Future questions:

-   whether the registry eventually becomes a more generic
    capability/name registry
-   EPHEMERAL vs PERSISTENT registrations
-   rebinding/reanimation of a service name after the old service dies
-   authorization for rebinding

Do NOT solve rebind authorization yet.

## PMEM

PMEM should be refcounted.

A VMO backed by PMEM retains that PMEM object.

``` text
VMO acquires PMEM
    -> pmem_ref_inc()

VMO stops using PMEM / dies
    -> pmem_ref_dec()
```

Need ensure PMEM cannot disappear while a live VMO still relies upon it.

## VMO

VMOs should be refcounted.

Persistent relationships that rely on a VMO must keep it alive.

Examples:

-   mappings
-   VMO sets
-   relevant capabilities, depending final cap-target lifetime semantics
-   other kernel objects storing the VMO handle long-term

VMO retains its backing PMEM.

## VMO sets

VMO sets are not necessarily permanently private/embedded objects.

Future intended use includes:

``` text
userspace creates VMO set
    ↓
populates/configures it
    ↓
passes/transfers it to another address space
    ↓
new AS acquires it
```

So give them sensible ownership/lifetime semantics.

An address space retains its VMO set.

VMO-set entries/mappings retain the VMOs upon which they depend.

## Address spaces

Address spaces already have useful refcount machinery.

Turn them into proper handle/cap-accessible kobjects.

An address space owns/retains things such as:

-   page tables
-   capset
-   VMO set

Threads retain their address space.

Eventually sufficiently privileged userspace can create/configure
address spaces.

## Factory authority

Eventually add some special factory object/capability.

Concept:

``` text
factory cap
    -> authority to create selected kernel objects
```

Potential examples:

``` text
create address space
create task
create VMO
create VMO set
...
```

Factory grants creation authority.

It does NOT permanently own everything created through it.

Do not overdesign this yet.

## PortIO

KEEP IT SIMPLE.

Do not automatically add target refcounting merely because PortIO is
represented by a handle.

Caps authorize access to PortIO objects.

Need eventually decide exactly when/how PortIO object storage is
reclaimed, but don't invent machinery until an actual lifetime
requirement demands it.

## Threads

If threads become proper exposed kobjects, give them sensible lifetime
semantics.

Important:

``` text
DEAD != FREE
```

Scheduler/kernel/other relationships may still retain a dead thread
object.

Review raw `thread_lookup()` pointer lifetime eventually.

## Sync primitives

Mutexes/semaphores are currently embedded in their owning objects.

Do NOT turn them into independently refcounted kobjects merely for
architectural symmetry.

The owning subsystem is responsible for ensuring they remain alive while
waiters may access them.

Revisit only if they later become independently exposed.

## CPU objects

Likely:

-   canonical
-   one object per CPU
-   permanent/kernel lifetime
-   no target refcount
-   caps merely authorize access/operations

# 2. Notifications

AFTER lifetime work is boring and stable.

Notification is a separate generic kobject.

Basic concept:

``` text
notification
    pending bitmask
    wait()
    signal(bits)
```

Architectural rule:

> Notification never knows what signals it. Producers know how to signal
> notifications.

Possible source APIs:

``` c
kirq_bind_notification(
    irq_handle_t irq,
    notification_handle_t notification,
    uint64_t bits);

kipc_bind_notification(
    ipc_handle_t endpoint,
    notification_handle_t notification,
    uint64_t bits);
```

Persistent source binding retains notification ref.

Removing binding releases it.

## IPC notification semantics

Notification means approximately:

``` text
endpoint is readable / needs servicing
```

NOT:

``` text
one notification event for every message
```

Consumer wakes and drains endpoint using recv/try-recv semantics.

## IRQ notification semantics

Notification bit means:

``` text
IRQ source requires servicing
```

It does not need to count every interrupt occurrence.

ACK remains an IRQ-specific operation.

## PS/2 eventual use

One PS/2 bus task could wait on:

``` text
bit 0 = IRQ1
bit 1 = IRQ12
bit 2 = port1 request endpoint readable
bit 3 = port2 request endpoint readable
```

One thread, one notification wait, multiple event sources.

# 3. PUBSUB FOLLOW-UP

Basic publisher/subscriber IPC exists and works from ring3.

Keep the current first implementation simple while extending it.

Remaining work:

-   unsubscribe
-   subscription-list cleanup when publishers/subscribers are destroyed
-   define safe traversal once subscription nodes can be removed/freed
-   publisher backlog / active-publication state
-   track which subscribers have already received the current
    publication
-   backpressure policies: RELIABLE / TIMEOUT / LOSSY
-   missed-publication accounting/status
-   preserve publication ordering while allowing progressive delivery
-   revisit publication authorization only when there is a concrete need

Current simple fan-out uses non-blocking subscriber enqueue.
Full/closed/missing subscribers are skipped for now.

Do NOT turn PUBSUB into a separate universal object framework. It is
built on the IPC endpoint machinery.

# 4. CONSOLE FOLLOW-UP

Late userspace console output now uses the global `console.output`
publisher. Console drivers subscribe to it.

Bochs E9 remains an early/kernel debug console using the direct
old-fashioned path. Keep it independent of late userspace PUBSUB so it
still works when higher-level infrastructure is unavailable or broken.

Remaining console work:

-   hook up `console.input`
-   decide the input-side routing/ownership model without
    overengineering it
-   runtime console selection/configuration if still useful
-   keep the early/late console boundary explicit and boring

# 5. ELF LOADER

Do this once the infrastructure underneath it is boring.

First ELF loader should be deliberately stupid.

No demand paging required.

Initial model:

``` text
parse ELF
    ↓
create/configure address space
    ↓
create/populate/map VMOs
    ↓
construct userspace stack
    ↓
provide bootstrap caps
    ↓
create task
    ↓
start task
```

Get ordinary eager ELF loading working before clever paging.

# LATER: USERSPACE EXCEPTION / PAGE-FAULT HANDLING

DO NOT IMPLEMENT THIS YET.

Keep the idea around because it fits the eventual personality
architecture.

## Default behaviour

Kernel's default remains:

``` text
userspace exception
        ↓
is an authorized userspace handler ALREADY waiting?
        │
    ┌───┴───┐
    │       │
   YES      NO
    │       │
    ▼       ▼
delegate   kill task
```

No queue of unresolved faults waiting indefinitely for a handler that
may never appear.

If nobody has explicitly volunteered to handle the fault:

``` text
task dies
```

Simple and safe.

## Handler runs in another address space

Pager/personality handler should normally live in a DIFFERENT AS from
the faulting task.

``` text
Personality / pager AS
        ▲
        │ exception delivery
        │
Kernel ─┼──────────── target AS
        │                 │
        │                 └── faulting thread
        │
        └── fault information
```

This lets personality layers implement their own VM/exception policy
independently.

## Exception source object

Do NOT literally make CPU exceptions IRQ objects.

Instead, use a similar small-object pattern.

``` text
IRQ object
    canonical hardware source
    WAIT
    ACK
    notification-bindable

Exception source
    userspace exception interception source
    WAIT
    supplies exception parameters

Fault context/token
    represents one suspended exception instance
    permits one-shot resume
```

Possible primitive:

``` c
SYS_EXCEPTION_WAIT(exception_cap, &fault);
```

Handler blocks in advance.

When a matching user exception happens:

``` text
fault
    ↓
kernel finds authorized waiting handler
    ↓
faulting thread suspended
    ↓
kernel creates opaque fault context/token
    ↓
handler receives fault information
```

No waiter:

``` text
kill faulting task
```

## Fault information

Something roughly like:

``` c
struct fault_event {
    fault_token_t token;

    uint32_t exception;

    uintptr_t address;
    uintptr_t ip;

    uint64_t flags;
};
```

For page faults, include enough information to distinguish things such
as:

-   read/write
-   present/not-present
-   userspace
-   instruction fetch
-   reserved-bit violation

Don't overabstract this until actually implementing it.

## Resolving faults

Handler uses NORMAL Sharkix VM mechanisms.

For example:

``` c
SYS_VM_MAP(target_as_cap, vmo_cap, ...);
SYS_VM_UNMAP(...);
```

Then:

``` c
SYS_FAULT_RESUME(token);
```

Kernel makes suspended thread runnable.

Faulting instruction retries naturally.

If the handler got it wrong, it faults again.

## Killing a faulted task

Do NOT invent `SYS_FAULT_KILL` unless a concrete need appears.

Use the ordinary task-kill mechanism.

``` text
recoverable fault:
    repair state
    SYS_FAULT_RESUME(token)

unrecoverable fault:
    kill task normally
```

Task death invalidates any outstanding fault token.

## Fault token

Opaque and one-shot.

It represents:

``` text
authority to resume this particular suspended exception
```

It is NOT:

-   a raw thread pointer
-   a raw exception-frame pointer
-   permanent control over the thread

Conceptually:

``` c
fault_context {
    token;
    thread;
    saved_exception_state;
    state;
};
```

Successful resume:

``` text
SYS_FAULT_RESUME(token)
    -> consume token
    -> resume thread
```

Second attempt:

``` text
SYS_FAULT_RESUME(token)
    -> INVALID
```

If the task dies first, token becomes invalid.

## Personality-layer use

Eventually this could permit:

``` text
Linux personality
    #PF -> Linux VM / SIGSEGV-like semantics
    #UD -> SIGILL-like semantics
    #DE -> SIGFPE-like semantics
    #GP -> appropriate Unix semantics
    #BP -> debugger/signal handling

Native Sharkix personality
    -> native policy

Other personality
    -> its own policy
```

Kernel does not need to know what Unix signals are.

## Default ring3 pager

Could eventually ship a boring standard Sharkix pager in ring3.

It handles common/native VMO-backed faults.

Personality implementations could:

-   implement paging entirely themselves, or
-   delegate boring cases to the standard pager

Don't design pager chaining until actually needed.

## Pager capabilities

Pager does NOT become omnipotent simply because it handles faults.

It receives only the authority it needs.

Potentially:

``` text
target AS cap
    appropriate VM operations

relevant VMO caps
    appropriate map/read/write authority

task cap
    kill authority if permitted

fault token
    one-shot resume authority
```

Ordinary capability checks still apply.

A pager cannot turn:

``` text
read-only VMO authority
```

into:

``` text
RWX mapping
```

merely because it's handling a fault.

## Multiple simultaneous faults

Don't solve scalability prematurely.

Simple initial semantics could be:

``` text
handler A waiting
handler B waiting

thread X faults
    -> handler A receives token 100

thread Y faults
    -> handler B receives token 101

thread Z faults
    -> nobody currently waiting
    -> default behaviour: kill
```

If a personality eventually needs hundreds of concurrent unresolved
faults, solve that when it exists.

## Exceptions and notifications

Do NOT force exceptions through notifications initially.

Notification semantics are roughly:

``` text
something is ready; come service it
```

Exception interception semantics are initially:

``` text
I am already blocked here and volunteering
to take responsibility for the next matching exception
```

That distinction makes:

``` text
no waiter -> kill
```

trivial.

Exception sources can integrate with notifications later if a real use
case demands it.

# DEVELOPMENT ORDER

Current rough order:

``` text
REMAINING KOBJECT LIFETIME / OWNERSHIP CLEANUP
        ↓
ADDRESS SPACE / VMO OBJECT CLEANUP
        ↓
NOTIFICATIONS
        ↓
CONSOLE.INPUT + REMAINING CONSOLE CLEANUP
        ↓
ELF LOADER
        ↓
PERSONALITY LAYER
        ↓
PUBSUB BACKPRESSURE / POLICY WORK AS NEEDED
        ↓
FANCY EXCEPTION/PAGER STUFF
```

# GENERAL SHARKIX RULES

Keep Sharkix stupid where stupid works.

Do NOT add:

-   universal abstractions without a concrete consumer
-   refcounts to permanent/canonical objects
-   rights merely because a mechanism can theoretically be subdivided
-   IPC when what is actually wanted is readiness notification
-   notifications when actual message data needs transporting
-   fake IPC endpoints merely to represent hardware IRQs
-   a giant generic kobject framework just because several subsystems
    use handles

Prefer:

``` text
small typed subsystems
simple uint64 handles
caps for authority
typed ref_inc/ref_dec only where lifetime actually requires it
ordinary existing syscalls reused by higher-level mechanisms
ring3 policy
minimal ring0 mechanism
```

Useful rules:

> If object A stores a handle to REFCOUNTED object B beyond the current
> operation, A should normally retain B and release it when that
> relationship ends.

> A cap object normally has exactly one owner: its capset.

> Moving a cap changes ownership. Deriving/copying a cap creates a new
> cap.

> Notifications never know who signals them. Producers know how to
> signal notifications.

> DEAD and FREE are different states for objects where outstanding
> references can exist.

And, critically:

> Don't implement the cool fucking pager before the boring fucking ELF
> loader works.

# OTHER IMPORTANT WORK

## Capability transfer

Want to implement cap transfer. This means exposing appropriate
capability-controlled objects such as address spaces, threads, CPU
cores, and related objects as needed.

Then support sending capabilities over IPC, conceptually something like:

``` text
SYS_IPC_SEND_CAPS
SYS_IPC_RECV_CAPS
```

These would exchange the ordinary message words plus capabilities.

Possible deliberately simple syscall surface:

``` text
SYS_IPC_SEND_CAP
SYS_IPC_SEND_2CAPS
...
```

with nicer wrapping in libsharkix.

Do not settle the exact syscall family until the object/cap-transfer
semantics underneath it are clear.

## Error numbers

Fix the error-number system so Sharkix has meaningful typed/defined
errors instead of returning `-1` all over the place.
