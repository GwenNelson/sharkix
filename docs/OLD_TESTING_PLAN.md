# Sharkix Syscall Testing Plan

This is a syscall-focused test plan for the current Sharkix tree. Each entry is intentionally small enough to become one boot profile, one userspace payload, or one host-side dispatch test.

## Test 01 - Syscall ABI Register Round Trip

Proposed test:
Run one CPL3 payload that fills every syscall ABI register with recognizable values, calls a syscall whose result is known, and verifies the values copied back through `sharkix_syscall_regs_t`. Start with `SYSCALL_TEST_WRITE`, then use `SYSCALL_TEST_WAKE`/`SYSCALL_TEST_BLOCK` for the multi-register return case.

Expected behavior:
The syscall ABI uses `rax,rdi,rsi,rdx,r10,r8,r9`. `RAX` selects the syscall on entry and holds the primary return value on exit. Result registers written by the kernel must land in the same userspace struct fields without swapping `r10`, `r8`, or `r9`.

Obvious errors:
Wrong register offsets, stale user register saves, `rcx/r11` confusion around `syscall/sysretq`, or a userspace wrapper that no longer matches the kernel `syscall_ctx_t`.

Potential edge cases:
Repeat the call many times in one thread and from two different threads so slow register drift or per-thread frame corruption is visible.

Note to self:
Next inspect the invalid-number path and placeholder syscalls; they should probably become explicit safe-failure tests before testing the richer syscall families.

## Test 02 - Unknown Syscall Number Safe Rejection

Proposed test:
Run a CPL3 payload that issues an unmapped positive syscall number, an unmapped negative syscall number outside every Sharkix range, and a very large unsigned value. After each call, issue `SYSCALL_TEST_WRITE` to prove the thread is still runnable.

Expected behavior:
`dispatch_syscall()` must return normally with `RAX == UINT64_MAX` for unknown numbers, and the caller must keep executing.

Obvious errors:
Unknown syscall panics the kernel, kills the calling thread, returns success, or falls through into a neighboring syscall implementation.

Potential edge cases:
Use values near the declared ranges, such as `-100`, `-108`, `-200`, `-204`, `-300`, `-304`, `-400`, `-405`, `-500`, `-507`, `-600`, `-603`, plus a positive personality-style value.

Note to self:
Next separate "unknown syscall" from "declared but placeholder syscall"; the latter is more dangerous because it currently dispatches but some handlers do not write a status.

## Test 03 - Declared Placeholder Syscalls Do Not Masquerade As Success

Proposed test:
Call every declared syscall whose current handler is a stub or near-stub: `CAP_TRANSFER`, `CAP_FORWARD`, `CAP_DERIVE`, `CAP_REMOVE`, `VM_PROTECT`, `PMEM_ALLOC`, `PMEM_DERIVE`, and `PMEM_MERGE`. Seed `RAX` and the argument registers with sentinel values before the call and record exactly what comes back.

Expected behavior:
The test should document current behavior first, then drive a decision: either placeholders must return `UINT64_MAX`/a typed error, or the implementation must define real semantics. They should not accidentally look like successful operations because the handler returned without setting `RAX`.

Obvious errors:
A placeholder returns its syscall number as if it were a useful status, preserves a misleading argument value, or creates the impression that an authority transfer/protect/allocation happened.

Potential edge cases:
Run these calls both with no relevant caps and with valid caps of the wrong type so future implementations cannot silently accept the wrong authority.

Note to self:
Next cover `SYSCALL_TEST_WRITE` and `SYSCALL_TEST_EXIT`; they are small but exercise caller identity and lifecycle.

## Test 04 - TEST_WRITE Returns The Kernel Thread Identity

Proposed test:
Run one CPL3 payload that calls `SYSCALL_TEST_WRITE` with a visible byte sequence and checks that each return value is the same nonzero thread ID. Then run two CPL3 payloads in the same address space and prove their returned IDs differ.

Expected behavior:
`SYSCALL_TEST_WRITE` prints the byte from `RDI` and returns `thread_current()->id` in `RAX`. Caller identity must come from the thread, not from the address space.

Obvious errors:
The syscall returns zero, returns the same ID for two same-address-space threads, corrupts the printed byte, or loses progress after repeated calls.

Potential edge cases:
Use bytes `0`, newline, printable ASCII, and `0xff` so the console path and register truncation behavior are explicit.

Note to self:
Next write the exit test with reaping/resource checks, because many syscall tests will rely on short-lived user payloads.

## Test 05 - TEST_EXIT Terminates And Reaps The Calling User Thread

Proposed test:
Run a CPL3 payload that emits one marker through `SYSCALL_TEST_WRITE`, calls `SYSCALL_TEST_EXIT`, and then contains an infinite loop that should never execute. A kernel monitor should wait for termination and reaping.

Expected behavior:
`SYSCALL_TEST_EXIT` calls `thread_exit_current()`. The user thread should transition out of runnable state, become reapable, and eventually reach `THREAD_STATE_INVALID` after `startup_reaper()` or an equivalent focused reaper path runs.

Obvious errors:
Code after the exit syscall runs, the thread stays permanently terminating/dead, or thread/page accounting does not return to its baseline.

Potential edge cases:
Repeat many short-lived exiting tasks and include one task that exits immediately without first writing a marker.

Note to self:
Next cover the blocking syscall pair, including the "second blocker" and "waker too early" errors.

## Test 06 - TEST_BLOCK And TEST_WAKE Resume The Original Syscall Frame

Proposed test:
Use the existing blocker/waker shape: one user thread calls `SYSCALL_TEST_BLOCK`, a second user thread calls `SYSCALL_TEST_WAKE`, and the blocker verifies all returned registers before exiting.

Expected behavior:
The blocker enters `THREAD_STATE_BLOCKED`, `syscall_block_test_invocations()` increments once, the waker writes the blocked syscall context, `syscall_block_test_wakes()` increments once, and the blocker returns from the original syscall with `RAX=0xb10c`, `RDI=0x1111...`, `RSI=0x2222...`, `RDX=0x3333...`, `R10=0x4444...`, `R8=0x5555...`, and `R9=0x6666...`.

Obvious errors:
The block handler runs twice, the blocked syscall loses its saved context, wake returns through the wrong thread, or any return register is shifted.

Potential edge cases:
Force scheduler yields between every phase: before the block, while blocked, before wake, after wake, and before reaping both tasks.

Note to self:
Next add explicit error tests for TEST_BLOCK/WAKE rather than relying only on the happy path profile.

## Test 07 - TEST_BLOCK And TEST_WAKE Error Ordering

Proposed test:
Run focused variants where `SYSCALL_TEST_WAKE` is called before any blocker exists, where two threads attempt `SYSCALL_TEST_BLOCK`, and where a waker calls wake after the blocker has already completed.

Expected behavior:
Early wake returns `UINT64_MAX`. The first blocker can block; a second blocker must return `UINT64_MAX` instead of replacing `block_test_thread_id`. A late duplicate wake should fail without corrupting the completed thread or incrementing the wake count.

Obvious errors:
The second blocker steals the global blocked-thread slot, a failed wake increments observability counters, or stale blocked-thread IDs point at a reused thread.

Potential edge cases:
Run this after repeated lifecycle churn so thread ID reuse, if it exists later, cannot accidentally wake the wrong task.

Note to self:
Next move into capability-name syscalls because they already have a register-only userspace wrapper and a dedicated profile.

## Test 08 - CAP_SETNAME And CAP_GETNAME Round Trip

Proposed test:
Give a user task a valid capability with `CAP_RIGHT_SETNAME | CAP_RIGHT_GETNAME`, set names of length 0, 1, 8, 9, 31, and 32 bytes through `SYSCALL_CAP_SETNAME`, then read them back through `SYSCALL_CAP_GETNAME`.

Expected behavior:
Names are packed through `RDX/R10/R8/R9` only, never through userspace pointers. `CAP_GETNAME` returns status in `RAX`, actual length in `RSI`, and zeroes unused output registers before returning.

Obvious errors:
Off-by-one packing at 8-byte boundaries, stale bytes after short names, length mismatch, or any accidental dependency on a userspace pointer.

Potential edge cases:
Use embedded NUL bytes in a 32-byte name so the syscall path is tested as length-delimited data, not C-string data.

Note to self:
Next add cap-name permission and invalid-handle failures; exact capset-member resolution is the important security behavior.

## Test 09 - CAP_SETNAME And CAP_GETNAME Authority Failures

Proposed test:
Attempt `CAP_SETNAME` and `CAP_GETNAME` with `CAP_INVALID_HANDLE`, a handle not installed in the caller's capset, and a valid cap missing the specific name right.

Expected behavior:
Both syscalls return failure in `RAX` without changing the underlying cap name. `CAP_GETNAME` should also leave `RSI=0` and clear `RDX/R10/R8/R9` on failure.

Obvious errors:
The kernel resolves a global cap handle that the caller does not hold, SETNAME works with only GETNAME rights, GETNAME leaks an old name through uncleared registers, or a failed SETNAME partially changes the name.

Potential edge cases:
Give two user address spaces different caps to the same kernel object and prove naming authority is checked on the exact supplied cap handle in the caller's capset.

Note to self:
Next cover `IPC_CREATE`, then send/recv with authority and empty-queue behavior.

## Test 10 - IPC_CREATE Returns A Usable Endpoint Capability

Proposed test:
From a user task, call `SYSCALL_IPC_CREATE`, then immediately use the returned cap for a same-thread or helper-thread IPC send/receive path.

Expected behavior:
`RAX == IPC_OK` and `RDI` contains a non-invalid cap handle installed in the caller's address-space capset with endpoint rights. On failure, `RAX` is a typed IPC error and `RDI == IPC_INVALID_HANDLE`.

Obvious errors:
The syscall creates an endpoint but fails to install the cap, leaks the endpoint after cap creation failure, returns a raw endpoint handle instead of a cap handle, or returns success with `IPC_INVALID_HANDLE`.

Potential edge cases:
Stress repeated create/destroy once endpoint destruction semantics are settled, and verify cap handles do not remain usable after cleanup.

Note to self:
Next add IPC send/recv message register mapping and empty/permission failures.

## Test 11 - IPC_SEND And IPC_RECV Register Payload Mapping

Proposed test:
Create or inject an endpoint cap, have one user task send five distinct words with `SYSCALL_IPC_SEND`, and have another user task receive with `SYSCALL_IPC_RECV`.

Expected behavior:
`IPC_SEND` takes destination cap in `RDI` and message words in `RSI/RDX/R10/R8/R9`. `IPC_RECV` returns message type in `RAX`, sender thread ID in `RDI`, and the five message words in `RSI/RDX/R10/R8/R9`.

Obvious errors:
Message words are shifted, sender ID is not the sending thread, receive returns a status code in the success case instead of message type, or send mutates unrelated registers that later code relies on.

Potential edge cases:
Use values that make swapped registers obvious, including `0`, `UINT64_MAX`, and different byte patterns in every word.

Note to self:
Next add IPC failure behavior: no rights, wrong cap type, invalid cap, empty endpoint, closed endpoint when destruction is defined.

## Test 12 - IPC_SEND And IPC_RECV Failure Modes

Proposed test:
Call `IPC_SEND` and `IPC_RECV` with `CAP_INVALID_HANDLE`, an uninstalled handle, a cap of the wrong object type, and endpoint caps missing `CAP_RIGHT_IPC_SEND` or `CAP_RIGHT_IPC_RECV`. Also receive from an empty endpoint.

Expected behavior:
Capability resolution failures return `IPC_ERR_PERMISSION`. Empty receive should return the current `ipc_recv()` empty-queue status without blocking or corrupting result registers.

Obvious errors:
Wrong cap types are accepted, missing rights still allow send/receive, empty receive returns stale message data, or failure leaves success-shaped register contents.

Potential edge cases:
Run failures before and after a successful message so stale queue/register state is easier to spot.

Note to self:
Next inspect VM and PMEM capability setup so VM_MAP/UNMAP tests are realistic rather than synthetic wish-list items.

## Test 13 - PMEM_NEW_VMO Success And Rights Translation

Proposed test:
Create a kernel PMEM object for a single backing page, install a PMEM cap into a user address space, and call `SYSCALL_PMEM_NEW_VMO` with `VMO_MAP | VMO_READ`, then with `VMO_MAP | VMO_READ | VMO_WRITE`.

Expected behavior:
On success `RAX == VM_OK` and `RDX` is a new VMO cap in the caller's capset. The VMO cap rights must match the requested VMO rights translated to `CAP_RIGHT_VMO_*`, plus generic rights.

Obvious errors:
The syscall returns the raw VMO handle instead of a cap, grants write/exec rights that were not requested, fails to install the cap, or leaks the VMO/cap if installation fails.

Potential edge cases:
Also request `VMO_EXEC`, because it has distinct cap and mapping bits and should not accidentally imply write.

Note to self:
Next add PMEM_NEW_VMO failures for invalid rights, unmappable/unreadable requests, wrong cap type, and missing PMEM rights.

## Test 14 - PMEM_NEW_VMO Failure Modes

Proposed test:
Call `SYSCALL_PMEM_NEW_VMO` with invalid VMO rights bits, missing `VMO_MAP`, missing `VMO_READ`, `CAP_INVALID_HANDLE`, a wrong-type cap, and PMEM caps missing each required `CAP_RIGHT_PMEM_*` bit.

Expected behavior:
Invalid request shapes return `VM_ERR_INVALID` and `RDX == CAP_INVALID_HANDLE`. Authority failures return `VM_ERR_PERMISSION` and `RDX == CAP_INVALID_HANDLE`.

Obvious errors:
Unreadable present mappings are allowed despite the x86-64 limitation, missing PMEM authority still creates a VMO, or failed calls leave a usable cap behind.

Potential edge cases:
Run a successful `PMEM_NEW_VMO` after each failure to prove the caller's capset and the global VMO table are still coherent.

Note to self:
Next test VM_MAP success and the exact `RDX` mapped-address return contract.

## Test 15 - VM_MAP Maps A VMO At The Requested User Address

Proposed test:
Use a VMO cap created from PMEM, call `SYSCALL_VM_MAP` with a page-aligned nonzero user virtual address, offset 0, length one page, `VMO_READ` or `VMO_READ | VMO_WRITE`, and flags 0. Then have userspace read/write the mapping according to its requested rights.

Expected behavior:
`RAX == VM_OK`, `RDX` equals the requested virtual address, address-space translation resolves the mapping, and the VMO set records the mapping.

Obvious errors:
The kernel maps a different address while returning success, forgets VMO bookkeeping, ignores requested write permission, or maps without `CAP_RIGHT_VMO_MAP`.

Potential edge cases:
Map the same VMO into two different user address spaces and prove both mappings point at the same PMEM backing without sharing unrelated address-space state.

Note to self:
Next add VM_MAP invalid argument and permission coverage; this handler has many explicit guards.

## Test 16 - VM_MAP Invalid Arguments And Bounds

Proposed test:
Call `SYSCALL_VM_MAP` with flags nonzero, `CAP_INVALID_HANDLE`, length zero, invalid rights bits, rights missing `VMO_READ`, virtual address zero, unaligned addresses/lengths, offsets beyond the PMEM backing, and a range that crosses the end of the backing object.

Expected behavior:
The syscall returns `VM_ERR_INVALID` for invalid request shapes and sets `RDX=0`. It must not create a partial mapping or vmoset entry.

Obvious errors:
Zero-address mapping succeeds, unreadable mapping succeeds, bad flags are ignored, or a failed partial map leaves page tables dirty.

Potential edge cases:
Try a mapping over an address that is already mapped; the lower layer has `VM_ERR_ALREADY_MAPPED`, but the current syscall collapses `kvmo_map()` failure to `VM_ERR_INVALID`, so record current behavior and decide whether to preserve or refine it.

Note to self:
Next add VM_MAP permission failures separately from shape failures.

## Test 17 - VM_MAP Capability Permission Failures

Proposed test:
Call `SYSCALL_VM_MAP` with a valid VMO cap that lacks `CAP_RIGHT_VMO_MAP`, then with caps lacking `CAP_RIGHT_VMO_READ`, `CAP_RIGHT_VMO_WRITE`, or `CAP_RIGHT_VMO_EXEC` while requesting those mapping rights.

Expected behavior:
Capability failures return `VM_ERR_PERMISSION` and `RDX=0`, with no mapping and no vmoset entry.

Obvious errors:
The syscall checks only intrinsic VMO rights and ignores cap rights, or one permission bit implies another.

Potential edge cases:
Create a VMO whose intrinsic rights allow write but the cap omits `CAP_RIGHT_VMO_WRITE`; the syscall must reject a writable mapping despite the object being capable of it.

Note to self:
Next cover VM_UNMAP success and failure; it currently requires a VMO cap and a virtual address.

## Test 18 - VM_UNMAP Removes A VMO Mapping And Its Bookkeeping

Proposed test:
Map a VMO into a user address space with `SYSCALL_VM_MAP`, verify it is accessible, then call `SYSCALL_VM_UNMAP` with the VMO cap and mapped virtual address. Afterward, attempt a userspace access that should fault or verify from the kernel monitor that translation and vmoset lookup fail.

Expected behavior:
`RAX == VM_OK`, the page-table mapping is gone, and the vmoset entry for that virtual address is removed.

Obvious errors:
The syscall removes bookkeeping but leaves the PTE, removes the PTE but leaves stale vmoset state, or unmaps a different mapping for the same VMO.

Potential edge cases:
Map one VMO at two different addresses and unmap only one address; the other mapping should remain valid.

Note to self:
Next add VM_UNMAP failures: invalid cap, missing map right, wrong address, wrong VMO.

## Test 19 - VM_UNMAP Failure Modes

Proposed test:
Call `SYSCALL_VM_UNMAP` with `CAP_INVALID_HANDLE`, wrong-type caps, VMO caps not installed in the caller's capset, VMO caps missing `CAP_RIGHT_VMO_MAP`, unmapped addresses, and an address mapped from a different VMO.

Expected behavior:
Invalid cap shape returns `VM_ERR_INVALID`; permission failures return `VM_ERR_PERMISSION`; existing VMO but missing mapping returns `VM_ERR_ADDRESS` or `VM_ERR_NOT_MAPPED` according to the exact failing layer.

Obvious errors:
Unmap succeeds with only read/write authority, unmaps by virtual address without checking the supplied VMO, or failure corrupts a neighboring mapping.

Potential edge cases:
Try unmapping an address inside a mapping but not equal to its start. Current `kvmo_unmap_at()` naming suggests start-address semantics; the test should make that explicit.

Note to self:
Next move to port I/O syscalls. They need QEMU-safe ports or fake/instrumented kernel objects.

## Test 20 - PORT_IN And PORT_OUT Widths, Offsets, And Rights

Proposed test:
Create a PortIO object for a QEMU-safe test range or an instrumented fake range, install read/write caps, and call `PORT_INB/INW/INL` plus `PORT_OUTB/OUTW/OUTL` at valid offsets.

Expected behavior:
Read syscalls return `PORTIO_OK` in `RAX` and the read value in `RDX`, zeroing `RDX` first on failure paths. Write syscalls return `PORTIO_OK` and perform only the requested byte/word/dword operation at `base + offset`.

Obvious errors:
Width truncation is wrong, offset is ignored, read and write rights are confused, or a failed read returns stale `RDX`.

Potential edge cases:
Use boundary offsets where a byte fits but word/dword access would exceed the PortIO object's length.

Note to self:
Next add explicit PortIO invalid and destroy behavior, including `CAP_DESTROY` because it currently routes through PortIO only.

## Test 21 - PortIO Permission And CAP_DESTROY Behavior

Proposed test:
Call every PortIO syscall with invalid handles, wrong-type caps, caps missing read/write rights, and offsets larger than `UINT32_MAX`. Then call `SYSCALL_CAP_DESTROY` on a PortIO cap with `CAP_RIGHT_DESTROY` and verify the object and cap are no longer usable.

Expected behavior:
Resolve failures return `PORTIO_ERR_INVALID` or `PORTIO_ERR_PERMISSION` as appropriate. Destroy returns `PORTIO_OK` only when both the PortIO object and cap are removed; later PortIO calls with that cap must fail.

Obvious errors:
`CAP_DESTROY` works on non-PortIO caps despite the current implementation being PortIO-specific, destroys the object but leaves a valid cap, or destroys the cap but leaves an accessible object through another stale path.

Potential edge cases:
Create two caps to the same PortIO object if cloning/derivation becomes available, then define whether destroying through one cap invalidates the object for all caps or only removes one authority.

Note to self:
Next cover IRQ_WAIT/IRQ_ACK happy path and permission failures.

## Test 22 - IRQ_WAIT And IRQ_ACK Capability-Gated Flow

Proposed test:
Create an IRQ object for a deterministic source, install a cap with `CAP_RIGHT_IRQ_WAIT | CAP_RIGHT_IRQ_ACK`, trigger the IRQ, call `SYSCALL_IRQ_WAIT`, then call `SYSCALL_IRQ_ACK`.

Expected behavior:
`IRQ_WAIT` blocks or waits according to `kirq_wait()` semantics until delivery, then returns `IRQ_OK`. `IRQ_ACK` returns `IRQ_OK` for the same IRQ object and should not acknowledge unrelated hardware IRQs.

Obvious errors:
Wait returns before delivery, acknowledge succeeds on the wrong object, or interrupt delivery wakes the wrong listener.

Potential edge cases:
Create two IRQ objects for the same hardware IRQ and prove delivery posts both semaphores if that remains the intended `kirq_handle()` behavior.

Note to self:
Next add IRQ invalid-handle and rights tests, plus a note that hardware timing should not make the suite flaky.

## Test 23 - IRQ_WAIT And IRQ_ACK Failure Modes

Proposed test:
Call `IRQ_WAIT` and `IRQ_ACK` with `CAP_INVALID_HANDLE`, wrong-type caps, uninstalled caps, caps missing the specific wait/ack right, and IRQ objects for invalid hardware IRQ numbers.

Expected behavior:
Capability failures return `IRQ_ERR_PERMISSION`. Invalid hardware IRQ setup should fail before userspace receives a cap, or syscalls should return `IRQ_ERR_HW_INVALID`/`IRQ_ERR_NOT_FOUND` according to where the invalid state is detected.

Obvious errors:
Missing `CAP_RIGHT_IRQ_WAIT` still permits waiting, missing `CAP_RIGHT_IRQ_ACK` still permits acknowledging, or invalid IRQ handles crash in the architecture interrupt path.

Potential edge cases:
Keep hardware-driven tests deterministic by allowing a kernel-side test hook to call `kirq_handle()` directly for synthetic delivery; reserve real PS/2 keyboard IRQ coverage for a separate interactive profile.

Note to self:
Next add benchmark syscall coverage and make clear it is observability, not correctness of timing.

## Test 24 - TEST_BENCHMARK State Machine

Proposed test:
Reset benchmark observability, call `SYSCALL_TEST_BENCHMARK` with READY, START, STOP, and an invalid command from one or more user tasks.

Expected behavior:
READY increments `syscall_benchmark_ready_count()` and returns 0. START records a nonzero start TSC and returns 0. STOP records a stop TSC and returns 0. Invalid commands return `UINT64_MAX`.

Obvious errors:
Invalid commands mutate counters, STOP happens before START but still appears meaningful, or repeated READY calls overflow/alias in a way that breaks benchmark startup coordination.

Potential edge cases:
Call READY from multiple tasks to confirm the counter is global and monotonic for the benchmark profile's coordination model.

Note to self:
Next add a host-side dispatch layer test so syscall handler return values can be checked before booting QEMU.

## Test 25 - Host-Side Dispatch Table Completeness

Proposed test:
Build a host-side or kernel-internal test that includes `include/sharkix/syscalls.inc`, constructs one `syscall_ctx_t` per declared number, and verifies every declared number reaches the intended handler category while unknown numbers return `UINT64_MAX`.

Expected behavior:
Every declaration in `syscalls.inc` is represented exactly once in `dispatch_syscall()`. The test should flag newly declared syscalls until they have either a real behavior test or an explicit placeholder-safe-failure test.

Obvious errors:
A syscall number is declared but not dispatched, two names alias unexpectedly, or a new placeholder silently returns with stale `RAX`.

Potential edge cases:
Generate the test matrix from `syscalls.inc` so renumbering cannot leave the test stale.

Note to self:
Next add a kernel/userspace wrapper consistency test for `sharkix_syscall_regs_t` and `syscall_ctx_t` size/offset drift.

## Test 26 - Kernel And Userspace Syscall Struct Layout Agreement

Proposed test:
Add a compile-time test target that asserts `sharkix_syscall_regs_t` and `syscall_ctx_t` have the same field order, field offsets, and 56-byte size.

Expected behavior:
Both sides agree on `RAX/RDI/RSI/RDX/R10/R8/R9` offsets, and the assembly stubs remain aligned with the C structs.

Obvious errors:
One struct gets a new field, reordered field, padding change, or typedef split without updating the assembly copy-in/copy-out code.

Potential edge cases:
Build the check in both the freestanding userspace library configuration and the kernel-side libsharkix stub configuration.

Note to self:
Next add a practical implementation order so future-me can turn this document into real tests without tackling the hardest hardware cases first.

## Test 27 - Syscall Test Implementation Order

Proposed test:
Treat this as the first meta-test for the plan: implement the syscall suite in layers and require each layer to produce an unambiguous PASS/FAIL marker on serial output.

Expected behavior:
Suggested order:
1. ABI/layout compile checks.
2. Unknown syscall and placeholder behavior.
3. `TEST_WRITE`, `TEST_EXIT`, `TEST_BLOCK`, `TEST_WAKE`, `TEST_BENCHMARK`.
4. Cap-name tests.
5. IPC create/send/recv tests.
6. PMEM/VM mapping tests.
7. PortIO tests with safe instrumentation.
8. IRQ tests with synthetic delivery before real hardware IRQs.

Obvious errors:
A profile prints a success marker before all assertions run, relies on manual visual inspection only, or mixes several syscall families so the failing contract is unclear.

Potential edge cases:
Each booted profile should also assert thread reaping and page/resource baselines where it creates short-lived user tasks.

Note to self:
Next do a final pass over the file for accidental overreach and verify only `docs/NEW_TESTING_PLAN.md` changed.
