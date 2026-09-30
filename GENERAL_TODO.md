# Sharkix TODO / Architecture Notes

This file tracks remaining work only. Completed work is removed rather than
marked done.

# NEXT CODING SESSION

## 1. Finish the load-only ELF path

The basic flat-binary ring3 ELF loader works: it parses/validates ELF64, maps
`PT_LOAD` segments into a supplied address space, returns the entry point, and
has successfully transferred control to a loaded ELF entry point.

Finish the remaining boring ELF semantics. The next concrete task is BSS /
partial-page handling using the new anonymous-VMO syscall:

- give the ELF loader a `CAP_TYPE_FACTORY_VMO` capability; possession of this
  exact typed cap is currently the creation authority (no factory rights bits
  or resource quota yet);
- use `VM_CREATE_ANON` to obtain zero-filled anonymous VMO caps for BSS
  (`p_memsz > p_filesz`);
- keep full file-backed pages mapped directly from the source ELF VMO;
- for a partial file/BSS boundary page, create a private zero-filled one-page
  anonymous VMO, map it temporarily into the loader, copy only the valid ELF
  file bytes into it, leave the remainder zero, then map it into the target;
- map remaining pure-BSS full pages from zero-filled anonymous VMO backing;
- remember that the mixed boundary page may itself begin at a non-page-aligned
  `p_vaddr`; calculate both the page start and the valid file-byte subrange
  correctly;
- preserve ELF load congruence: `p_offset % PAGE_SIZE == p_vaddr % PAGE_SIZE`;
- preserve the rule: do not copy a page unless the desired process page differs
  from the corresponding source ELF VMO page;
- preserve the rule that ELF `PF_R/PF_W/PF_X` requests cannot manufacture
  authority not present in the supplied source/target capabilities;
- keep ELF logical content length separate from the page-rounded source VMO
  extent;
- finish failure-path cleanup for partially constructed images;
- add a few deliberately malformed ELF cases to exercise bounds/overflow and
  segment validation.

Do not turn this into demand paging or a process loader. Load and launch remain
separate operations.

## 2. Generic launch/bootstrap path

Separately from ELF loading:

- define a simple standard Sharkix initial userspace stack/bootstrap format;
- allow a launcher to place the target's initial capability information on
  that stack;
- create the initial thread with:
  - RIP = ELF entry point returned by the loader
  - RSP = launcher-created initial stack
- start the thread only after loading and bootstrap setup are complete.

The existing named-cap bootstrap scheme is the starting point. Do not invent
a more elaborate ABI unless a concrete need appears.

# DRIVER ARCHITECTURE AFTER ELF

## Read the current driver architecture first

- Read and review `docs/DRIVERS.md`.
- Audit the existing ring0 portions under `src/drivers/`.
- Use the existing drivers as evidence for what the future metadata actually
  needs to express.

Classify current ring0 operations into:

``` text
declarative policy/configuration
    - named dependencies
    - required rights
    - objects/services provided
    - readiness dependencies

generic launch/init machinery
    - registry lookup
    - object creation
    - cap derivation/restriction
    - address-space/task creation
    - bootstrap-cap construction
    - ELF loading
    - stack construction
    - thread startup

genuine kernel mechanisms
    - creation/delegation of kernel-backed resources
    - IRQ authority
    - PortIO authority
    - PMEM/VMO/AS/thread objects
    - other privileged object factories
```

Do not refactor this while auditing it. First establish what the code actually
does.

## Driver metadata

Figure out a sane metadata format that can be read directly from an ELF
without needing to execute driver code.

Derive it from the audit rather than designing a speculative framework.

Likely things metadata may need to describe:

- named dependencies;
- required capability rights;
- kernel/resource objects required;
- IPC/notification objects to create;
- registry names/services provided;
- bootstrap capabilities;
- lifecycle/readiness dependencies.

The goal is NOT to encode today's bespoke ring0 setup procedures into
metadata.

The goal is:

``` text
driver-specific ring0 setup operation
    ↓
can this be declarative?
    ├─ yes -> metadata
    └─ no
        ↓
what small generic facility would make it declarative?
```

Prefer adding a generic facility over keeping permanent per-driver ring0 setup
code.

Do NOT turn driver metadata into an OOP framework, IDL system, package
manager, general-purpose graph language, or executable configuration
language.

## Generic object/resource factories

Where the audit shows that init needs to manufacture privileged objects,
provide small generic capability-controlled facilities.

Potential examples include:

``` text
create address space
create task/thread
create VMO
create VMO set
create IPC endpoint
create notification
obtain/delegate IRQ authority
obtain/delegate PortIO authority
```

Factory authority grants creation authority. It does not permanently own
everything created through it.

Add only the factories that have real consumers.

### Resource accounting / limits

Do not make resource limits part of the initial factory-capability mechanism.
For now, a factory cap answers only whether its holder has authority to create
the relevant object.

Later, add a resource-accounting / quota mechanism when there is a concrete
need to limit consumption such as anonymous VMO memory. Keep this conceptually
separate from basic creation authority.

Questions to resolve when implementing it include:

- what owns or identifies a resource budget/accounting domain;
- whether limits are attached to factory caps, separate budget objects, or
  another accounting authority;
- how delegation and cap derivation interact with budgets;
- when resources are charged and released, including objects that outlive the
  cap/factory that created them;
- how shared pages, future COW, and other shared backing are accounted;
- whether different resources need separate limits rather than one generic
  quota.

Do not add quota fields to the generic capability structure merely because VMO
creation can consume memory. First establish the simple factory-cap model, then
add accounting when real consumers make the required semantics clear.

## Migrate drivers to ELF

Once the ELF loader and generic launch path work:

- migrate existing ring3 drivers/services from flat binaries to normal ELF
  executables;
- replace bespoke ring0 launch/setup code with metadata + generic facilities;
- migrate incrementally rather than requiring a flag-day conversion;
- keep the early/kernel debug console independent of the late userspace
  driver path;
- remove flat-binary/bootstrap-specific driver launch scaffolding once
  nothing needs it;
- goal: ordinary drivers should not require `src/drivers/*/kernel` setup
  modules merely to be launched.

# INIT / SERVICE MANAGER

Build a deliberately boring ring3 init/service manager after the ELF loader
and driver metadata model are usable.

Initial responsibilities:

``` text
read boot/service configuration and ELF metadata
    ↓
determine required services/drivers
    ↓
resolve dependencies
    ↓
create/obtain required resources
    ↓
construct restricted initial capability set
    ↓
load ELF
    ↓
construct standard bootstrap stack
    ↓
start task
    ↓
wait for READY where required
    ↓
start dependants
```

Keep mechanism and policy separate.

Configuration determines which initial capabilities a service receives.
Drivers should not need arbitrary global authority to discover or manufacture
resources.

Do not overdesign supervision initially. First version needs deterministic
startup, clear failure reporting, and readiness/dependency handling.

Once ELF + init are the normal userspace boot path:

- remove obsolete bootstrap launch paths;
- remove temporary flat-binary driver machinery except anything genuinely
  required to bootstrap the ELF loader itself;
- remove hard-coded driver startup ordering;
- consolidate duplicated initial-cap setup.

# REMAINING OBJECT LIFETIME / OWNERSHIP WORK

Do this as required by the ELF/init work rather than as an open-ended
refactoring project.

Useful distinction:

``` text
handles identify
caps authorize
refs keep REFCOUNTED objects alive
```

Do NOT invent a universal kobject framework merely because several subsystems
use handles.

## IPC / PUBSUB

Core ordinary endpoint lifetime/refcounting is working.

Remaining PUBSUB lifetime/policy work:

- unsubscribe;
- subscription-list lifetime and cleanup;
- publisher/subscriber destruction cleanup;
- safe traversal once subscription nodes can be removed/freed;
- publisher backlog / active-publication state;
- track which subscribers have received the current publication;
- RELIABLE / TIMEOUT / LOSSY backpressure policies;
- missed-publication accounting/status;
- preserve ordering during progressive delivery.

Current simple fan-out may remain simple until a concrete consumer requires
the rest.

## IPC registry

Registry currently names IPC rendezvous points.

Future work, preferably driven by init/metadata requirements:

- keep IPC rendezvous naming distinct from the generic named-object registry
  unless experience later shows they genuinely want to converge;
- EPHEMERAL vs PERSISTENT registrations if actually needed;
- rebinding/reanimation of service names;
- authorization for rebinding.

Do not solve rebind authorization prematurely.

## PMEM / VMO / VMO sets

Anonymous zero-filled VMOs now exist as a distinct kernel object/construction
path. Keep their external semantics independent of how physical backing is
allocated.

Remaining work:

- PMEM should be refcounted where live PMEM-backed VMOs depend on it;
- a PMEM-backed VMO retains its backing PMEM;
- `kvmo_destroy()` must destroy/release the backing PMEM and free its physical pages when the VMO has `owns_pmem == true`; non-owning VMOs must leave their backing PMEM alone;
- VMOs should be refcounted where persistent relationships depend on them;
- VMO-set entries/mappings retain the VMOs they depend upon;
- an address space retains its VMO set;
- give VMO sets sensible ownership semantics because they are intended to be
  transferable/configurable objects.

### Physical allocator / fragmentation

The current physical allocator can require a contiguous run for multi-page
allocations. This is acceptable as a temporary implementation detail, but
anonymous VMOs must not permanently require one physically contiguous extent.

Later, rework the physical page allocator around a buddy allocator (or an
equally simple allocator that provides the same useful properties):

- maintain free blocks by power-of-two order;
- split larger blocks to satisfy smaller allocations;
- coalesce free buddies on release;
- keep useful global free-page accounting;
- preserve order-0/single-page allocation;
- retain a way to request genuinely contiguous physical memory where hardware
  or another concrete consumer requires it.

Once that exists, allow anonymous VMOs to be backed by multiple contiguous
physical extents/buddy blocks rather than requiring one giant run. A VMO
remains one contiguous logical byte range regardless of physical fragmentation.
Prefer an extent representation over one bookkeeping object per page when a
contiguous block is available.

Do NOT block the current ELF/BSS work on replacing the allocator.

## Threads

If threads become exposed handle/cap objects, give them sensible lifetime
semantics.

``` text
DEAD != FREE
```

Review raw `thread_lookup()` pointer lifetime when this becomes relevant to
task/thread capabilities and generic launch.

## PortIO / sync / CPU objects

Keep these simple.

- Do not add target refcounting to PortIO merely because it has a handle.
- Embedded mutexes/semaphores remain owned by their containing subsystem
  unless they later become independently exposed.
- CPU objects are likely canonical/permanent; caps authorize operations but
  do not imply target refcounting.

# CONSOLE / INPUT FOLLOW-UP

Do this after the ELF/driver/init work unless it becomes necessary sooner.

- hook up `console.input`;
- eventually define a generic keyboard-event representation above
  device-specific drivers;
- translate PS/2 scan codes to generic key events in the PS/2-specific layer
  or an immediately adjacent PS/2 decoder;
- keep keyboard layout / character / Unicode policy above hardware-specific
  scan-code or HID decoding;
- decide input routing/ownership only when there is a real consumer;
- runtime console selection/configuration if still useful;
- keep the early/late console boundary explicit and boring.

Late userspace console output uses `console.output` PUBSUB. Early Bochs E9
debug output remains independent.

# OTHER IMPORTANT WORK

## Capability transfer

Eventually support sending capabilities over IPC once the object/cap-transfer
semantics underneath it are clear.

Conceptually:

``` text
SYS_IPC_SEND_CAPS
SYS_IPC_RECV_CAPS
```

Do not settle the exact syscall family prematurely.

## Error numbers

Replace generic `-1` returns with meaningful typed/defined Sharkix errors.

## Initrd

Add initrd support when the ELF/init path has a concrete need for packaged
userspace executables/configuration.

## DDK / out-of-tree drivers

Clean up libsharkix/DDK support and out-of-tree driver builds after the normal
ELF driver model exists.

## Portability

Continue architecture/platform separation as real portability work demands
it. Do not refactor merely for symmetry.

## Personality layer

Start the personality layer only after ordinary ELF loading, generic program
launch, and the normal userspace service/driver boot path are established.

# LATER: USERSPACE EXCEPTION / PAGE-FAULT HANDLING

DO NOT IMPLEMENT THIS YET.

Keep the idea, but defer design/implementation until the ordinary ELF,
address-space and personality machinery gives it a real consumer.

Desired high-level rule remains:

``` text
userspace exception
    ↓
authorized handler already waiting?
    ├─ yes -> delegate fault and suspend faulting thread
    └─ no  -> kill task
```

Potential future model:

- separate exception-source object rather than pretending CPU exceptions are
  IRQs;
- opaque one-shot fault token/context;
- handler normally lives in another address space;
- handler repairs state using ordinary Sharkix VM mechanisms;
- successful resume consumes the fault token;
- no queued unresolved faults waiting indefinitely for a handler;
- notifications need not be involved unless a concrete use appears.

Do not design pager chaining, large-scale concurrent fault queues, or fancy
personality semantics before the boring ELF loader works.

# DEVELOPMENT ORDER

``` text
FINISH ELF BSS / PARTIAL-PAGE HANDLING USING ANONYMOUS VMOS
    ↓
STANDARD STACK / CAP BOOTSTRAP + GENERIC LAUNCH
    ↓
READ docs/DRIVERS.md + AUDIT EXISTING RING0 DRIVER SETUP
    ↓
DERIVE ELF-READABLE DRIVER METADATA FORMAT
    ↓
ADD ONLY THE GENERIC FACTORIES/FACILITIES THE AUDIT REQUIRES
    ↓
MIGRATE DRIVERS/SERVICES TO NORMAL ELF STARTUP
    ↓
BORING INIT + DEPENDENCY-ORDERED STARTUP
    ↓
REMOVE OBSOLETE DRIVER-SPECIFIC RING0 / FLAT-BINARY PATHS
    ↓
CONSOLE.INPUT / GENERIC KEY EVENTS AS NEEDED
    ↓
BUDDY PHYSICAL ALLOCATOR / SCATTERED ANON-VMO BACKING WHEN IT BECOMES WORTH IT
    ↓
PERSONALITY LAYER
    ↓
PUBSUB POLICY / CAP TRANSFER / OTHER FOLLOW-UP AS CONSUMERS REQUIRE
    ↓
FANCY EXCEPTION/PAGER STUFF
```

# GENERAL SHARKIX RULES

Keep Sharkix stupid where stupid works.

Do NOT add:

- universal abstractions without a concrete consumer;
- refcounts to permanent/canonical objects;
- rights merely because a mechanism can theoretically be subdivided;
- IPC when what is actually wanted is readiness notification;
- notifications when actual message data needs transporting;
- fake IPC endpoints merely to represent hardware IRQs;
- a giant generic kobject framework just because several subsystems use
  handles;
- driver-specific ring0 setup code when a small generic facility can express
  the same requirement;
- abstractions merely because there is space for them.

Prefer:

``` text
small typed subsystems
simple uint64 handles
caps for authority
typed ref_inc/ref_dec only where lifetime actually requires it
ordinary existing syscalls reused by higher-level mechanisms
ring3 policy
minimal generic ring0 mechanism
declarative driver metadata
```

Useful rules:

> If object A stores a handle to REFCOUNTED object B beyond the current
> operation, A should normally retain B and release it when that relationship
> ends.

> A cap object normally has exactly one owner: its capset.

> Moving a cap changes ownership. Deriving/copying a cap creates a new cap.

> Notifications never know who signals them. Producers know how to signal
> notifications.

> DEAD and FREE are different states for objects where outstanding references
> can exist.

> If implementation cannot be explained in terms of the clean conceptual
> model, reconsider the implementation.

> Do not recreate the abandoned FacetOS OOP/IDL architecture.

And, critically:

> Don't implement the cool fucking pager before the boring fucking ELF loader
> works.


# GENERIC OBJECT REGISTRY

## Generic registry

Add a generic named-object registry alongside the existing IPC registry.

Do NOT replace or generalize the IPC registry yet. Keep the two concepts
separate unless experience shows they genuinely want to converge.

Initial purpose:

- provide stable names for non-IPC kernel objects/resources;
- allow init/driver metadata to refer to resources by name;
- support things such as IRQ, PortIO, VMO, notification, address-space,
  factory, or other capability-controlled objects as real consumers appear.

Conceptually:

```text
generic registry:
    name -> object handle

IPC registry:
    name -> IPC rendezvous/service endpoint


### misc stuff i forgot to add earlier

After BSS is working: add further typed factory capabilities only for objects that now have concrete userspace consumers, particularly the objects needed by init/launch. Keep each factory narrow (CAP_TYPE_FACTORY_VMO, CAP_TYPE_FACTORY_AS, CAP_TYPE_FACTORY_THREAD, etc.) rather than introducing a universal factory.
Begin designing the positive syscall table as part of bringing up the real init/userspace environment. This will require cleaning up/refactoring the existing temporary/test syscall-number assignments. Preserve the test syscalls where still useful, but move them out of the namespace/layout intended for the stable positive syscall ABI rather than designing the permanent table around bootstrap tests.
Let the requirements of init + generic ELF launch drive which factories and positive syscalls actually get implemented. Do not pre-build every conceivable factory/syscall.
