# Sharkix TODO / Architecture Notes

This file tracks remaining work only. Completed work is removed rather
than marked done.

# NEXT CODING SESSION

The userspace thread-launch milestone is now working: an authorized
ring3 task can create address spaces, map payload/stack VMOs, create
dormant threads, and start them. Treat `AS_CREATE`, the AS/thread
factories, `THREAD_CREATE` / `THREAD_START`, and the basic
`kthread_test` launch path as implemented. Do not frameworkize the test.

Before migrating long-running drivers, close the concrete correctness
gaps found by the post-milestone source review and continue the bounded,
subsystem-by-subsystem lifetime/destruction audit. The KAS pass is
complete and the currently implemented kthread
create/start/unstarted-destroy paths have been checked; do not reopen
completed passes without a concrete reason.

## 1. Fix concrete thread / ELF launch correctness issues

-   finish started-thread destruction semantics before exposing broader
    thread lifecycle control: self-destruction can use the existing
    `thread_exit_current()` + global thread reaper path, but arbitrary
    termination of another started thread needs scheduler/wait-queue
    cancellation support so no ready/sleep/IPC/notification structure
    can retain a pointer to a reaped `thread_t`; preserve
    `DEAD != FREE`;
-   make the authority rule for thread creation explicit and enforce it:
    creating a thread in an address space must require the intended
    `AS_THREAD_CREATE` (or equivalent final chosen) right rather than
    accepting any cap of AS type;
-   writable file-backed ELF `PT_LOAD` handling now uses eager private
    copies rather than directly mapping the shared source ELF VMO. This
    deliberately avoids needing COW/private-file mapping semantics in
    the current VMO subsystem;
-   tighten ELF admission before relying on the loader for general
    driver ELFs: validate required `p_offset` / `p_vaddr` page
    congruence; correctly handle or deliberately reject overlapping
    `PT_LOAD` page ranges; and validate that `e_entry` lies in an
    executable loaded segment;
-   keep this bounded to concrete loader correctness. Do not turn it
    into general ELF conformance or dynamic-linker work yet.

### Later ELF / VMO optimization: private demand-paged file backing

The current eager-copy loader is intentionally the simple
implementation. Later, once the VMO/page-fault machinery has a concrete
reason to support it, investigate private file-backed mappings with
copy-on-write / copy-on-demand semantics:

-   allow clean ELF-backed pages to remain shared with the source image
    where their required contents are identical;
-   allow initialized writable `PT_LOAD` pages to begin from shared file
    backing but materialize private pages on write (COW), without
    granting write authority to the source ELF VMO;
-   materialize mixed file/BSS boundary pages on demand by copying the
    valid file bytes and zeroing the remainder;
-   consider demand paging so untouched executable pages need not be
    copied/read into private memory at launch time;
-   keep this as general VMO/VM/page-fault machinery rather than
    ELF-loader-specific cleverness.

Do not block the current loader/init work on this optimization. The
eager-copy path is the intended implementation until the VM subsystem
can express these semantics cleanly.

### Hostile-ELF audit triage (2026-10-02)

A read-only adversarial audit of the current loader found no direct path
from ELF-controlled virtual addresses into the kernel half and no direct
bypass of the existing AS/VMO capability checks. It did find several
malformed-input, resource-lifetime, and launch-protocol problems. Triage
them according to the current project stage rather than treating every
production-hardening item as an immediate blocker.

#### MUST fix before more feature work / literally tonight

These are either reachable correctness/security bugs on the current
path, can turn ordinary failure into whole-system failure/hang, or
become harder to repair if more launch machinery is built on top of the
present semantics:

-   **ELF admission must become a complete side-effect-free pass before
    commit.** Validate all relevant program headers and compute all
    rounded ranges before creating VMOs or changing the target AS. This
    is the structural fix that makes malformed input rejection
    deterministic and prevents a late bad header from being discovered
    only after earlier mutations.
-   **Enforce `p_offset` / `p_vaddr` page congruence and supported
    `p_align` semantics before any copying/mapping.** The current
    missing congruence check can make the loader calculate a mixed-page
    source outside the mapped source VMO and fault instead of rejecting
    the ELF.
-   **Reject overlapping page-rounded `PT_LOAD` ranges explicitly during
    admission.** Do not depend on the current lower VM layer
    incidentally rejecting replacement mappings; the loader's accepted
    format and security result must not change if mapping internals
    later change.
-   **Validate `e_entry` against the declared byte range of an admitted
    executable `PT_LOAD` (`PF_X`), not merely against a mapped/rounded
    page.** Keep the temporary header-level `e_entry` sanity check only
    until this real admission check exists; `e_entry` is a virtual
    address and is not fundamentally bounded by ELF file length.
-   **Keep ELF header/table sanity deliberately strict even for metadata
    the loader does not consume.** Bound ordinary program headers
    generously but finitely (`e_phnum <= 64`) because they drive loader
    work; allow a generous ordinary section-header count
    (`e_shnum <= 1024`) because sections are ignored at runtime, but
    require any claimed PHDR/SHDR table to fit wholly inside the logical
    ELF length using overflow-safe subtraction/division checks. Reject
    unsupported extended numbering rather than accidentally interpreting
    it. The point is structural sanity, not caring about section
    contents.
-   **Reject load permissions/semantics the current VM cannot faithfully
    represent.** In particular reject a `PT_LOAD` without `PF_R`, reject
    unknown `p_flags` bits, and explicitly reject unsupported executable
    semantics/features rather than silently pretending to support them.
    Keep this narrowly scoped to the static ELF subset Sharkix actually
    consumes; do not begin dynamic-linker/TLS implementation merely
    because those ELF features exist.
-   **Remove the ELF-triggerable whole-machine halt path.** A segment
    can currently occupy the later fixed stack range, causing stack
    setup failure and the test startup profile to execute `cli; hlt`
    forever. Any ELF/launch failure must become an ordinary failed
    launch/cleanup path, never a deliberate system halt. A generalized
    forbidden-range mechanism may wait for the generic launcher; fixing
    the catastrophic failure behavior may not.
-   **Fix/enforce the intended `AS_THREAD_CREATE` authority check for
    `THREAD_CREATE`.** This is a current capability-boundary correctness
    gap and should not be carried into generic launch.
-   **Do not add generic/started-thread destruction until the
    termination contract is sound.** The existing kthread
    create/start/unstarted-destroy paths are acceptable for the current
    milestone, but arbitrary termination of another started thread is
    not implemented. Add a core termination operation only after it can
    safely detach the target from every scheduler/wait structure that
    may retain it. Self-destruction should unpublish/detach the managed
    wrapper, release locks, then finish with non-returning
    `thread_exit_current()` and the existing global reaper.
-   **Fix the anonymous VMO/PMEM ownership leak as part of the lifetime
    audit before substantially increasing anonymous-VMO consumers.**
    Current anonymous backing can permanently lose physical pages when
    descriptors/caps disappear. More ELF launch/init work would multiply
    the number of paths that depend on these ownership semantics, so
    establish the correct ownership/reclamation rule now rather than
    adding loader-specific cleanup hacks.
-   **Audit/fix the known capability global-lock/destructor ordering and
    reachable AS destruction lifetime problems before adding more cap
    handoff/destruction users.** Do not run arbitrary subsystem
    destructors while `global_caps_table_lock` is held; do not use
    copied `cap_t *` pointers after dropping the lock; ensure
    lookup/acquire is atomic with unregister for destructible/refcounted
    targets. These are foundation semantics that cap transfer/init would
    otherwise build upon.

For tonight's blocking pass, prefer small explicit invariants and
regression tests over new frameworks. Build/test after each bounded
subsystem change. The goal is not formal perfection; it is to stop
knowingly building new functionality on top of reachable UAF, leak,
deadlock, authority-check, or whole-system-failure bugs.

#### Important, but may be deferred past tonight

-   **Full transactional ELF commit/rollback:** the admission/commit
    split should be established now, but complete rollback becomes much
    easier and more trustworthy once VMO/mapping ownership and
    destruction are sound. Do not invent ad-hoc loader lifetime rules.
    After the lifetime fixes, make a failed commit unmap everything
    installed by that load and release everything it created.
-   **Loader allocation/resource budgets:** hostile ELF input can direct
    the loader's factory authority into very large allocations/work even
    though the ELF process itself has no factory cap. This is a real
    confused-deputy/resource-exhaustion issue before arbitrary untrusted
    ELF submission is supported. Do not design the final
    quota/accounting architecture tonight. The existing later
    resource-accounting section is the right home; generous temporary
    sanity ceilings are acceptable if trivial, but are not a substitute
    for eventual delegated accounting/budgets.
-   **Loader death waking a synchronous launch coordinator:** a loader
    fault/death can currently leave a coordinator blocked waiting for a
    status IPC. This is real robustness work, but the general answer
    belongs with thread/IPC endpoint lifetime/death semantics rather
    than ELF parsing. Revisit during the lifetime/IPC lifecycle work
    before relying on the loader as a robust long-running service.
-   **Partial-page information exposure:** current source-VMO
    construction zeroes the rounded allocation before copying the ELF,
    so current padding is initialized/non-secret. Document that as a
    required temporary source-VMO contract. Later private boundary-page
    materialization/COW/demand paging can remove the assumption; do not
    implement the pager now.
-   **Source VMO sealing/immutability against other aliases:** the
    current profile gives the source image no write authority, which is
    sufficient for the controlled path. A reusable file-backed loader
    eventually needs a stronger immutable/sealed backing contract if
    another cap could mutate backing that target processes map directly.
    This belongs with later VMO semantics.
-   **W+X policy:** `PF_W|PF_X` is not by itself a capability escape.
    Decide later whether Sharkix's supported ELF policy rejects it by
    default; do not let this become an unrelated W\^X project tonight
    unless current consumers require a decision.
-   **General target-AS/forbidden-range policy:** current loading
    assumes a fresh, separate target AS. Document that assumption now.
    Let the generic launcher/bootstrap work define stack/guard/bootstrap
    reservations and a reusable forbidden-range contract when it has
    concrete consumers.

#### Loader finish line for the current milestone

Treat the loader as good enough to stop touching once: its accepted
static ELF subset is explicit; admission is side-effect-free; all
accepted address/file arithmetic and page relationships are validated;
page-overlap and entry-point semantics are deterministic; malformed
input cannot make the loader perform an out-of-range source access; and
ordinary load failure cannot halt the machine. Then let the lifetime
audit repair the ownership machinery underneath it. Do not expand this
milestone into general ELF conformance, dynamic linking, COW, demand
paging, or resource-accounting architecture.

#### Regression tests to retain

For every concrete hostile-ELF finding fixed, keep a regression
input/test where practical: incongruent offset/vaddr, rounded-page
overlap, entry in non-executable or executable-page padding,
unsupported/unreadable load flags, stack/reserved-range collision once
that policy exists, arithmetic/bounds edge cases, and a later-invalid
header after earlier valid-looking headers. Re-run the existing large
initialized writable-data + BSS + RO protection tests as the normal
positive case.

## 2. Continue the bounded lifetime / destruction audit

Keep moving subsystem by subsystem rather than reopening completed work.

Completed for the current implemented paths:

-   KAS registry/acquire/unregister/reaper ownership has been audited;
    registry publication/acquisition is serialized and unregister
    transfers the registry-owned reference to deferred cleanup. Keep the
    known producer/reaper locking fix in place.
-   `kthread_init()`, `kthread_create()`, `kthread_start()`, and
    `kthread_destroy_unstarted()` have been audited at the kthread
    boundary. `kthread_t` now tracks whether start succeeded so
    unstarted destruction does not depend on core `thread_t` state
    representation.
-   `scheduler_make_runnable()` is acceptable under the current UP
    model; revisit its IRQ-only synchronization when SMP becomes real.

Pending thread work is deliberately separated from the completed basic
kthread pass:

-   inspect core `thread.c` lightly after the subsystem/call-site audit
    to verify the execution-context lifetime assumptions and reaper
    semantics;
-   implement arbitrary started-thread termination later as an explicit
    scheduler/thread feature, including safe removal/cancellation from
    every queue or wait structure that can retain the target;
-   self-destruction should use the existing non-returning
    `thread_exit_current()` path after all kthread/cap bookkeeping and
    locks are released;
-   do not add another reaper thread merely for kthread destruction.

Then continue with simpler capability-visible subsystems. Prioritize
reachable UAF, destruction/unregister races, cap-lock/destructor
ordering, persistent references, and resource leaks.

Definition of done for this pass: capability-visible objects needed by
init have coherent, explainable lifetime rules and no known reachable
UAF/deadlock or unsafe destruction race on their implemented paths.

## 3. Finish capability handoff needed by ring3 init

A userspace init must be able to construct a restricted capability set
for a new domain. The current `CAP_TRANSFER`, `CAP_FORWARD`, and
`CAP_DERIVE` stubs are a concrete integration blocker for replacing
kernel-profile provisioning with a real ring3 launcher.

-   implement only the derivation/transfer/forward semantics actually
    needed by generic launch and init;
-   preserve attenuation: init should be able to hand a service only the
    rights it needs;
-   keep ownership explicit: moving a cap changes ownership;
    deriving/copying creates a distinct cap;
-   do not design capability-over-IPC merely to solve initial bootstrap
    handoff if a simpler generic launch mechanism suffices.

## 4. Standard bootstrap ABI + generic ELF launch

-   define a simple standard Sharkix initial userspace stack/bootstrap
    format;
-   allow the launcher to place the target's initial capability
    information there;
-   create the target AS through its factory and load the ELF using the
    corrected load-only loader;
-   create the initial thread with RIP = validated ELF entry and RSP =
    launcher stack top;
-   start it only after mappings and bootstrap state are complete.

The existing named-cap bootstrap scheme is the starting point. Do not
invent a more elaborate ABI without a concrete consumer.

## 5. Bring up a boring real init

Once generic launch and cap handoff are sufficient, begin the real ring3
init path. Let real driver/service requirements drive further factories
and syscalls. Do not pre-build every conceivable creation facility.

# Kernel core / subsystem boundary cleanup

After the immediate AS/thread factory and generic-launch work is green,
continue the source-boundary cleanup so `core/` knows as little as
practical about particular kernel-managed subsystems.

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

-   move/re-home `startup_kernel_thread()` out of the startup-profile
    interface once the boundary cleanup is active; it is a broadly
    useful thread facility and likely belongs with core thread/thread.h
    rather than `startup.h`. Do not mix this naming/location refactor
    into the current lifetime audit;
-   keep syscall entry/dispatch mechanics in core, but move
    subsystem-specific syscall implementations alongside the subsystem
    they operate on where this produces a cleaner dependency boundary;
-   avoid a central core syscall implementation file accumulating
    knowledge of VMO, AS, kthread, notification, IRQ, PortIO, or future
    subsystem internals;
-   derive capability type enums, rights, valid-right masks, and
    type-driven validation/dispatch tables from the canonical `caps.inc`
    declarations rather than maintaining parallel lists in core code;
-   audit remaining switches/tables in core that enumerate concrete
    capability types and either derive them from `caps.inc` or move the
    type-specific policy to the owning subsystem;
-   keep capability mechanism generic:
    capsets/handles/derivation/transfer and authority checks belong in
    the capability machinery, while semantics of a particular cap target
    belong to its subsystem;
-   prefer ordinary separately compiled subsystem translation units; do
    not make subsystem implementations textual `.c` includes merely to
    remove code from a core source file;
-   do not introduce a universal kobject/OOP dispatch framework to
    achieve this. Small explicit typed subsystems plus generated ABI
    declarations are preferred.

Useful dependency smell test:

``` text
subsystem -> core       normal
core -> subsystem       justify carefully
core -> every concrete cap type       refactor target
```

The goal is not a mathematically pure core. Architecture, scheduler,
memory, and capability mechanisms will necessarily provide interfaces
used by subsystems. The goal is to stop generic core machinery from
becoming the place where knowledge of every Sharkix object type and
syscall implementation accumulates.

# DRIVER ARCHITECTURE AFTER ELF

## Read the current driver architecture first

-   Read and review `docs/DRIVERS.md`.
-   Audit the existing ring0 portions under `src/drivers/`.
-   Use the existing drivers as evidence for what the future metadata
    actually needs to express.

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

Do not refactor this while auditing it. First establish what the code
actually does.

## Driver metadata

Figure out a sane metadata format that can be read directly from an ELF
without needing to execute driver code.

Derive it from the audit rather than designing a speculative framework.

Likely things metadata may need to describe:

-   named dependencies;
-   required capability rights;
-   kernel/resource objects required;
-   IPC/notification objects to create;
-   registry names/services provided;
-   bootstrap capabilities;
-   lifecycle/readiness dependencies.

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

Prefer adding a generic facility over keeping permanent per-driver ring0
setup code.

Do NOT turn driver metadata into an OOP framework, IDL system, package
manager, general-purpose graph language, or executable configuration
language.

## Generic object/resource factories

Where the audit shows that init needs to manufacture privileged objects,
provide small generic capability-controlled facilities.

The VMO, AS, and thread factories required for the basic generic-launch
path now exist.

Add further typed factories only when a real consumer requires them.
Possible later consumers may include VMO sets, IPC endpoints,
notifications, or other privileged resources, but do not pre-build them.

Factory authority grants creation authority. It does not permanently own
everything created through it. Keep each factory narrow rather than
introducing a universal factory.

### Resource accounting / limits

Do not make resource limits part of the initial factory-capability
mechanism. For now, a factory cap answers only whether its holder has
authority to create the relevant object.

Later, add a resource-accounting / quota mechanism when there is a
concrete need to limit consumption such as anonymous VMO memory. Keep
this conceptually separate from basic creation authority.

Questions to resolve when implementing it include:

-   what owns or identifies a resource budget/accounting domain;
-   whether limits are attached to factory caps, separate budget
    objects, or another accounting authority;
-   how delegation and cap derivation interact with budgets;
-   when resources are charged and released, including objects that
    outlive the cap/factory that created them;
-   how shared pages, future COW, and other shared backing are
    accounted;
-   whether different resources need separate limits rather than one
    generic quota.

Do not add quota fields to the generic capability structure merely
because VMO creation can consume memory. First establish the simple
factory-cap model, then add accounting when real consumers make the
required semantics clear.

## Migrate drivers to ELF

Once the ELF loader and generic launch path work:

-   migrate existing ring3 drivers/services from flat binaries to normal
    ELF executables;
-   replace bespoke ring0 launch/setup code with metadata + generic
    facilities;
-   migrate incrementally rather than requiring a flag-day conversion;
-   keep the early/kernel debug console independent of the late
    userspace driver path;
-   remove flat-binary/bootstrap-specific driver launch scaffolding once
    nothing needs it;
-   goal: ordinary drivers should not require `src/drivers/*/kernel`
    setup modules merely to be launched.

# INIT / SERVICE MANAGER

Build a deliberately boring ring3 init/service manager after the ELF
loader and driver metadata model are usable.

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
Drivers should not need arbitrary global authority to discover or
manufacture resources.

Do not overdesign supervision initially. First version needs
deterministic startup, clear failure reporting, and readiness/dependency
handling.

Once ELF + init are the normal userspace boot path:

-   remove obsolete bootstrap launch paths;
-   remove temporary flat-binary driver machinery except anything
    genuinely required to bootstrap the ELF loader itself;
-   remove hard-coded driver startup ordering;
-   consolidate duplicated initial-cap setup.

# REMAINING OBJECT LIFETIME / OWNERSHIP WORK

Do this as required by the ELF/init work rather than as an open-ended
refactoring project.

Useful distinction:

``` text
handles identify
caps authorize
refs keep REFCOUNTED objects alive
```

Do NOT invent a universal kobject framework merely because several
subsystems use handles.

## Lifetime / destruction audit

The AS_CREATE work has made previously theoretical lifetime races
reachable. Do a deliberate pass over capability-visible/refcounted
subsystems as the thread/init work requires it. Keep the audit typed and
local to each subsystem rather than building a universal lifetime
framework.

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

-   apply explicit typed acquire/release APIs to remaining
    refcounted/destructible registries where concurrent
    unregister/destruction can occur; KAS already has this pattern and
    should be treated as the completed reference model rather than
    re-audited;
-   audit IPC, VMO/PMEM, notifications and IRQ for
    raw-lookup/use-after-unregister patterns, adding typed
    acquire/release APIs only where the target actually has
    refcounted/destructible lifetime;
-   audit remaining persistent relationships (for example address space
    -\> VMO set) so the owner retains the referenced object for the full
    lifetime of the relationship;
-   do not call subsystem destructors while holding
    `global_caps_table_lock`; copy the capability type/object identity
    needed for dispatch while protected, then release the cap-table lock
    before entering subsystem destruction code;
-   never retain or dereference a `cap_t *` obtained from the global
    table after dropping the lock unless the capability mechanism
    explicitly gives it a safe lifetime; copy the required values first;
-   define and audit the distinction between `CAP_RIGHT_REMOVE` (remove
    this authority record) and `CAP_RIGHT_DESTROY` (request
    destruction/unregistration of the target object); removing a cap
    must not implicitly destroy its target;
-   audit concurrent REMOVE/DESTROY and concurrent DESTROY/DESTROY on
    the same cap. Validation, subsystem destruction and final cap
    removal currently form a multi-stage operation and need an explicit
    linearization rule;
-   consider a per-cap lifecycle such as `LIVE -> DESTROYING -> removed`
    so one thread can atomically claim a destructive operation without
    holding the global capability lock across arbitrary subsystem code;
    all resolve/remove paths would reject a cap in `DESTROYING`;
-   do not implement rollback from `DESTROYING` to `LIVE` unless
    subsystem failure semantics guarantee that a failed destroy leaves
    the object intact. Prefer destruction/unregistration semantics where
    the externally visible transition is committed cleanly and later
    cleanup may drain references;
-   sibling/derived caps may initially become stale after the target
    object is unregistered. A stale cap safely failing target
    acquisition is acceptable; do not add object-to-cap reverse tracking
    or global revocation machinery without a concrete requirement;
-   for each destructible subsystem, document what owns its registry
    reference, what can hold temporary/persistent references, what
    unregister means, when new acquisitions stop, and what event finally
    frees the object.

Do this audit incrementally as real factories/objects become
userspace-visible, but do not postpone concrete reachable races. The
immediate AS acquire/release and cap-lock/destructor issues must be
fixed before treating AS destruction as sound.

## IPC / PUBSUB

Core ordinary endpoint lifetime/refcounting is working.

Endpoint capacity follow-up:

-   add optional dynamic endpoint resizing with sensible defaults,
    expressed in terms such as minimum/maximum free slots rather than
    forcing callers to micromanage raw capacity;
-   likely expose this through small `kipc_` operations plus optional
    endpoint-creation syscall fields/options;
-   preserve the current fixed/default sizing when the optional values
    are zero, so existing callers do not need to care.

Remaining PUBSUB lifetime/policy work:

-   unsubscribe;
-   subscription-list lifetime and cleanup;
-   publisher/subscriber destruction cleanup;
-   safe traversal once subscription nodes can be removed/freed;
-   publisher backlog / active-publication state;
-   track which subscribers have received the current publication;
-   RELIABLE / TIMEOUT / LOSSY backpressure policies;
-   missed-publication accounting/status;
-   preserve ordering during progressive delivery.

Current simple fan-out may remain simple until a concrete consumer
requires the rest.

## IPC registry

Registry currently names IPC rendezvous points.

Future work, preferably driven by init/metadata requirements:

-   keep IPC rendezvous naming distinct from the generic named-object
    registry unless experience later shows they genuinely want to
    converge;
-   EPHEMERAL vs PERSISTENT registrations if actually needed;
-   rebinding/reanimation of service names;
-   authorization for rebinding.

Do not solve rebind authorization prematurely.

## PMEM / VMO / VMO sets

Anonymous zero-filled VMOs now exist as a distinct kernel
object/construction path. Keep their external semantics independent of
how physical backing is allocated.

Remaining work:

-   PMEM should be refcounted where live PMEM-backed VMOs depend on it;
-   a PMEM-backed VMO retains its backing PMEM;
-   `kvmo_destroy()` must destroy/release the backing PMEM and free its
    physical pages when the VMO has `owns_pmem == true`; non-owning VMOs
    must leave their backing PMEM alone;
-   VMOs should be refcounted where persistent relationships depend on
    them;
-   VMO-set entries/mappings retain the VMOs they depend upon;
-   an address space retains its VMO set;
-   give VMO sets sensible ownership semantics because they are intended
    to be transferable/configurable objects.

### Physical allocator / fragmentation

The current physical allocator can require a contiguous run for
multi-page allocations. This is acceptable as a temporary implementation
detail, but anonymous VMOs must not permanently require one physically
contiguous extent.

Later, rework the physical page allocator around a buddy allocator (or
an equally simple allocator that provides the same useful properties):

-   maintain free blocks by power-of-two order;
-   split larger blocks to satisfy smaller allocations;
-   coalesce free buddies on release;
-   keep useful global free-page accounting;
-   preserve order-0/single-page allocation;
-   retain a way to request genuinely contiguous physical memory where
    hardware or another concrete consumer requires it.

Once that exists, allow anonymous VMOs to be backed by multiple
contiguous physical extents/buddy blocks rather than requiring one giant
run. A VMO remains one contiguous logical byte range regardless of
physical fragmentation. Prefer an extent representation over one
bookkeeping object per page when a contiguous block is available.

Do NOT block the current ELF/BSS work on replacing the allocator.

## Threads / kthread

The userspace-visible `kthread` wrapper and basic
create/start/unstarted-destroy paths now work and have had a bounded
lifetime/concurrency pass. Keep the intended boundary:

``` text
core/thread.c          scheduler/execution machinery
subsystems/kthread.c   managed kobject/handle/lifetime wrapper
```

Current invariant:

-   `kthread_t::started` records whether `thread_start()` succeeded.
    This is intentionally kthread-owned bookkeeping so the managed
    subsystem does not depend on the internal state representation in
    core `thread.c`;
-   creation fully initializes the wrapper before publishing it under
    `kthreads_lock`;
-   start lookup/use and the successful `started = true` transition are
    serialized under `kthreads_lock`;
-   unstarted destruction verifies `started == false` and unpublishes
    under the same lock, then performs core destruction after unlocking.

Pending lifecycle work:

-   add `kthread_destroy_started()` / generic `kthread_destroy()` only
    when the started-thread termination contract is implemented cleanly;
-   self-target destruction is the easy supported core case: perform
    managed-object/cap bookkeeping, release all locks, then call
    non-returning `thread_exit_current()` and let the existing global
    thread reaper reclaim the core execution context;
-   arbitrary termination of another started thread is required, but is
    a separate core scheduler/thread feature. It must safely
    remove/cancel the target from the ready queue, sleep state,
    IPC/notification waits, or any other structure retaining
    `thread_t *` before the existing reaper can free it;
-   do not implement arbitrary termination by merely changing the target
    state to `TERMINATING`;
-   do not add a second reaper for kthread;
-   review remaining raw `thread_lookup()` pointer lifetime and
    establish the appropriate acquire/state/identity contract;
-   keep CPU-core objects and CPU affinity/control authority separate
    from thread creation.

Reaper/core-thread follow-up:

-   **verify and fix kernel-stack reclamation in `thread_reap()` before
    relying on repeated thread churn.** A 2026-10-02 source note says
    the reaper does not reclaim kernel stacks, while a later inspection
    reported that it does free kernel stacks. Resolve this against the
    current source and keep a regression/stress test; do not let the
    TODO silently assume either version;
-   kernel-stack virtual-address ranges are reported to advance
    monotonically even when physical backing is reclaimed. Treat
    virtual-range reuse as separate later work if confirmed.

``` text
DEAD != FREE
```

## PortIO / sync / CPU objects

Keep these simple.

-   Do not add target refcounting to PortIO merely because it has a
    handle.
-   Embedded mutexes/semaphores remain owned by their containing
    subsystem unless they later become independently exposed.
-   CPU objects are likely canonical/permanent; caps authorize
    operations but do not imply target refcounting.

# CONSOLE / INPUT FOLLOW-UP

Do this after the ELF/driver/init work unless it becomes necessary
sooner.

-   hook up `console.input`;
-   eventually define a generic keyboard-event representation above
    device-specific drivers;
-   translate PS/2 scan codes to generic key events in the PS/2-specific
    layer or an immediately adjacent PS/2 decoder;
-   keep keyboard layout / character / Unicode policy above
    hardware-specific scan-code or HID decoding;
-   decide input routing/ownership only when there is a real consumer;
-   runtime console selection/configuration if still useful;
-   keep the early/late console boundary explicit and boring.

Late userspace console output uses `console.output` PUBSUB. Early Bochs
E9 debug output remains independent.

# OTHER IMPORTANT WORK

## Capability derivation / handoff / transfer

The generic capability operations needed by ring3 init are now immediate
work: `CAP_DERIVE`, `CAP_TRANSFER`, and `CAP_FORWARD` are currently
stubbed, and init needs a safe way to construct restricted capability
sets for newly launched domains. Implement the minimum semantics
required by generic launch/init, with clear ownership and attenuation
rules.

Capability transfer over ordinary IPC remains later work once a concrete
consumer requires it. A possible eventual shape remains:

``` text
SYS_IPC_SEND_CAPS
SYS_IPC_RECV_CAPS
```

Do not settle that IPC syscall family prematurely.

## Error numbers

Replace generic `-1` returns with meaningful typed/defined Sharkix
errors.

## Initrd

Add initrd support when the ELF/init path has a concrete need for
packaged userspace executables/configuration.

## DDK / out-of-tree drivers

Clean up libsharkix/DDK support and out-of-tree driver builds after the
normal ELF driver model exists.

## Early boot / architecture / portability refactor

Return to this after the immediate factory/init work. The source-tree
reorganisation is useful precisely because it makes architecture and
boot-protocol assumptions auditable file by file.

### Split Multiboot1 from x86_64 bootstrap machinery

The current Multiboot1 `boot.S` still combines protocol ABI work with
x86_64 machine bootstrap work. Refactor it so the ownership boundaries
are explicit.

Multiboot1-specific code should own:

-   the Multiboot1 header;
-   `_start`;
-   receipt/preservation of the Multiboot `%eax` magic and `%ebx`
    information pointer;
-   the Multiboot-specific 64-bit continuation / handoff into
    `mb_init.c`.

Architecture-specific bootstrap code under `src/kernel/arch/x86_64/`
should own the machinery that is not intrinsically Multiboot:

-   temporary/bootstrap stack where required;
-   bootstrap page tables and initial x86_64 mappings;
-   enabling PAE/long mode/paging for boot paths that enter in 32-bit
    protected mode;
-   temporary bootstrap GDT and the 32 -\> 64 bit transition;
-   switch to the normal kernel stack.

Prefer a name that states the actual contract (for example
`bootstrap32.S`) rather than implying that every x86_64 boot path must
use it. A future boot protocol that enters directly in long mode may
bypass this code.

Keep the temporary bootstrap GDT distinct from the proper runtime kernel
GDT/TSS installed by normal x86_64 architecture initialization.

Make Multiboot-header placement explicit in the linker script rather
than depending on object/link order.

### Define the boot-protocol -\> Sharkix handoff

Do not make generic kernel code parse a bootloader's native structures.

Define a small Sharkix-owned boot-information format/API containing the
information the generic kernel actually needs, including a normalized
physical memory map. Boot-protocol components translate their native
representation into this format before generic kernel initialization.

The boundary should become conceptually:

``` text
boot protocol
    -> protocol-specific parser/translator
    -> Sharkix boot-info / normalized memory map
    -> architecture early init as required
    -> generic kernel
```

Keep the format boring and driven by current consumers. It is an
internal kernel boot contract, not a general firmware/bootloader ABI.

### Refactor memory.c / memory.h away from Multiboot

This is expected to be a substantial job.

`core/memory.c` / its public kernel memory interfaces must stop
including or understanding Multiboot1 structures. In particular:

-   define a Sharkix-specific normalized physical-memory-map
    representation;
-   have `boot/multiboot1` translate the Multiboot memory map into it;
-   make physical-memory initialization consume only the normalized
    Sharkix representation;
-   remove Multiboot-specific parsing/types/includes from generic memory
    code;
-   audit any other boot-protocol assumptions currently leaking into
    `core/memory.c`, `memory.h`, or adjacent VM initialization;
-   preserve architecture-specific VM policy separately from
    boot-protocol parsing.

Do this as a deliberate refactor with the existing x86_64/Multiboot path
kept green, not mixed into unrelated subsystem work.

### BOOTBOOT / alternate boot path later

After the boot contract and architecture boundary are clean, consider
adding BOOTBOOT (and eventually other architectures such as AArch64) as
a second real consumer of those interfaces.

If BOOTBOOT already supplies long mode and usable initial mappings, do
not re-run the 32-bit x86 bootstrap merely for symmetry. Let BOOTBOOT
handle the machine transition it promises, then have Sharkix establish
whatever proper runtime x86_64 state it still requires (kernel
virtual-memory policy, runtime GDT/TSS, interrupt architecture, etc.).

The portability goal is not zero architecture-specific code. The goal is
that architecture-specific code has an obvious home and generic
core/subsystem code does not accidentally depend on x86_64 or a
particular boot protocol.

## Personality layer

Start the personality layer only after ordinary ELF loading, generic
program launch, and the normal userspace service/driver boot path are
established.

The overall shape remains plausible for Unix personalities, but the
forwarding ABI is not designed yet. When this becomes the active
consumer:

-   replace/relocate temporary positive-number test syscalls so the
    stable positive namespace can be personality-serviced cleanly;
-   define per-domain personality binding and forwarding of unhandled
    positive syscalls to an authorized userspace personality service;
-   let concrete Linux/BSD compatibility work drive process/thread,
    signal, VM and executable-startup contracts rather than putting
    those semantics into the microkernel pre-emptively;
-   dynamic linking/interpreter and auxiliary-vector compatibility are
    later personality/loader ABI work, not blockers for controlled
    static driver ELFs.

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

-   separate exception-source object rather than pretending CPU
    exceptions are IRQs;
-   opaque one-shot fault token/context;
-   handler normally lives in another address space;
-   handler repairs state using ordinary Sharkix VM mechanisms;
-   successful resume consumes the fault token;
-   no queued unresolved faults waiting indefinitely for a handler;
-   notifications need not be involved unless a concrete use appears.

Do not design pager chaining, large-scale concurrent fault queues, or
fancy personality semantics before the boring ELF loader works.

# STOP-THE-LINE CHECK BEFORE FUN WORK

Use this as the practical gate when deciding whether to keep auditing or
go build something enjoyable.

**Do it now** if a known issue can, on currently reachable paths and
without assuming a hostile production user: corrupt/free live kernel
memory, dereference a freed object, leak physical memory repeatedly
during normal development/launch, deadlock/hang launch indefinitely,
halt the whole machine on an ordinary recoverable failure, bypass an
intended capability check, or establish ownership/lifetime semantics
that imminent init/cap-handoff work would multiply and make harder to
change.

Under the currently known findings, the stop-the-line set is:

1.  bounded ELF admission fixes and removal of the ELF-triggered
    `cli; hlt` failure path;
2.  explicit `AS_THREAD_CREATE` enforcement;
3.  no new generic started-thread destruction until the
    managed-wrapper/core-thread termination contract is sound; verify
    current reaper/kernel-stack cleanup before thread churn becomes a
    normal path;
4.  anonymous VMO/PMEM backing ownership and reclamation sufficient that
    create/map/unmap/destroy/failure paths do not permanently leak
    pages;
5.  reachable AS acquire/unregister/destruction correctness and the
    global capability-lock/destructor rule, including no unsafe
    `cap_t *` lifetime across unlock;
6.  any additional concrete UAF/deadlock/double-free/irrecoverable leak
    discovered while fixing those exact paths.

Do **not** recursively promote every theoretical issue found by the
audit into tonight's blocker list. Once the known stop-the-line set is
green and tests pass, it is legitimate to do feature work while keeping
the finite lifetime audit as the next deliberate reliability project.

## Fun work allowed after the stop-the-line set is green

These are real feature-development choices, not consolation-prize
cleanup. Pick whichever is motivating once the stop-the-line foundations
it depends on are green. Prefer work that exercises the new generic
mechanisms and exposes the next concrete requirement.

### Option A --- begin the real ring3 init (**best near-term feature target**)

Start a deliberately tiny `init` now, even before it can launch the
whole system. Keep the first version aggressively boring: enter in ring3
through the standard bootstrap path, inspect/consume its bootstrap
information, print an unmistakable `hello from init`/diagnostic, and
become the place where subsequent generic launch policy will live. Grow
it only as the required cap-handoff and launch mechanisms become
available. Do not start dependency graphs, supervision frameworks,
configuration languages, or driver metadata yet.

This is useful even if init initially cannot launch another service: it
gives the bootstrap ABI and capability handoff work a real consumer
instead of designing them in the abstract.

### Option B --- add the minimum syscalls/factories that init immediately needs

Adding syscalls is fair game when a concrete init/driver consumer is
waiting for them. Prefer small capability-controlled mechanisms over
speculative syscall families. Immediate candidates are the minimum
`CAP_DERIVE` / `CAP_TRANSFER` / `CAP_FORWARD` semantics needed to
construct a restricted child domain, **provided cap
ownership/destruction foundations are sound first**. If init exposes
another missing primitive, add that primitive narrowly and document
which consumer required it.

Do not add syscalls merely because they seem generally Unix-like or
might be useful to a future personality. Let init and the first migrated
services pull the ABI into existence.

### Option C --- standard bootstrap ABI + generic ELF launch

Define the small initial userspace stack/cap bootstrap format and make
one generic launcher path perform: create AS -\> load admitted ELF -\>
map stack/bootstrap -\> install restricted initial caps -\> create
initial thread at validated RIP/RSP -\> start. This is both fun and
directly on the critical path to init. Keep the existing named-cap
bootstrap idea as the starting point and resist making a miniature
process framework.

### Option D --- migrate a driver/service to ELF (**tempting, but gated**)

Do **not** migrate a long-running or authority-rich real driver while
the current stop-the-line loader/lifetime issues remain. Once loader
admission, thread lifetime, VMO/PMEM reclamation, AS/cap destruction
foundations, and generic launch are green, migrate **one** existing
controlled ring3 payload/service as the integration test. Pick something
with limited authority and simple bootstrap needs before PS/2 or another
driver holding juicy hardware caps.

The migration is valuable because it will reveal which bootstrap caps,
factories, registry operations, readiness mechanisms, and metadata are
genuinely required. Do not design the complete driver metadata system
before this experiment.

### Option E --- add a small syscall because it is intrinsically fun

Allowed after the stop-the-line set, but give it a rule: the syscall
must either exercise an existing subsystem safely or have a concrete
near-term init/service consumer. Good small additions are operations
that complete an already-existing object's minimal useful interface. Bad
additions tonight are broad POSIX/process APIs, personality forwarding,
pager/fault APIs, or speculative lifecycle controls that would force
lifetime semantics not yet audited.

### Option F --- adversarial integration toys

Keep malformed-ELF regression specimens, thread-state torture tests, and
create/map/destroy memory reclamation loops. These count as fun when
they make the kernel visibly reject attempted murder. They also turn
tonight's boring fixes into permanent executable claims about Sharkix
behaviour.

### Suggested motivation loop

Do not require an entire audit phase before touching features. A sane
loop is:

``` text
fix one stop-the-line invariant
    -> add/prove its regression test
    -> do one bounded piece of init/generic-launch/syscall work
    -> if that exposes a concrete foundational bug, fix it before building further
    -> return to fun work
```

The most rewarding near-term feature milestone remains: **real ring3
init uses generic facilities to construct a restricted domain, load an
ELF, provide its bootstrap caps, create its initial thread, and start it
without bespoke kernel launch code.**

## Dreaded boring audit work that remains important

After (or interleaved in bounded chunks with) the fun milestone work,
continue the finite typed lifetime audit rather than doing an open-ended
rewrite:

``` text
KAS / AS registry lifetime                     DONE for current paths
    ↓
basic kthread create/start/unstarted destroy    DONE for current paths
    ↓
pick a simpler remaining typed subsystem
    ↓
VMO + PMEM backing ownership / refs / final reclamation
    ↓
VMO sets + AS mappings / persistent refs
    ↓
cap REMOVE vs DESTROY + concurrent destructive operations
    ↓
IPC endpoint/service death and blocked-waiter semantics
    ↓
notifications
    ↓
IRQ
```

Thread follow-up is now a bounded dependency item rather than a reason
to hold the whole audit open: lightly inspect core `thread.c`/reaper
semantics, verify kernel-stack reclamation, and defer arbitrary
other-thread termination until its cross-subsystem cancellation rules
can be designed deliberately.

Adjust the exact order when a concrete dependency demands it, but keep
each pass typed and bounded: read-only inspection -\> state the intended
lifetime rule -\> identify violations -\> smallest understandable fix
-\> build/test -\> commit. Comments should explain ownership/state
invariants to a human reader, not narrate syntax.

The finite audit is done when capability-visible objects needed by init
have coherent ownership, acquisition/unregister/free rules and there are
no known reachable UAFs, deadlocks, double-destroys, or permanent
resource leaks on implemented paths. It is **not** done only when every
hypothetical future object and concurrency model has been designed.

## Explicitly not tonight

Unless a stop-the-line bug proves one is required, do not spend tonight
on: COW/demand paging; a general pager; dynamic ELF/linker/TLS support;
final quota/resource-accounting architecture; a universal
kobject/lifetime framework; cap-over-IPC; PUBSUB policy completeness;
buddy allocation; driver metadata framework design; personality
forwarding; generalized userspace exception handling; boot-protocol
portability; or polishing unrelated subsystem naming/refactors.

# DEVELOPMENT ORDER

``` text
FIX CONCRETE ELF LAUNCH CORRECTNESS GAPS + VERIFY THREAD REAPER CLEANUP
    ↓
CONTINUE TYPED LIFETIME / DESTRUCTION / RESOURCE-LEAK AUDIT
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

-   universal abstractions without a concrete consumer;
-   refcounts to permanent/canonical objects;
-   rights merely because a mechanism can theoretically be subdivided;
-   IPC when what is actually wanted is readiness notification;
-   notifications when actual message data needs transporting;
-   fake IPC endpoints merely to represent hardware IRQs;
-   a giant generic kobject framework just because several subsystems
    use handles;
-   driver-specific ring0 setup code when a small generic facility can
    express the same requirement;
-   abstractions merely because there is space for them.

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
> operation, A should normally retain B and release it when that
> relationship ends.

> A cap object normally has exactly one owner: its capset.

> Moving a cap changes ownership. Deriving/copying a cap creates a new
> cap.

> Notifications never know who signals them. Producers know how to
> signal notifications.

> DEAD and FREE are different states for objects where outstanding
> references can exist.

> If implementation cannot be explained in terms of the clean conceptual
> model, reconsider the implementation.

> Do not recreate the abandoned FacetOS OOP/IDL architecture.

And, critically:

> Don't implement the cool fucking pager before the boring fucking ELF
> loader works.

# GENERIC OBJECT REGISTRY

## Generic registry

Add a generic named-object registry alongside the existing IPC registry.

Do NOT replace or generalize the IPC registry yet. Keep the two concepts
separate unless experience shows they genuinely want to converge.

Initial purpose:

-   provide stable names for non-IPC kernel objects/resources;
-   allow init/driver metadata to refer to resources by name;
-   support things such as IRQ, PortIO, VMO, notification,
    address-space, factory, or other capability-controlled objects as
    real consumers appear.

Conceptually:

``` text
generic registry:
    name -> object handle

IPC registry:
    name -> IPC rendezvous/service endpoint
```
