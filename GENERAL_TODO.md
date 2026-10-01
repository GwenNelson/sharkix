# Sharkix TODO / Architecture Notes

This file tracks remaining work only. Completed work is removed rather than
marked done.

# NEXT CODING SESSION

The userspace thread-launch milestone is now working: an authorized ring3 task can create address spaces, map payload/stack VMOs, create dormant threads, and start them. Treat `AS_CREATE`, the AS/thread factories, `THREAD_CREATE` / `THREAD_START`, and the basic `kthread_test` launch path as implemented. Do not frameworkize the test.

Before migrating long-running drivers, close the concrete correctness gaps found by the post-milestone source review, then do the bounded lifetime/destruction audit.

## 1. Fix concrete thread / ELF launch correctness issues

- fix the kthread wrapper lifetime bug: it currently keeps a `thread_t *` that may outlive the scheduler thread after `thread_reap()` frees it; a still-valid thread cap must never dereference a reaped thread. Fold this into the thread lifetime audit and preserve `DEAD != FREE`;
- make the authority rule for thread creation explicit and enforce it: creating a thread in an address space must require the intended `AS_THREAD_CREATE` (or equivalent final chosen) right rather than accepting any cap of AS type;
- writable file-backed ELF `PT_LOAD` handling now uses eager private copies rather than directly mapping the shared source ELF VMO. This deliberately avoids needing COW/private-file mapping semantics in the current VMO subsystem;
- tighten ELF admission before relying on the loader for general driver ELFs: validate required `p_offset` / `p_vaddr` page congruence; correctly handle or deliberately reject overlapping `PT_LOAD` page ranges; and validate that `e_entry` lies in an executable loaded segment;
- keep this bounded to concrete loader correctness. Do not turn it into general ELF conformance or dynamic-linker work yet.

### Later ELF / VMO optimization: private demand-paged file backing

The current eager-copy loader is intentionally the simple implementation. Later, once the VMO/page-fault machinery has a concrete reason to support it, investigate private file-backed mappings with copy-on-write / copy-on-demand semantics:

- allow clean ELF-backed pages to remain shared with the source image where their required contents are identical;
- allow initialized writable `PT_LOAD` pages to begin from shared file backing but materialize private pages on write (COW), without granting write authority to the source ELF VMO;
- materialize mixed file/BSS boundary pages on demand by copying the valid file bytes and zeroing the remainder;
- consider demand paging so untouched executable pages need not be copied/read into private memory at launch time;
- keep this as general VMO/VM/page-fault machinery rather than ELF-loader-specific cleverness.

Do not block the current loader/init work on this optimization. The eager-copy path is the intended implementation until the VM subsystem can express these semantics cleanly.

## 2. Lifetime / destruction audit

Do the deliberate, finite audit in the lifetime section below now that AS and thread creation are reachable from userspace. Prioritize reachable UAF, destruction/unregister races, cap-lock/destructor ordering, persistent references, and resource leaks. The kthread/reaper stale-pointer issue above is part of this audit, not a reason to invent a universal object framework.

Definition of done for this pass: capability-visible objects needed by init have coherent, explainable lifetime rules and no known reachable UAF/deadlock or unsafe destruction race on their implemented paths.

## 3. Finish capability handoff needed by ring3 init

A userspace init must be able to construct a restricted capability set for a new domain. The current `CAP_TRANSFER`, `CAP_FORWARD`, and `CAP_DERIVE` stubs are a concrete integration blocker for replacing kernel-profile provisioning with a real ring3 launcher.

- implement only the derivation/transfer/forward semantics actually needed by generic launch and init;
- preserve attenuation: init should be able to hand a service only the rights it needs;
- keep ownership explicit: moving a cap changes ownership; deriving/copying creates a distinct cap;
- do not design capability-over-IPC merely to solve initial bootstrap handoff if a simpler generic launch mechanism suffices.

## 4. Standard bootstrap ABI + generic ELF launch

- define a simple standard Sharkix initial userspace stack/bootstrap format;
- allow the launcher to place the target's initial capability information there;
- create the target AS through its factory and load the ELF using the corrected load-only loader;
- create the initial thread with RIP = validated ELF entry and RSP = launcher stack top;
- start it only after mappings and bootstrap state are complete.

The existing named-cap bootstrap scheme is the starting point. Do not invent a more elaborate ABI without a concrete consumer.

## 5. Bring up a boring real init

Once generic launch and cap handoff are sufficient, begin the real ring3 init path. Let real driver/service requirements drive further factories and syscalls. Do not pre-build every conceivable creation facility.

# Kernel core / subsystem boundary cleanup

After the immediate AS/thread factory and generic-launch work is green, continue
the source-boundary cleanup so `core/` knows as little as practical about
particular kernel-managed subsystems.

The intended direction is:

``` text
core/
    generic kernel machinery
    syscall entry/dispatch mechanics
    scheduler/execution machinery
    fundamental memory/architecture mechanisms

subsystems/
    capability-visible managed objects
    subsystem-specific operations and syscall implementations
    object-specific lifetime/validation policy

include/sharkix/caps.inc
    canonical capability type/right ABI declarations
```

Concrete cleanup work:

- keep syscall entry/dispatch mechanics in core, but move subsystem-specific
  syscall implementations alongside the subsystem they operate on where this
  produces a cleaner dependency boundary;
- avoid a central core syscall implementation file accumulating knowledge of
  VMO, AS, kthread, notification, IRQ, PortIO, or future subsystem internals;
- derive capability type enums, rights, valid-right masks, and type-driven
  validation/dispatch tables from the canonical `caps.inc` declarations rather
  than maintaining parallel lists in core code;
- audit remaining switches/tables in core that enumerate concrete capability
  types and either derive them from `caps.inc` or move the type-specific policy
  to the owning subsystem;
- keep capability mechanism generic: capsets/handles/derivation/transfer and
  authority checks belong in the capability machinery, while semantics of a
  particular cap target belong to its subsystem;
- prefer ordinary separately compiled subsystem translation units; do not make
  subsystem implementations textual `.c` includes merely to remove code from a
  core source file;
- do not introduce a universal kobject/OOP dispatch framework to achieve this.
  Small explicit typed subsystems plus generated ABI declarations are preferred.

Useful dependency smell test:

``` text
subsystem -> core       normal
core -> subsystem       justify carefully
core -> every concrete cap type       refactor target
```

The goal is not a mathematically pure core. Architecture, scheduler, memory,
and capability mechanisms will necessarily provide interfaces used by
subsystems. The goal is to stop generic core machinery from becoming the place
where knowledge of every Sharkix object type and syscall implementation
accumulates.

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

The VMO, AS, and thread factories required for the basic generic-launch path now exist.

Add further typed factories only when a real consumer requires them. Possible
later consumers may include VMO sets, IPC endpoints, notifications, or other
privileged resources, but do not pre-build them.

Factory authority grants creation authority. It does not permanently own
everything created through it. Keep each factory narrow rather than
introducing a universal factory.

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

## Lifetime / destruction audit

The AS_CREATE work has made previously theoretical lifetime races reachable. Do a
deliberate pass over capability-visible/refcounted subsystems as the thread/init work
requires it. Keep the audit typed and local to each subsystem rather than building a
universal lifetime framework.

General lifetime contract to aim for:

``` text
handles identify
caps authorize
refs keep REFCOUNTED objects alive

registry owns one ref while an object is registered
acquire/release pairs protect temporary operation references
persistent object relationships retain what they depend on
unregister prevents new acquisitions before dropping the registry ref
DEAD / unregistered != FREE while references remain
```

Concrete audit work:

- replace raw registry lookups that return refcounted objects with explicit
  acquire/release APIs where concurrent unregister/destruction can occur;
- for address spaces, use `kas_acquire()` / `kas_release()` as the subsystem
  lifetime API rather than exposing core memory retain/release details to callers;
- make lookup+retain atomic with respect to unregister: acquire while holding the
  registry lock; unregister removes the entry while holding that lock, then drops
  the registry reference after unlocking;
- audit IPC, VMO/PMEM, notifications, IRQ and the forthcoming kthread subsystem
  for the same raw-lookup/use-after-unregister pattern, adding typed acquire/release
  APIs only where the target actually has refcounted/destructible lifetime;
- audit persistent relationships (for example kthread -> address space and
  address space -> VMO set) so the owner retains the referenced object for the
  full lifetime of the relationship;
- do not call subsystem destructors while holding `global_caps_table_lock`; copy
  the capability type/object identity needed for dispatch while protected, then
  release the cap-table lock before entering subsystem destruction code;
- never retain or dereference a `cap_t *` obtained from the global table after
  dropping the lock unless the capability mechanism explicitly gives it a safe
  lifetime; copy the required values first;
- define and audit the distinction between `CAP_RIGHT_REMOVE` (remove this
  authority record) and `CAP_RIGHT_DESTROY` (request destruction/unregistration
  of the target object); removing a cap must not implicitly destroy its target;
- audit concurrent REMOVE/DESTROY and concurrent DESTROY/DESTROY on the same cap.
  Validation, subsystem destruction and final cap removal currently form a
  multi-stage operation and need an explicit linearization rule;
- consider a per-cap lifecycle such as `LIVE -> DESTROYING -> removed` so one
  thread can atomically claim a destructive operation without holding the global
  capability lock across arbitrary subsystem code; all resolve/remove paths would
  reject a cap in `DESTROYING`;
- do not implement rollback from `DESTROYING` to `LIVE` unless subsystem failure
  semantics guarantee that a failed destroy leaves the object intact. Prefer
  destruction/unregistration semantics where the externally visible transition is
  committed cleanly and later cleanup may drain references;
- sibling/derived caps may initially become stale after the target object is
  unregistered. A stale cap safely failing target acquisition is acceptable; do
  not add object-to-cap reverse tracking or global revocation machinery without a
  concrete requirement;
- for each destructible subsystem, document what owns its registry reference, what
  can hold temporary/persistent references, what unregister means, when new
  acquisitions stop, and what event finally frees the object.

Do this audit incrementally as real factories/objects become userspace-visible, but
do not postpone concrete reachable races. The immediate AS acquire/release and
cap-lock/destructor issues must be fixed before treating AS destruction as sound.

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

## Threads / kthread

The userspace-visible `kthread` wrapper and basic create/start syscalls now work. Keep the intended boundary:

``` text
core/thread.c          scheduler/execution machinery
subsystems/kthread.c   managed kobject/handle/lifetime wrapper
```

Remaining lifetime/correctness work:

- fix the stale `thread_t *` relationship between the kthread wrapper and the scheduler reaper; a reaped scheduler thread must not leave a dereferenceable pointer behind a valid kthread cap;
- review remaining raw `thread_lookup()` pointer lifetime and establish the appropriate acquire/state/identity contract;
- make dormant/runnable/dead/reaped transitions explicit enough that implemented operations such as `THREAD_START` fail safely in every state;
- keep CPU-core objects and CPU affinity/control authority separate from thread creation.

``` text
DEAD != FREE
```

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

## Capability derivation / handoff / transfer

The generic capability operations needed by ring3 init are now immediate work: `CAP_DERIVE`, `CAP_TRANSFER`, and `CAP_FORWARD` are currently stubbed, and init needs a safe way to construct restricted capability sets for newly launched domains. Implement the minimum semantics required by generic launch/init, with clear ownership and attenuation rules.

Capability transfer over ordinary IPC remains later work once a concrete consumer requires it. A possible eventual shape remains:

``` text
SYS_IPC_SEND_CAPS
SYS_IPC_RECV_CAPS
```

Do not settle that IPC syscall family prematurely.

## Error numbers

Replace generic `-1` returns with meaningful typed/defined Sharkix errors.

## Initrd

Add initrd support when the ELF/init path has a concrete need for packaged
userspace executables/configuration.

## DDK / out-of-tree drivers

Clean up libsharkix/DDK support and out-of-tree driver builds after the normal
ELF driver model exists.

## Early boot / architecture / portability refactor

Return to this after the immediate factory/init work. The source-tree
reorganisation is useful precisely because it makes architecture and
boot-protocol assumptions auditable file by file.

### Split Multiboot1 from x86_64 bootstrap machinery

The current Multiboot1 `boot.S` still combines protocol ABI work with x86_64
machine bootstrap work. Refactor it so the ownership boundaries are explicit.

Multiboot1-specific code should own:

- the Multiboot1 header;
- `_start`;
- receipt/preservation of the Multiboot `%eax` magic and `%ebx` information
  pointer;
- the Multiboot-specific 64-bit continuation / handoff into `mb_init.c`.

Architecture-specific bootstrap code under `src/kernel/arch/x86_64/` should
own the machinery that is not intrinsically Multiboot:

- temporary/bootstrap stack where required;
- bootstrap page tables and initial x86_64 mappings;
- enabling PAE/long mode/paging for boot paths that enter in 32-bit protected
  mode;
- temporary bootstrap GDT and the 32 -> 64 bit transition;
- switch to the normal kernel stack.

Prefer a name that states the actual contract (for example
`bootstrap32.S`) rather than implying that every x86_64 boot path must use it.
A future boot protocol that enters directly in long mode may bypass this code.

Keep the temporary bootstrap GDT distinct from the proper runtime kernel
GDT/TSS installed by normal x86_64 architecture initialization.

Make Multiboot-header placement explicit in the linker script rather than
depending on object/link order.

### Define the boot-protocol -> Sharkix handoff

Do not make generic kernel code parse a bootloader's native structures.

Define a small Sharkix-owned boot-information format/API containing the
information the generic kernel actually needs, including a normalized physical
memory map. Boot-protocol components translate their native representation
into this format before generic kernel initialization.

The boundary should become conceptually:

``` text
boot protocol
    -> protocol-specific parser/translator
    -> Sharkix boot-info / normalized memory map
    -> architecture early init as required
    -> generic kernel
```

Keep the format boring and driven by current consumers. It is an internal
kernel boot contract, not a general firmware/bootloader ABI.

### Refactor memory.c / memory.h away from Multiboot

This is expected to be a substantial job.

`core/memory.c` / its public kernel memory interfaces must stop including or
understanding Multiboot1 structures. In particular:

- define a Sharkix-specific normalized physical-memory-map representation;
- have `boot/multiboot1` translate the Multiboot memory map into it;
- make physical-memory initialization consume only the normalized Sharkix
  representation;
- remove Multiboot-specific parsing/types/includes from generic memory code;
- audit any other boot-protocol assumptions currently leaking into
  `core/memory.c`, `memory.h`, or adjacent VM initialization;
- preserve architecture-specific VM policy separately from boot-protocol
  parsing.

Do this as a deliberate refactor with the existing x86_64/Multiboot path kept
green, not mixed into unrelated subsystem work.

### BOOTBOOT / alternate boot path later

After the boot contract and architecture boundary are clean, consider adding
BOOTBOOT (and eventually other architectures such as AArch64) as a second real
consumer of those interfaces.

If BOOTBOOT already supplies long mode and usable initial mappings, do not
re-run the 32-bit x86 bootstrap merely for symmetry. Let BOOTBOOT handle the
machine transition it promises, then have Sharkix establish whatever proper
runtime x86_64 state it still requires (kernel virtual-memory policy, runtime
GDT/TSS, interrupt architecture, etc.).

The portability goal is not zero architecture-specific code. The goal is that
architecture-specific code has an obvious home and generic core/subsystem code
does not accidentally depend on x86_64 or a particular boot protocol.

## Personality layer

Start the personality layer only after ordinary ELF loading, generic program launch, and the normal userspace service/driver boot path are established.

The overall shape remains plausible for Unix personalities, but the forwarding ABI is not designed yet. When this becomes the active consumer:

- replace/relocate temporary positive-number test syscalls so the stable positive namespace can be personality-serviced cleanly;
- define per-domain personality binding and forwarding of unhandled positive syscalls to an authorized userspace personality service;
- let concrete Linux/BSD compatibility work drive process/thread, signal, VM and executable-startup contracts rather than putting those semantics into the microkernel pre-emptively;
- dynamic linking/interpreter and auxiliary-vector compatibility are later personality/loader ABI work, not blockers for controlled static driver ELFs.

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
FIX CONCRETE KTHREAD / ELF LAUNCH CORRECTNESS GAPS
    ↓
LIFETIME / DESTRUCTION / RESOURCE-LEAK AUDIT
    ↓
IMPLEMENT MINIMUM CAP DERIVE / TRANSFER / FORWARD HANDOFF NEEDED BY INIT
    ↓
KERNEL CORE / SUBSYSTEM BOUNDARY CLEANUP AS NEEDED
    ↓
STANDARD STACK / CAP BOOTSTRAP + GENERIC ELF LAUNCH
    ↓
BEGIN BORING REAL INIT; LET ITS NEEDS DRIVE FURTHER FACTORIES/SYSCALLS
    ↓
READ docs/DRIVERS.md + AUDIT EXISTING RING0 DRIVER SETUP
    ↓
DERIVE ELF-READABLE DRIVER METADATA FORMAT
    ↓
ADD ONLY FURTHER GENERIC FACTORIES/FACILITIES REAL CONSUMERS REQUIRE
    ↓
MIGRATE DRIVERS/SERVICES TO NORMAL ELF STARTUP
    ↓
DEPENDENCY-ORDERED INIT / REMOVE OBSOLETE DRIVER-SPECIFIC RING0 + FLATBIN PATHS
    ↓
CLEAN UP STABLE POSITIVE SYSCALL NAMESPACE AS PERSONALITY ABI BECOMES CONCRETE
    ↓
RETURN TO EARLY-BOOT / ARCH PORTABILITY REFACTOR:
  SPLIT MULTIBOOT1 FROM X86_64 BOOTSTRAP
  DEFINE SHARKIX BOOT-INFO + NORMALIZED MEMORY MAP
  PURGE MULTIBOOT KNOWLEDGE FROM memory.c / memory.h
    ↓
CONSIDER BOOTBOOT / SECOND BOOT PATH ON THE CLEAN INTERFACE
    ↓
CONSOLE.INPUT / GENERIC KEY EVENTS AS NEEDED
    ↓
BUDDY PHYSICAL ALLOCATOR / SCATTERED ANON-VMO BACKING WHEN IT BECOMES WORTH IT
    ↓
PERSONALITY LAYER
    ↓
PUBSUB POLICY / CAP-OVER-IPC / OTHER FOLLOW-UP AS CONSUMERS REQUIRE
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
```
