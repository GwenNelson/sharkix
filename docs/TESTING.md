# Sharkix Kernel Testing Plan

This document proposes a systematic kernel testing plan for the current Sharkix tree as inspected on 2026-08-30. It is derived from the code that currently exists under `src/kernel/`, `src/user/`, `include/sharkix/kernel/`, `bootstub32/`, the build system, and the current startup profiles.

The current tree is a higher-half x86_64 kernel using a custom FreeRTOS port. It already contains:

- Multiboot-1 boot and higher-half transition via [`src/kernel/boot.S`](/home/gwen/sharkix/src/kernel/boot.S)
- Physical-memory discovery and allocator setup in [`src/kernel/memory.c`](/home/gwen/sharkix/src/kernel/memory.c)
- Address-space creation, mapping, unmapping, translation, activation, and destruction
- Dynamic kernel heap via `ksbrk()`, `kmalloc()`, `kfree()`
- Guarded kernel-stack allocation
- Sharkix-owned `thread_t` lifecycle on top of FreeRTOS in [`src/kernel/thread.c`](/home/gwen/sharkix/src/kernel/thread.c)
- x86_64 syscall entry/return and exception routing in [`src/kernel/arch/x86_64/portASM.S`](/home/gwen/sharkix/src/kernel/arch/x86_64/portASM.S) and [`src/kernel/arch/x86_64/port.c`](/home/gwen/sharkix/src/kernel/arch/x86_64/port.c)
- Flat user-image loading in [`src/kernel/program.c`](/home/gwen/sharkix/src/kernel/program.c)
- Current startup profiles:
  `normal`, `syscall`, `exceptions`, `vm`, `lifecycle`, `two_tasks_one_space`, `syscall_block`

This plan is intentionally split by execution environment:

1. Host-side tests
2. Booted kernel-space tests
3. Single-usermode-task tests
4. Multiple-usermode-task tests

The aim is to push as much logic as possible into host-side coverage, then use booted tests for the parts that actually require the live kernel, page tables, exceptions, scheduling, CPL3 entry, or QEMU-visible hardware behaviour.

## Coverage Inventory

Major subsystems and interfaces currently present in the tree:

- Boot and image layout
  - `_start`, long-mode transition, bootstrap page tables, temporary identity map, higher-half jump
  - Multiboot-1 structures in [`include/sharkix/kernel/boot/multiboot1.h`](/home/gwen/sharkix/include/sharkix/kernel/boot/multiboot1.h)
  - Linker layout in [`src/kernel/linker.ld`](/home/gwen/sharkix/src/kernel/linker.ld)

- Console and diagnostics
  - `console_init()`
  - `console_putc()`
  - `console_write()`
  - `console_hex()`
  - `console_decimal()`

- Physical memory and kernel VM
  - `phys_to_virt()`
  - `virt_to_phys()`
  - `memory_init()`
  - `phys_total_ram_bytes()`
  - `phys_managed_page_count()`
  - `phys_free_page_count()`
  - `phys_pages_in_use()`
  - `phys_alloc_page()`
  - `phys_alloc_pages()`
  - `phys_alloc_page_below()`
  - `phys_alloc_pages_below()`
  - `phys_page_get()`
  - `phys_page_put()`
  - `ksbrk()`
  - `kmalloc()`
  - `kfree()`
  - `kernel_stack_alloc()`
  - `kernel_stack_free()`
  - `address_space_create()`
  - `address_space_kernel()`
  - `address_space_retain()`
  - `address_space_release()`
  - `address_space_references()`
  - `address_space_map_page()`
  - `address_space_unmap_page()`
  - `address_space_translate()`
  - `address_space_activate()`

- Program loading
  - `program_map_flat_image()`
  - `program_map_user_stack()`
  - `program_load_and_start()`
  - `program_start_options_t`
  - `program_image_t`

- Threads and scheduler integration
  - `thread_current()`
  - `thread_current_id()`
  - `thread_lookup()`
  - `thread_get_state()`
  - `thread_create()`
  - `thread_start()`
  - `thread_create_started()`
  - `thread_destroy_unstarted()`
  - `thread_prepare_current()`
  - `thread_timer_may_preempt_current()`
  - `thread_exit_current()`
  - `thread_reap()`
  - `thread_reaped_count()`
  - `thread_block_current()`
  - `thread_wake()`
  - `thread_get_blocked_syscall_context()`
  - `thread_handle_exception()`
  - `thread_handle_kernel_exception()`
  - `cpu0` current-thread/kernel-stack state

- x86_64 architecture code
  - `arch_init_cpu_local()`
  - `arch_init_syscalls()`
  - `vPortInstallKernelGDT()`
  - `vPortSetKernelStack()`
  - FreeRTOS entry, yield, timer, syscall, and exception assembly paths
  - GDT, TSS, IDT setup
  - CR3 switching and CPL3 return

- Syscall ABI
  - `syscall_ctx_t`
  - `syscall_disposition_t`
  - `syscall_result_t`
  - `dispatch_syscall()`
  - Implemented syscall handlers:
    - `SYSCALL_TEST_WRITE`
    - `SYSCALL_TEST_EXIT`
    - `SYSCALL_TEST_BLOCK`
    - `SYSCALL_TEST_WAKE`

- Declared but not implemented syscall groups
  - IPC syscall numbers in [`include/sharkix/kernel/syscalls.inc`](/home/gwen/sharkix/include/sharkix/kernel/syscalls.inc)
  - VM syscall numbers in [`include/sharkix/kernel/syscalls.inc`](/home/gwen/sharkix/include/sharkix/kernel/syscalls.inc)
  - `docs/DESIGN.md` contains intended semantics, but the current kernel does not implement them. Tests must treat them as currently unspecified/placeholder and assert only safe rejection unless behaviour is later defined in code.

- Existing startup-profile coverage
  - `normal`
  - `syscall`
  - `exceptions`
  - `vm`
  - `lifecycle`
  - `two_tasks_one_space`
  - `syscall_block`

- Build and packaging behaviour
  - Makefile profile selection
  - automatic header dependency generation
  - `bootstub32` development boot path
  - GRUB ISO boot path

## Test Harness Principles

- Prefer host-side tests for logic that can run with stubbed architecture hooks and synthetic Multiboot data.
- Reserve booted tests for page-table behaviour, CR3 switching, CPL changes, faults, syscall entry/return, and scheduler interactions.
- Keep each named test narrow and binary PASS/FAIL.
- Record resource baselines before repeated create/map/start/exit/destroy loops and assert return to baseline afterward.
- Re-run selected tests under varied RAM configurations:
  - minimum supported RAM
  - minimum plus one or two extra pages
  - values around bitmap/refcount and physmap growth boundaries
  - large RAM
  - RAM maps with holes/reserved regions
- Repeat leak-sensitive and scheduler-sensitive tests many times.
- Keep build-system checks in the suite:
  - profile selection must pick exactly one startup object
  - header changes must rebuild affected objects without `make clean`

## Existing Profiles And What They Currently Prove

- `PROFILE=normal`
  - Boots the kernel, creates a kernel thread in the kernel address space, and proves the scheduler keeps running.

- `PROFILE=syscall`
  - Creates two independent CPL3 tasks in separate address spaces.
  - Proves CPL3 entry, SYSCALL entry/return, shared kernel-half mappings, distinct CR3 roots, and repeated execution.

- `PROFILE=exceptions`
  - Launches CPL3 tasks that trigger `#UD`, null-pointer `#PF`, and supervisor-address access `#PF`.
  - Proves those user exceptions reach `thread_handle_exception()` and the kernel survives with another task still running.

- `PROFILE=vm`
  - Exercises repeated map/unmap/destroy in fresh address spaces.
  - Proves basic page reclamation accounting and CPL0 execution in a non-kernel address space.
  - Also proves `THREAD_PRIVILEGE_USER + address_space_kernel()` is rejected.

- `PROFILE=lifecycle`
  - Repeatedly launches exiting userspace tasks and waits for deferred reaping.
  - Proves deferred thread cleanup, address-space auto-reap, and retained-address-space lifetime semantics.

- `PROFILE=two_tasks_one_space`
  - Creates two CPL3 threads in one non-kernel address space with separate user stacks and kernel stacks.
  - Proves thread identity is not equivalent to address-space identity.

- `PROFILE=syscall_block`
  - Proves one syscall can block, keep a persistent syscall return context, be woken by another thread, return through the original syscall path, and then be reaped.

These should remain part of the eventual suite; they should be strengthened with explicit assertions and folded into a more systematic harness rather than discarded.

# 1. Host-Side Tests

Host-side tests should compile selected kernel translation units into a host executable with stubbed low-level hooks. They are best suited for pure logic, state machines, allocator accounting, Multiboot memory-map parsing, rollback behaviour, and API semantics that do not require real paging or real CPL transitions.

Some useful internals are currently `static` inside [`src/kernel/memory.c`](/home/gwen/sharkix/src/kernel/memory.c) and [`src/kernel/thread.c`](/home/gwen/sharkix/src/kernel/thread.c). For those, the host test harness should either:

- compile the translation unit directly with test-only stubs, or
- introduce test seams later if needed

The tests below are written against current behaviour, not against a hypothetical redesign.

## Console Formatting Helpers

Summary:
Verify `console_hex()` and `console_decimal()` format values correctly and `console_putc()` newline semantics remain stable.

Purpose:
Sharkix relies heavily on serial diagnostics for verification. Formatting regressions produce false negatives in nearly every booted test and can hide real faults.

Setup:
Host harness with `outb()` and VGA writes stubbed into a capture buffer.

Assertions / pseudocode:
```text
console_init()
console_write("X")
ASSERT(serial_buffer == "X")
console_putc('\n')
ASSERT(serial_buffer_suffix == "\r\n")
console_hex(0)
ASSERT(serial_buffer_contains("0x0000000000000000"))
console_hex(0x1234abcd)
ASSERT(serial_buffer_contains("0x000000001234abcd"))
console_decimal(0)
ASSERT(serial_buffer_contains("0"))
console_decimal(18446744073709551615ULL)
ASSERT(serial_buffer_contains("18446744073709551615"))
```

Success cases:
- Newline emits CRLF on serial and resets VGA line position.
- Hex and decimal formatting are stable for zero, small, and maximum values.

Failure cases:
- Missing carriage return.
- Truncated or malformed numeric output.

## Boot Memory Accounting From Multiboot Mmap

Summary:
Verify that `memory_init()` derives total RAM, managed pages, and free pages from actual Multiboot-1 memory information.

Purpose:
The allocator depends on correct interpretation of usable vs reserved memory. This catches bugs where total RAM is miscomputed, reserved regions are accidentally managed, or allocator metadata overlaps usable pages.

Setup:
Construct synthetic `multiboot_info_t` plus `multiboot_mmap_entry_t` arrays in host memory. Stub architecture hooks such as `vPortInstallKernelGDT()` and CR3 reloads. Provide a synthetic backing array for physmap-visible pages.

Assertions / pseudocode:
```text
build_mmap([
  usable 0x00100000..0x01000000,
  reserved hole,
  usable 0x02000000..0x04000000
])
memory_init(MULTIBOOT_BOOTLOADER_MAGIC, mbi_phys)
ASSERT(phys_total_ram_bytes() == sum_of_available_entries)
ASSERT(phys_managed_page_count() < phys_total_ram_bytes() / PAGE_SIZE)
ASSERT(phys_free_page_count() <= phys_managed_page_count())
ASSERT(page_zero_not_managed)
ASSERT(kernel_image_pages_not_managed)
ASSERT(multiboot_pages_not_managed)
ASSERT(bootstrap_metadata_pages_not_managed)
```

Success cases:
- Available entries contribute to `phys_total_ram_bytes()`.
- Managed/free counts reflect only allocator-managed pages.
- Reserved ranges are excluded.

Failure cases:
- Reserved pages appear allocatable.
- Managed count equals total RAM blindly.
- Metadata, modules, or kernel image can be allocated.

## Memory Info Fallback Without Mmap

Summary:
Verify the `MB_INFO_MEMORY` fallback path when a full mmap is not provided.

Purpose:
Current code supports either a real Multiboot mmap or the older lower/upper memory fields. This test verifies the fallback semantics explicitly.

Setup:
Provide `multiboot_info_t` with `MB_INFO_MEMORY` set and no `MB_INFO_MMAP`.

Assertions / pseudocode:
```text
mbi.flags = MB_INFO_MEMORY
mbi.mem_lower = ...
mbi.mem_upper = ...
memory_init(...)
ASSERT(phys_total_ram_bytes() == (mem_lower + mem_upper) * 1024)
ASSERT(allocator_initialized)
ASSERT(usable_ranges_split_at_1MiB_convention)
```

Success cases:
- Fallback path initializes successfully.

Failure cases:
- Panic with valid fallback info.
- Incorrect total RAM accounting.

## Reserved-Range Exclusion

Summary:
Verify that boot structures, module payloads, strings, and reserved low memory are excluded from allocator management.

Purpose:
This catches overlaps between live boot payloads and later allocations.

Setup:
Synthetic Multiboot info with module table, module payloads, command line, boot loader string, mmap table, and APM/config pointers.

Assertions / pseudocode:
```text
memory_init(...)
repeat phys_alloc_page(&pa) until exhaustion
ASSERT(no_allocated_page_overlaps(multiboot_info))
ASSERT(no_allocated_page_overlaps(module_table))
ASSERT(no_allocated_page_overlaps(module_payloads))
ASSERT(no_allocated_page_overlaps(cmdline_or_loader_name_pages))
ASSERT(no_allocated_page_below(0x100000))
```

Success cases:
- All enumerated live boot structures remain reserved.

Failure cases:
- Any returned page overlaps reserved boot payloads.

## Ordinary Allocation Preserves Legacy DMA Reserve

Summary:
Verify that unconstrained allocations do not consume pages below the preserved DMA threshold.

Purpose:
Current allocator deliberately keeps ordinary allocations above `LEGACY_DMA_RESERVE_LIMIT`. This is a critical policy and should not regress silently.

Setup:
Initialize with enough RAM below and above 16 MiB.

Assertions / pseudocode:
```text
memory_init(...)
for i in 0..N:
  ASSERT(phys_alloc_page(&pa) == true)
  ASSERT(pa >= 0x01000000)
```

Success cases:
- General allocations come only from the general pool.

Failure cases:
- Ordinary allocation falls back into legacy DMA reserve.

## Constrained Low-Memory Allocation

Summary:
Verify that explicit constrained allocation can return low physical pages when requested.

Purpose:
The code exposes `phys_alloc_page_below()` and `phys_alloc_pages_below()` specifically so low memory is still usable when explicitly requested.

Setup:
Memory map with usable pages below 16 MiB.

Assertions / pseudocode:
```text
memory_init(...)
ASSERT(phys_alloc_page_below(0x01000000, &pa) == true)
ASSERT(pa < 0x01000000)
ASSERT(phys_alloc_pages_below(4, 0x01000000, &run) == true)
ASSERT(run < 0x01000000)
ASSERT(run_is_page_aligned_and_contiguous)
```

Success cases:
- Explicit low-memory requests succeed.

Failure cases:
- API never returns low pages even when available.
- Returned run crosses the requested limit.

## Physical Allocation Exhaustion And Reuse

Summary:
Verify clean failure on exhaustion and correct reuse after release.

Purpose:
This catches false-success allocation, stale free counters, and pages that never re-enter the free pool.

Setup:
Use a tiny synthetic RAM configuration.

Assertions / pseudocode:
```text
initial_free = phys_free_page_count()
allocate_until_failure(record_pages)
ASSERT(phys_alloc_page(&pa) == false)
ASSERT(phys_free_page_count() == 0)
for each page in record_pages: phys_page_put(page)
ASSERT(phys_free_page_count() == initial_free)
ASSERT(phys_alloc_page(&pa2) == true)
ASSERT(pa2 in record_pages)
```

Success cases:
- Exhaustion fails cleanly.
- Freed pages are reusable.

Failure cases:
- Allocation succeeds outside usable RAM.
- Free count is wrong.
- Pages are leaked after release.

## Physical Refcount Semantics

Summary:
Verify `phys_page_get()` and `phys_page_put()` maintain physical lifetime independently of mapping ownership.

Purpose:
This is foundational for shared mappings and future VM objects. A page must remain allocated until its final physical reference is released.

Setup:
Allocate one page.

Assertions / pseudocode:
```text
ASSERT(phys_alloc_page(&pa) == true)
free_before = phys_free_page_count()
phys_page_get(pa)
phys_page_put(pa)
ASSERT(phys_free_page_count() == free_before)
phys_page_put(pa)
ASSERT(phys_free_page_count() == free_before + 1)
ASSERT(phys_alloc_page(&pa2) == true)
ASSERT(pa2 == pa or pa2_is_valid_reused_page)
```

Success cases:
- Intermediate puts do not free the page.
- Final put returns it to allocator.

Failure cases:
- Page frees too early.
- Page never frees.

## Invalid Physical Refcount Operations

Summary:
Verify impossible physical-page reference operations are detected.

Purpose:
Allocator misuse should fail loudly in debug/assert-enabled test builds.

Setup:
Create harness variants expecting `memory_panic()` or equivalent trap capture.

Assertions / pseudocode:
```text
ASSERT_PANIC(phys_page_get(unaligned_page))
ASSERT_PANIC(phys_page_put(unaligned_page))
ASSERT_PANIC(phys_page_get(unmanaged_page))
ASSERT_PANIC(phys_page_put(unmanaged_page))
ASSERT(phys_alloc_page(&pa) == true)
phys_page_put(pa)
ASSERT_PANIC(phys_page_put(pa))
ASSERT_PANIC(phys_page_get(pa))
```

Success cases:
- Impossible refcount transitions are rejected.

Failure cases:
- Silent double-free.
- Get-on-free succeeds.

## Address-Space Reference Semantics

Summary:
Verify `address_space_create()`, `address_space_retain()`, `address_space_release()`, and `address_space_references()` obey current lifetime rules.

Purpose:
Address-space lifetime must be independent of thread count and safe for auto-reap.

Setup:
Initialized allocator and kernel address space.

Assertions / pseudocode:
```text
as = address_space_create(ADDRESS_SPACE_REAP_WHEN_THREADLESS)
ASSERT(as != NULL)
ASSERT(address_space_references(as) == 1)
address_space_retain(as)
ASSERT(address_space_references(as) == 2)
address_space_release(as)
ASSERT(address_space_references(as) == 1)
address_space_release(as)
ASSERT(resources_returned_to_baseline)
ASSERT(address_space_kernel() != NULL)
ASSERT(address_space_references(address_space_kernel()) == UINT32_MAX or permanent_nonzero_semantics)
```

Success cases:
- Non-kernel address spaces are reference counted.
- Kernel address space is permanent.

Failure cases:
- Kernel address space is destructible.
- Refs underflow or leak.

## Mapping Transfer Semantics

Summary:
Verify `ADDRESS_SPACE_MAP_OWNED` transfers one existing caller-held physical reference, while a non-owned mapping acquires its own additional reference.

Purpose:
This is explicitly documented in `memory.h` and is easy to get wrong.

Setup:
Allocate one page and one address space.

Assertions / pseudocode:
```text
phys_alloc_page(&pa)
free0 = phys_free_page_count()
ASSERT(address_space_map_page(as, 0x400000, pa, PAGE_USER | ADDRESS_SPACE_MAP_OWNED) == 0)
ASSERT(phys_free_page_count() == free0)
ASSERT(address_space_unmap_page(as, 0x400000) == 0)
ASSERT(phys_free_page_count() == free0 + 1)

phys_alloc_page(&pb)
free1 = phys_free_page_count()
ASSERT(address_space_map_page(as, 0x401000, pb, PAGE_USER) == 0)
ASSERT(phys_free_page_count() == free1)
phys_page_put(pb)
ASSERT(phys_free_page_count() == free1)
ASSERT(address_space_unmap_page(as, 0x401000) == 0)
ASSERT(phys_free_page_count() == free1 + 1)
```

Success cases:
- Owned mapping consumes the caller’s existing reference by transfer.
- Borrowed mapping keeps the page alive independently.

Failure cases:
- Original allocation ref leaks forever.
- Borrowed mapping does not hold its own ref.

## Map/Unmap Translation Correctness

Summary:
Verify `address_space_map_page()`, `address_space_translate()`, and `address_space_unmap_page()` are mutually consistent.

Purpose:
This catches stale mappings, wrong permissions handling, and false-success translate results.

Setup:
One fresh non-kernel address space.

Assertions / pseudocode:
```text
ASSERT(address_space_translate(as, 0x400000) == UINT64_MAX)
phys_alloc_page(&pa)
ASSERT(address_space_map_page(as, 0x400000, pa, PAGE_USER | ADDRESS_SPACE_MAP_OWNED) == 0)
ASSERT(address_space_translate(as, 0x400000) == pa)
ASSERT(address_space_unmap_page(as, 0x400000) == 0)
ASSERT(address_space_translate(as, 0x400000) == UINT64_MAX)
ASSERT(address_space_unmap_page(as, 0x400000) != 0)
```

Success cases:
- Translation appears after map and disappears after unmap.

Failure cases:
- Stale translation after unmap.
- Duplicate unmap incorrectly succeeds.

## Mapping Argument Validation

Summary:
Verify mapping and unmapping APIs reject unaligned, duplicate, and out-of-range virtual addresses or invalid physical addresses.

Purpose:
These are easy false-success bugs in VM code and can later become silent corruption.

Setup:
One fresh non-kernel address space plus one allocated page.

Assertions / pseudocode:
```text
phys_alloc_page(&pa)
ASSERT(address_space_map_page(NULL, 0x400000, pa, PAGE_USER) != 0)
ASSERT(address_space_map_page(as, 0x400001, pa, PAGE_USER) != 0)
ASSERT(address_space_map_page(as, USER_CANONICAL_TOP, pa, PAGE_USER) != 0)
ASSERT(address_space_map_page(as, 0x400000, pa + 1, PAGE_USER) != 0)
ASSERT(address_space_map_page(as, 0x400000, pa, PAGE_USER | ADDRESS_SPACE_MAP_OWNED) == 0)
ASSERT(address_space_map_page(as, 0x400000, pa, PAGE_USER) != 0)
ASSERT(address_space_unmap_page(NULL, 0x400000) != 0)
ASSERT(address_space_unmap_page(as, 0x400001) != 0)
```

Success cases:
- Invalid arguments are rejected without mutation.

Failure cases:
- Unaligned or duplicate mappings succeed.
- Failed operations partially mutate page tables or refcounts.

## Sparse Map/Unmap Reclaims Intermediate Page Tables

Summary:
Verify repeated sparse map/unmap in a long-lived address space does not leak PT/PD/PDPT pages.

Purpose:
Current code prunes empty paging levels on unmap. This must be stress-tested separately from address-space destruction.

Setup:
One long-lived address space. Multiple sparse virtual addresses spread across different PT/PD/PDPT boundaries.

Assertions / pseudocode:
```text
baseline = phys_pages_in_use()
repeat many times:
  map pages at sparse VAs
  unmap all of them
ASSERT(phys_pages_in_use() == baseline)
```

Success cases:
- Empty lower-level tables are reclaimed.

Failure cases:
- Page-table pages leak over time.

## ksbrk Growth, Shrink, And Rollback

Summary:
Verify the dynamic kernel heap grows, maps pages lazily, shrinks, and rolls back on failure.

Purpose:
This catches partially grown heap state, wrong break accounting, and leaked heap mappings.

Setup:
Initialized kernel address space and allocator. For rollback, use a tiny available page pool.

Assertions / pseudocode:
```text
base = kernel_heap_break()
ASSERT(ksbrk(0) == base)
ASSERT(ksbrk(100) == base)
ASSERT(kernel_heap_break() == base + 100)
ASSERT(ksbrk(PAGE_SIZE * 2) != (void *)-1)
ASSERT(kernel_heap_break() == expected)
ASSERT(ksbrk(-100) != (void *)-1)
ASSERT(kernel_heap_break() == expected_minus_100)

exhaust_pages_to_force_failure()
old = kernel_heap_break()
ASSERT(ksbrk(PAGE_SIZE * 4) == (void *)-1)
ASSERT(kernel_heap_break() == old)
ASSERT(no_new_heap_pages_remain_mapped)
```

Success cases:
- Heap grows and shrinks across page boundaries.
- Failure leaves prior break intact.

Failure cases:
- Partial growth committed on failure.
- Shrink leaks pages.

## kmalloc/kfree Reuse And Coalescing

Summary:
Verify `kmalloc()` and `kfree()` reuse freed chunks and coalesce adjacent frees.

Purpose:
The current allocator is simple and should be directly validated before relying on it for thread/address-space lifetimes.

Setup:
Fresh heap state or isolated test process.

Assertions / pseudocode:
```text
a = kmalloc(64)
b = kmalloc(64)
c = kmalloc(64)
ASSERT(all_non_null_and_distinct)
kfree(b)
d = kmalloc(64)
ASSERT(d == b or same_chunk_reused)
kfree(a)
kfree(c)
kfree(d)
large = kmalloc(192)
ASSERT(large != NULL)
ASSERT(no_heap_page_leak_if_entire_region_freed)
```

Success cases:
- Freelist reuse works.
- Adjacent free chunks coalesce.

Failure cases:
- Permanent fragmentation in trivial case.
- Double-free not detected.

## Program Image Mapping Rollback

Summary:
Verify `program_map_flat_image()` and `program_map_user_stack()` roll back fully on allocation or mapping failure.

Purpose:
These loaders are convenience layers over VM primitives and must not leave half-built user spaces behind.

Setup:
Synthetic one-page and multi-page images, plus forced allocation failure after N pages.

Assertions / pseudocode:
```text
as = address_space_create(0)
baseline = phys_pages_in_use()
force_failure_after_one_page()
ASSERT(program_map_flat_image(as, image_2pages, 0x400000) != 0)
ASSERT(address_space_translate(as, 0x400000) == UINT64_MAX)
ASSERT(address_space_translate(as, 0x401000) == UINT64_MAX)
ASSERT(phys_pages_in_use() == baseline)
```

Success cases:
- No partial program or stack mappings survive failed setup.

Failure cases:
- Leaked pages or leftover mappings after failure.

## Program API Argument Validation

Summary:
Verify `program_map_flat_image()`, `program_map_user_stack()`, and `program_load_and_start()` reject invalid arguments cleanly.

Purpose:
These are public Sharkix convenience APIs and should fail atomically on bad input.

Setup:
Synthetic image buffers and option structures.

Assertions / pseudocode:
```text
ASSERT(program_map_flat_image(NULL, &image, 0x400000) != 0)
ASSERT(program_map_flat_image(as, NULL, 0x400000) != 0)
ASSERT(program_map_flat_image(as, &empty_image, 0x400000) != 0)
ASSERT(program_map_flat_image(as, &image, 0x400001) != 0)
ASSERT(program_map_user_stack(as, 0x800001, PAGE_SIZE, &top) != 0)
ASSERT(program_map_user_stack(NULL, 0x800000, PAGE_SIZE, &top) != 0)
ASSERT(program_load_and_start(&image, NULL, NULL, NULL) != 0)
ASSERT(program_load_and_start(&image, kernel_privilege_options, NULL, NULL) != 0)
```

Success cases:
- Invalid program-load inputs fail without partial setup.

Failure cases:
- Invalid inputs succeed.
- Failed setup leaks mappings, pages, or thread objects.

## Thread Lifecycle State Machine

Summary:
Verify the public Sharkix thread states and legal transitions as currently implemented.

Purpose:
This catches accidental state corruption and false-success lifecycle operations.

Setup:
Stub FreeRTOS creation/suspend/resume/delete hooks in a host harness.

Assertions / pseudocode:
```text
t = thread_create(as, THREAD_PRIVILEGE_KERNEL, params)
ASSERT(t != NULL)
ASSERT(thread_get_state(t->id) == THREAD_STATE_READY)
ASSERT(thread_start(t) == 0)
ASSERT(thread_get_state(t->id) == THREAD_STATE_RUNNABLE)
ASSERT(thread_start(t) != 0)
thread_destroy_unstarted(t_ready_only_case)
ASSERT(thread_get_state(id) == THREAD_STATE_INVALID after reaper_or_direct_cleanup_semantics)
```

Success cases:
- `NEW -> READY -> RUNNABLE` via create/start.
- Duplicate start rejected.

Failure cases:
- Created thread runs before `thread_start()`.
- Invalid transitions silently succeed.

## Thread Registry And Identity Queries

Summary:
Verify `thread_lookup()`, `thread_get_state()`, and `thread_current_id()` match current documented lifetime semantics.

Purpose:
Thread identity is the authoritative kernel-visible caller identity for syscalls and diagnostics.

Setup:
Host harness or booted kernel with one managed running thread plus one unstarted thread.

Assertions / pseudocode:
```text
t = thread_create(as, THREAD_PRIVILEGE_KERNEL, params)
ASSERT(thread_lookup(t->id) == t)
ASSERT(thread_get_state(t->id) == THREAD_STATE_READY)
ASSERT(thread_lookup(nonexistent_id) == NULL)
ASSERT(thread_get_state(nonexistent_id) == THREAD_STATE_INVALID)
cpu0.current_thread = t
ASSERT(thread_current() == t)
ASSERT(thread_current_id() == t->id)
cpu0.current_thread = NULL
ASSERT(thread_current_id() == 0)
```

Success cases:
- Registry lookup and current-thread identity are coherent.

Failure cases:
- Invalid IDs resolve as live.
- `thread_current_id()` returns stale IDs.

## Privilege/Address-Space Validation

Summary:
Verify the legal and illegal thread privilege/address-space combinations enforced by `thread_create()`.

Purpose:
The current model allows:
`kernel AS + CPL0`, `private AS + CPL0`, `private AS + CPL3`
and rejects:
`kernel AS + CPL3`

Setup:
Kernel address space plus one created non-kernel address space.

Assertions / pseudocode:
```text
ASSERT(thread_create(address_space_kernel(), THREAD_PRIVILEGE_KERNEL, kernel_params) != NULL)
ASSERT(thread_create(private_as, THREAD_PRIVILEGE_KERNEL, kernel_params) != NULL)
ASSERT(thread_create(private_as, THREAD_PRIVILEGE_USER, user_params) != NULL)
ASSERT(thread_create(address_space_kernel(), THREAD_PRIVILEGE_USER, user_params) == NULL)
```

Success cases:
- Valid combinations accepted.

Failure cases:
- Kernel-AS user thread created.
- Valid private-AS kernel thread rejected.

## Block/Wake API Semantics

Summary:
Verify `thread_block_current()`, `thread_wake()`, and `thread_get_blocked_syscall_context()` semantics at the Sharkix layer.

Purpose:
These are the substrate for future blocking syscalls and later IPC.

Setup:
Host harness with stubbed suspend/resume/yield and a fake current thread.

Assertions / pseudocode:
```text
cpu0.current_thread = t_running
ctx = local_syscall_ctx
ASSERT(thread_block_current(&ctx) returns via wake path only)
ASSERT(t_running.state == THREAD_STATE_BLOCKED during suspension)
ASSERT(thread_get_blocked_syscall_context(t_running) == &ctx)
ASSERT(thread_wake(t_running) == 0)
ASSERT(t_running.state == THREAD_STATE_RUNNABLE)
ASSERT(thread_wake(t_running) != 0)
```

Success cases:
- Only blocked threads can be woken.

Failure cases:
- Wake of non-blocked thread succeeds.
- Block loses the authoritative syscall context pointer.

## Scheduler Preparation And Preemption Policy

Summary:
Verify `thread_prepare_current()` and `thread_timer_may_preempt_current()` maintain Sharkix thread state coherently across managed and unmanaged tasks.

Purpose:
These hooks are the bridge between FreeRTOS scheduling and Sharkix state, so contradictions here can invalidate many higher-level tests.

Setup:
Host harness with synthetic `thread_t` objects and one unmanaged-idle case.

Assertions / pseudocode:
```text
cpu0.current_thread = running_kernel_thread
ASSERT(thread_prepare_current(runnable_user_thread) == runnable_user_thread->address_space->pml4_phys)
ASSERT(running_kernel_thread.state == THREAD_STATE_RUNNABLE)
ASSERT(runnable_user_thread.state == THREAD_STATE_RUNNING)
ASSERT(cpu0.current_thread == runnable_user_thread)
ASSERT(cpu0.kernel_stack_top == runnable_user_thread->kernel_stack_top)

ASSERT(thread_prepare_current(NULL) == address_space_kernel()->pml4_phys)
ASSERT(thread_current() == NULL)
ASSERT(cpu0.kernel_stack_top == 0)

cpu0.current_thread = kernel_thread
ASSERT(thread_timer_may_preempt_current() == 0)
cpu0.current_thread = user_thread
ASSERT(thread_timer_may_preempt_current() == 1)
cpu0.current_thread = NULL
ASSERT(thread_timer_may_preempt_current() == 1)
```

Success cases:
- Running thread becomes runnable when switched away.
- Incoming runnable thread becomes running.
- Unmanaged idle task clears stale Sharkix current-thread state.
- PIT preemption policy matches current kernel-vs-user rule.

Failure cases:
- READY/BLOCKED/DEAD thread is accidentally treated as runnable.
- Idle execution inherits stale managed-thread state.

# 2. Booted Kernel-Space Tests

These tests boot Sharkix in QEMU but remain in kernel space. They are for real page-table behaviour, guard pages, CR3 switching for kernel threads, interrupt/yield behaviour, and boot-time hardware setup.

## Kernel Boot Banner And ELF Layout

Summary:
Verify the built image is a bootable ELF64 higher-half kernel and reaches `kernel_high_entry()`.

Purpose:
This is the most basic integration gate for every profile.

Setup:
Build `kernel.elf`, inspect via `readelf`, boot through `bootstub32`, and boot through GRUB ISO.

Assertions / pseudocode:
```text
ASSERT(readelf_machine == x86-64)
ASSERT(load_segments_include_higher_half_around_KERNEL_BASE)
boot_qemu_bootstub32()
ASSERT(output_contains("SharkKernel x86_64"))
ASSERT(output_contains("kernel virtual base: 0xffffffff80000000"))
boot_qemu_grub_iso()
ASSERT(same_kernel_reaches_banner)
```

Success cases:
- Same kernel boots by both boot paths.

Failure cases:
- Wrong ELF class/arch.
- Wrong virtual base.
- One boot path diverges.

## Header Dependency Tracking

Summary:
Verify header changes rebuild exactly the affected objects without requiring `make clean`.

Purpose:
The Makefile currently uses generated dependency files. This should stay correct because stale kernel objects can invalidate every other test result.

Setup:
Use a disposable build tree or timestamp-based test run.

Assertions / pseudocode:
```text
make PROFILE=normal
record object mtimes
touch include/sharkix/kernel/thread.h
make PROFILE=normal
ASSERT(thread_dependent_objects_rebuilt)
ASSERT(unrelated_objects_not_rebuilt_unnecessarily)

touch include/FreeRTOSConfig.h
make PROFILE=normal
ASSERT(all_or_expected_global_dependents_rebuilt)
```

Success cases:
- Header edits trigger rebuilds of affected objects.

Failure cases:
- Stale objects survive header changes.

## Physmap Translation Sanity

Summary:
Verify the direct-map translation identity currently checked in `kernel_high_entry()`.

Purpose:
This catches early physmap breakage before more complex tests run.

Setup:
Boot any profile.

Assertions / pseudocode:
```text
ASSERT(output_contains("physmap translation: ok"))
```

Success cases:
- `virt_to_phys(phys_to_virt(VGA_PHYS)) == VGA_PHYS`

Failure cases:
- Direct-map helpers broken.

## Physical RAM Configuration Matrix

Summary:
Boot Sharkix under several QEMU RAM sizes and verify allocator statistics remain sensible.

Purpose:
Current allocator derives metadata and managed pages from the real boot memory map. It should be stressed around small and unusual RAM sizes.

Setup:
Run selected profiles with QEMU `-m` values such as:

- minimum supported size
- minimum plus 1 page
- 17 MiB
- 31 MiB
- 33 MiB
- 64 MiB
- 512 MiB
- 1 GiB if practical

Assertions / pseudocode:
```text
for ram_size in matrix:
  boot()
  ASSERT(parsed_ram_bytes_is_nonzero)
  ASSERT(managed_pages * PAGE_SIZE <= reported_ram_bytes)
  ASSERT(free_pages <= managed_pages)
  ASSERT(boot_succeeds_or_fails_cleanly_below_supported_minimum)
```

Success cases:
- Statistics remain internally consistent.

Failure cases:
- Wraparound or metadata overlap at boundary sizes.

## Kernel Thread In Private Address Space

Summary:
Verify a CPL0 thread can run in a non-kernel address space and actually uses that CR3.

Purpose:
This is a current Sharkix invariant proved partly by `PROFILE=vm`; it deserves a dedicated test case.

Setup:
Use or strengthen the current `vm` profile.

Assertions / pseudocode:
```text
create private_as
create CPL0 thread in private_as
start thread
ASSERT(thread executes)
ASSERT(observed_cr3 == private_as->pml4_phys)
ASSERT(thread_current()->privilege == THREAD_PRIVILEGE_KERNEL)
ASSERT(thread_current()->address_space == private_as)
```

Success cases:
- Privilege and address-space identity remain independent.

Failure cases:
- CPL0 thread is forced onto kernel CR3.

## Idle Task Current-Thread Semantics

Summary:
Verify the unmanaged FreeRTOS idle task does not inherit stale Sharkix thread identity, kernel stack top, or TSS state.

Purpose:
`thread_prepare_current(NULL)` is a deliberate part of the current design and should be tested explicitly.

Setup:
Boot a profile where all managed work quiesces long enough for idle to run, plus a monitor thread.

Assertions / pseudocode:
```text
wait_until_idle_executes
ASSERT(thread_current() == NULL while idle is active)
ASSERT(cpu_local.kernel_stack_top == 0 while idle is active)
ASSERT(active_cr3 == address_space_kernel()->pml4_phys)
```

Success cases:
- Idle execution has coherent unmanaged semantics.

Failure cases:
- Idle sees stale current-thread identity from a prior managed task.

## Kernel Stack Guard Fault

Summary:
Verify a kernel stack guard page actually faults rather than silently corrupting adjacent memory.

Purpose:
Current kernel stacks are virtually mapped allocations with a guard gap. This needs a dedicated destructive test.

Setup:
Dedicated kernel-only overflow test profile or a targeted kernel helper.

Assertions / pseudocode:
```text
create kernel thread with small kernel stack
deliberately recurse_or_probe_below_stack_base
ASSERT(kernel fault path identifies kernel exception)
ASSERT(fault_address lands in stack guard page)
ASSERT(no adjacent thread metadata corruption observed before halt)
```

Success cases:
- Overflow faults at guard boundary.

Failure cases:
- Silent corruption.
- Fault address lands inside mapped stack unexpectedly.

## TLB Invalidates On Kernel Mapping Removal

Summary:
Verify stale kernel translations disappear immediately after unmapping.

Purpose:
The current VM code uses `invlpg`; this must be validated directly for live mappings.

Setup:
Kernel-only test that maps a page in the kernel heap or test range, accesses it, unmaps it, then re-accesses it.

Assertions / pseudocode:
```text
map kernel VA -> page
write/read succeeds
unmap
access same VA
ASSERT(kernel page fault occurs immediately at that VA)
```

Success cases:
- Removed translation is not retained in TLB.

Failure cases:
- Access still succeeds after unmap.

## Kernel Heap Growth Under Load

Summary:
Verify `ksbrk()` and `kmalloc()` can expand beyond the historical fixed 64 KiB allocator ceiling.

Purpose:
This catches regression back to heap-size assumptions and validates dynamic kernel allocation in the live kernel.

Setup:
Kernel-only stress profile allocating well beyond 64 KiB in varying chunk sizes.

Assertions / pseudocode:
```text
baseline = phys_pages_in_use()
allocate_total > 64KiB through kmalloc()
ASSERT(all_allocations_non_null)
free_all()
ASSERT(phys_pages_in_use() returns to baseline or expected allocator-retained-state)
```

Success cases:
- Kernel allocations continue past 64 KiB.

Failure cases:
- Unexpected early exhaustion.
- Leak after freeing.

## Thread Reaper Always Present

Summary:
Verify deferred cleanup does not depend on a profile forgetting to create a reaper.

Purpose:
Current code creates the reaper in `startup_common_init()`, which is already called from `kernel_high_entry()`. This should remain guaranteed.

Setup:
Boot each profile, especially ones that do not explicitly call `startup_reaper()` before doing useful work.

Assertions / pseudocode:
```text
boot profile
create exiting thread
wait
ASSERT(thread_reaped_count() increments)
```

Success cases:
- Cleanup occurs without profile-local reaper setup.

Failure cases:
- Exiting thread remains permanently terminating/dead.

# 3. Single-Usermode-Task Tests

These tests require CPL3 entry but only one user thread/address space at a time.

## Single User Task Basic Syscall Loop

Summary:
Verify one CPL3 thread repeatedly issues `SYSCALL_TEST_WRITE` and receives its thread ID in `RAX`.

Purpose:
This is the basic syscall smoke test and should be narrower than the current two-task profile.

Setup:
One user image that writes one marker, checks `RAX`, yields, and repeats.

Assertions / pseudocode:
```text
launch single user task
ASSERT(first syscall prints marker)
ASSERT(kernel logs one caller thread ID announcement)
ASSERT(repeated syscalls continue without fault)
```

Success cases:
- User entry, syscall entry, syscall return, and yield loop all work.

Failure cases:
- Wrong caller ID.
- Syscall path corrupts registers or return state.

## Invalid Syscall Number Safe Rejection

Summary:
Verify an undefined syscall number returns failure without corrupting thread state.

Purpose:
Current `dispatch_syscall()` returns `UINT64_MAX` for unsupported syscalls. This should be explicit.

Setup:
One user payload issuing an unmapped positive syscall number and an unimplemented negative IPC/VM syscall number.

Assertions / pseudocode:
```text
syscall invalid_positive
ASSERT(RAX == UINT64_MAX)
syscall SYSCALL_IPC_CREATE or SYSCALL_VM_MAP current_placeholder
ASSERT(RAX == UINT64_MAX)
ASSERT(thread remains runnable)
```

Success cases:
- Unsupported syscalls fail safely.

Failure cases:
- Unsupported syscall halts kernel.
- Placeholder negative syscalls accidentally succeed.

## User Exit Syscall

Summary:
Verify `SYSCALL_TEST_EXIT` terminates the current userspace thread and cleanup completes.

Purpose:
This exercises the normal userspace thread termination path without concurrency.

Setup:
One user image identical in spirit to `src/user/tests/exit.s`.

Assertions / pseudocode:
```text
start user task
capture tid
ASSERT(thread_get_state(tid) transitions RUNNABLE/RUNNING -> TERMINATING -> DEAD -> INVALID)
ASSERT(thread_reaped_count() increments)
ASSERT(phys_pages_in_use() returns to baseline)
```

Success cases:
- Userspace exit works and reaps correctly.

Failure cases:
- Thread never leaves terminating state.
- Resources leak.

## User Initial Context And Stack Entry

Summary:
Verify a newly created CPL3 thread starts at the requested RIP with the requested initial user stack pointer.

Purpose:
`thread_create()` synthesizes the initial kernel-stack frame for CPL3 entry. Errors here can look like random user faults.

Setup:
One user payload that reports success only if its initial stack lies in the expected stack mapping and execution begins at the expected entrypoint.

Assertions / pseudocode:
```text
map image at load_address
map stack at chosen stack_base
create user thread with entry_rip and initial_stack_pointer
start thread
ASSERT(first observable action from user payload proves expected entry path executed)
ASSERT(no immediate #PF/#GP due to malformed initial frame)
```

Success cases:
- User initial RIP/RSP synthesis is correct.

Failure cases:
- Thread faults before executing intended payload.

## User Invalid Opcode Exception

Summary:
Verify a CPL3 `ud2` faults only the offending task.

Purpose:
This validates synchronous exception containment.

Setup:
One user payload from `src/user/tests/ud.s`.

Assertions / pseudocode:
```text
launch ud_task
ASSERT(kernel logs vector 6 and cpl 3)
ASSERT(task is terminated and reaped)
ASSERT(kernel remains alive)
```

Success cases:
- `#UD` from CPL3 is isolated.

Failure cases:
- Kernel halts globally.

## User Null Page Fault

Summary:
Verify a CPL3 load from address 0 triggers `#PF` and kills only that task.

Purpose:
Validates unmapped-user-page fault handling.

Setup:
One user payload from `src/user/tests/pagefault.s`.

Assertions / pseudocode:
```text
launch pf_task
ASSERT(kernel logs vector 14, cpl 3, fault_address == 0)
ASSERT(task terminated)
ASSERT(kernel survives)
```

Success cases:
- Unmapped lower-half fault is contained.

Failure cases:
- Kernel halts.
- Wrong fault address reported.

## User Supervisor-Address Access Fault

Summary:
Verify a CPL3 access to `KERNEL_BASE` faults and does not expose kernel memory.

Purpose:
This checks that upper-half kernel mappings remain supervisor-only.

Setup:
One user payload from `src/user/tests/kernel_access.s`.

Assertions / pseudocode:
```text
launch kernel_access_task
ASSERT(kernel logs vector 14 or appropriate protection fault with cpl 3)
ASSERT(task terminated)
ASSERT(no kernel memory read succeeds)
```

Success cases:
- Supervisor protection is enforced.

Failure cases:
- User can read kernel image mapping.

## User Privileged Instruction Fault

Summary:
Verify a CPL3 privileged instruction such as `cli` or `hlt` faults and kills only that task.

Purpose:
Current `#GP` handling should be exercised directly, not only inferred.

Setup:
Dedicated user payload executing a privileged instruction.

Assertions / pseudocode:
```text
launch gp_task
ASSERT(kernel logs vector 13 and cpl 3)
ASSERT(task terminated and reaped)
ASSERT(kernel survives)
```

Success cases:
- `#GP` from userspace is isolated.

Failure cases:
- Userspace `#GP` reaches fatal kernel halt path.

## Syscall Register Preservation And Result Mapping

Summary:
Verify all Sharkix syscall result registers are returned correctly and non-result state remains coherent across repeated calls.

Purpose:
The syscall ABI is register-structured, not pointer-based. This test complements `syscall_block` by covering non-blocking return paths.

Setup:
One CPL3 payload issuing `SYSCALL_TEST_WRITE` repeatedly while checking preserved/returned registers around the call.

Assertions / pseudocode:
```text
set known values in caller-visible registers
issue SYSCALL_TEST_WRITE
ASSERT(RAX == caller_thread_id)
ASSERT(observed_result_registers match syscall contract)
ASSERT(repeated invocations do not drift or corrupt stack/return state)
```

Success cases:
- Non-blocking syscall return path is stable.

Failure cases:
- Wrong register mapping on return.
- Repeated calls slowly corrupt user context.

## User Stack Guard Fault

Summary:
Verify a userspace stack overflow or access below the configured guard page faults cleanly.

Purpose:
Current user stacks are mapped above an unmapped guard page selected by the loader. That behaviour should be explicitly tested.

Setup:
One user payload that walks downward below `PROGRAM_DEFAULT_STACK_BASE`.

Assertions / pseudocode:
```text
launch stack_guard_task
ASSERT(fault_address lands in unmapped guard page below user stack)
ASSERT(task terminated)
ASSERT(other kernel state unaffected)
```

Success cases:
- Guard page is real.

Failure cases:
- User stack silently corrupts adjacent mapping.

# 4. Multiple-Usermode-Task Tests

These tests cover concurrency, shared vs separate address spaces, blocking/waking, scheduler-visible state transitions, and resource cleanup across interacting threads.

## Two Independent User Tasks In Separate Address Spaces

Summary:
Verify two CPL3 threads run concurrently with distinct CR3 roots and distinct lower-half code pages.

Purpose:
This is the current `PROFILE=syscall` scenario and remains a core regression test.

Setup:
Launch `taskA` and `taskB` via `program_load_and_start()`.

Assertions / pseudocode:
```text
ASSERT(a->id != b->id)
ASSERT(a->address_space != b->address_space)
ASSERT(a->address_space->pml4_phys != b->address_space->pml4_phys)
ASSERT(address_space_translate(a->address_space, 0x400000) !=
       address_space_translate(b->address_space, 0x400000))
ASSERT(serial_output_contains_A_and_B_repeatedly)
```

Success cases:
- Separate lower-half mappings are isolated.

Failure cases:
- Same CR3 used for both.
- Same physical code page unintentionally shared.

## Two CPL3 Threads Sharing One Address Space

Summary:
Verify two userspace threads can share one address space while keeping distinct thread identities and separate stacks.

Purpose:
This is the current `PROFILE=two_tasks_one_space` scenario and is critical because caller identity must not be inferred from CR3.

Setup:
One address space, two images mapped at different virtual addresses, two user stacks, two threads.

Assertions / pseudocode:
```text
ASSERT(a->id != b->id)
ASSERT(a->address_space == b->address_space)
ASSERT(a->address_space->pml4_phys == b->address_space->pml4_phys)
ASSERT(stack_a != stack_b)
ASSERT(a->kernel_stack_top != b->kernel_stack_top)
ASSERT(serial_output_contains_A_and_B)
ASSERT(syscall_caller_ids_distinct_even_with_same_CR3)
```

Success cases:
- Shared CR3 does not collapse per-thread identity.

Failure cases:
- One thread’s stack or identity corrupts the other’s.

## Mixed Kernel And User Scheduling

Summary:
Verify kernel threads and user threads can coexist, yield, and continue making progress without corrupting one another’s CR3, stack, or identity state.

Purpose:
The current kernel intentionally runs both CPL0 and CPL3 Sharkix threads under one FreeRTOS scheduler. Mixed scheduling deserves explicit coverage.

Setup:
One kernel spinner thread, one shared-AS user thread, and one separate-AS user thread.

Assertions / pseudocode:
```text
start kernel_thread + user_thread_A + user_thread_B
ASSERT(kernel marker continues)
ASSERT(user markers continue)
ASSERT(kernel thread runs with kernel or intended private AS CR3 as configured)
ASSERT(user threads resume with their own CR3 and thread IDs)
```

Success cases:
- Mixed privilege scheduling is stable.

Failure cases:
- One class of thread starves or resumes with the wrong machine state.

## Created But Not Started Thread

Summary:
Verify `thread_create()` alone does not make a thread execute before `thread_start()`.

Purpose:
This is a required semantic distinction in the current object model.

Setup:
Create one CPL3 or CPL0 thread, delay `thread_start()`, observe for a period, then start it.

Assertions / pseudocode:
```text
t = thread_create(...)
ASSERT(t != NULL)
ASSERT(thread_get_state(t->id) == THREAD_STATE_READY)
wait_scheduler_intervals()
ASSERT(no_task_output_from_t)
ASSERT(thread_start(t) == 0)
ASSERT(task_output_appears_after_start)
```

Success cases:
- Ready thread remains non-running.

Failure cases:
- Thread executes before explicit start.

## Auto-Reap Address Space

Summary:
Verify an ephemeral address space with no retained external reference disappears naturally after its final thread exits.

Purpose:
This is current `program_load_and_start(... reap_on_exit=1 ...)` behaviour.

Setup:
Launch a short-lived user program without `out_address_space`.

Assertions / pseudocode:
```text
baseline_pages = phys_pages_in_use()
start ephemeral program with reap_on_exit=1 and no out_address_space
wait for thread_reaped_count increment
ASSERT(phys_pages_in_use() == baseline_pages)
```

Success cases:
- Final thread release allows address space destruction.

Failure cases:
- Address space survives with no refs.
- Pages leak.

## Retained Auto-Reap Address Space

Summary:
Verify a reap-on-exit address space remains alive if an explicit owning reference is retained.

Purpose:
Current convenience API documents this ownership explicitly.

Setup:
Use `program_load_and_start(..., &held_as, NULL)`.

Assertions / pseudocode:
```text
baseline = phys_pages_in_use()
ASSERT(program_load_and_start(... reap_on_exit=1, &held_as, NULL) == 0)
wait until thread reaped
ASSERT(phys_pages_in_use() > baseline)
address_space_release(held_as)
ASSERT(phys_pages_in_use() == baseline)
```

Success cases:
- External ownership prevents auto-destruction.

Failure cases:
- Address space vanishes despite retained ref.
- Releasing final ref does not reclaim pages.

## Blocking Syscall Continuation

Summary:
Verify a blocked syscall resumes through the original syscall-return path with a result supplied by another thread.

Purpose:
This is the current `PROFILE=syscall_block` scenario and one of the most important interaction tests in the tree.

Setup:
Blocker task issues `SYSCALL_TEST_BLOCK`, waker task issues `SYSCALL_TEST_WAKE`, monitor task observes thread states and reaping.

Assertions / pseudocode:
```text
start blocker, waker, monitor
ASSERT(syscall_block_test_invocations() == 1)
ASSERT(thread_get_state(blocker_id) == THREAD_STATE_BLOCKED while waiting)
waker supplies all result registers and wakes blocker
ASSERT(syscall_block_test_wakes() == 1)
ASSERT(blocker observes exact expected RAX/RDI/RSI/RDX/R10/R8/R9 values)
ASSERT(blocker handler not reinvoked after wake)
ASSERT(thread_get_state(blocker_id) == THREAD_STATE_INVALID after reaping)
ASSERT(thread_get_state(waker_id) == THREAD_STATE_INVALID after reaping)
```

Success cases:
- Persistent syscall return context works.
- Wake resumes original syscall path.

Failure cases:
- Handler runs twice.
- Wrong return registers.
- Blocked thread lost forever.

## Scheduler State Consistency Across Context Switches

Summary:
Verify `thread_current()`, `thread_get_state()`, TSS kernel stack, and CR3 remain coherent across repeated switches among kernel threads, shared-AS user threads, and separate-AS user threads.

Purpose:
This catches state bleed between scheduling classes.

Setup:
One kernel spinner, two shared-AS user tasks, two separate-AS user tasks, plus optional monitor thread.

Assertions / pseudocode:
```text
on each diagnostic sample:
  ASSERT(thread_current() matches actually running task)
  ASSERT(current running thread state == THREAD_STATE_RUNNING)
  ASSERT(non-running started live threads are RUNNABLE or BLOCKED, not READY)
  ASSERT(tss_rsp0 == thread_current()->kernel_stack_top for managed thread)
  ASSERT(cr3 == thread_current()->address_space->pml4_phys)
```

Success cases:
- CPU-local and scheduler-visible state remain coherent.

Failure cases:
- Stale current-thread pointer.
- Wrong TSS stack after switch.
- Wrong CR3 after switch.

## Repeated Lifecycle Stress

Summary:
Verify repeated create/load/start/exit/reap/reuse cycles do not leak pages or stale IDs.

Purpose:
This strengthens the current `lifecycle` profile into a systematic stress test.

Setup:
Repeat the current lifecycle scenario many times, potentially with more than 16 iterations.

Assertions / pseudocode:
```text
baseline_pages = phys_pages_in_use()
baseline_reaped = thread_reaped_count()
repeat N times:
  start exiting user program
  wait for reap
ASSERT(phys_pages_in_use() == baseline_pages)
ASSERT(thread_reaped_count() >= baseline_reaped + N)
ASSERT(no_old_thread_id_wrongly_reappears_as_live)
```

Success cases:
- Cleanup is stable over time.

Failure cases:
- Cumulative leak.
- Registry corruption.

## Repeated Shared-Address-Space Lifecycle

Summary:
Verify multiple threads in one address space can be created, started, exited, and reaped in varying orders without premature address-space destruction.

Purpose:
Current coverage for shared address spaces is mostly a steady-state run; lifecycle ordering should be exercised explicitly.

Setup:
One non-kernel address space, multiple user threads with separate stacks, explicit external AS ref held and then dropped later.

Assertions / pseudocode:
```text
as = address_space_create(REAP_WHEN_THREADLESS)
map program + stacks
t1 = thread_create(as, USER, ...)
t2 = thread_create(as, USER, ...)
thread_start(t1)
thread_start(t2)
terminate t1 first
ASSERT(as still alive)
terminate t2
ASSERT(as still alive if external ref held)
address_space_release(as)
ASSERT(resources return to baseline)
```

Success cases:
- Final thread does not destroy AS while refs remain.

Failure cases:
- Premature AS destruction.

## Exception Isolation With Survivor Task

Summary:
Verify one crashing CPL3 task does not kill unrelated runnable tasks.

Purpose:
Current `exceptions` profile already has a survivor kernel task; this should be strengthened into an explicit multi-task isolation test.

Setup:
One survivor task and several faulting user tasks.

Assertions / pseudocode:
```text
launch survivor + bad tasks
ASSERT(each bad task faults and is reaped)
ASSERT(survivor output continues after each failure)
ASSERT(kernel remains responsive after all bad tasks are gone)
```

Success cases:
- Fault isolation works under concurrent activity.

Failure cases:
- One user fault halts unrelated tasks.

## Mixed Blocking And Exception Interaction

Summary:
Verify a blocked thread and an exceptioning thread in another address space do not corrupt one another’s state.

Purpose:
This catches subtle interactions between blocked-syscall state, reaping, and exception cleanup.

Setup:
One blocker/waker pair plus one user task that faults while blocker is asleep.

Assertions / pseudocode:
```text
blocker enters BLOCKED
faulting task triggers #UD or #PF and dies
ASSERT(blocker remains BLOCKED)
waker supplies result and wakes blocker
ASSERT(blocker returns correctly and exits cleanly)
ASSERT(all resources reclaimed)
```

Success cases:
- Unrelated exception path does not disturb blocked syscall continuation.

Failure cases:
- Fault cleanup corrupts blocked thread context.

## RAM-Limit Lifecycle Tests

Summary:
Repeat lifecycle and map/unmap stress near low-memory limits and with fragmented RAM maps.

Purpose:
Many allocator and cleanup bugs only show up under tight resource pressure.

Setup:
Run `vm`, `lifecycle`, `two_tasks_one_space`, and `syscall_block` under several constrained RAM sizes and, where possible, boot memory maps with holes.

Assertions / pseudocode:
```text
for ram_config in constrained_matrix:
  boot selected profile
  ASSERT(no allocation outside usable RAM)
  ASSERT(clean failure instead of corruption on exhaustion)
  ASSERT(reclaimed pages return to baseline where expected)
```

Success cases:
- Tight-memory operation remains correct.

Failure cases:
- Corruption only visible under low memory.

## Repetition Targets

The following tests should be repeated many times rather than run once:

- `Sparse Map/Unmap Reclaims Intermediate Page Tables`
- `Physical Allocation Exhaustion And Reuse`
- `Physical Refcount Semantics`
- `ksbrk Growth, Shrink, And Rollback`
- `Repeated Lifecycle Stress`
- `Repeated Shared-Address-Space Lifecycle`
- `Blocking Syscall Continuation`
- `Exception Isolation With Survivor Task`
- `RAM-Limit Lifecycle Tests`

The goal is to expose:

- cumulative leaks
- stale registry state
- stale TLB state
- stack corruption
- scheduler-order-dependent failures
- refcount underflow/overflow

## Ambiguous Or Currently Unspecified Areas

The following areas should be called out explicitly instead of silently assuming semantics:

- Negative IPC and VM syscall numbers are declared in `syscalls.inc` and described in `docs/DESIGN.md`, but they are not implemented in `dispatch_syscall()`. Current tests should only assert safe rejection until code defines their behaviour.
- `thread_lookup()` is documented as a single-CPU diagnostic raw-pointer lookup valid only until the next deferred-reaper pass. Host-side and booted tests should not treat it as a durable ownership API.
- `program_load_and_start(..., out_thread)` currently returns an observing pointer valid until deferred reaping, not an owned reference. Tests should validate only those documented lifetime semantics.
- `phys_total_ram_bytes()` is as accurate as the Multiboot-1 handoff allows. Tests should define expectations in terms of current code:
  - with `MB_INFO_MMAP`, it is the sum of entries whose type is `MULTIBOOT_MEMORY_AVAILABLE`
  - with `MB_INFO_MEMORY`, it is `(mem_lower + mem_upper) * 1024`
- FreeRTOS internal behaviour outside Sharkix’s explicit wrapper points should not be treated as Sharkix API contracts unless Sharkix code documents them.
- No current public kernel interface exposes post-boot Multiboot module handoff to ordinary kernel code. That path should not receive a detailed test plan until the kernel-side interface is defined.

## Coverage Matrix

| Subsystem / interface group | Proposed tests |
| --- | --- |
| Boot path, linker, higher-half entry | Kernel Boot Banner And ELF Layout |
| Console and diagnostic formatting | Console Formatting Helpers |
| Multiboot memory info parsing | Boot Memory Accounting From Multiboot Mmap; Memory Info Fallback Without Mmap; Reserved-Range Exclusion; Physical RAM Configuration Matrix |
| Physical page statistics APIs | Boot Memory Accounting From Multiboot Mmap; Physical RAM Configuration Matrix; Physical Allocation Exhaustion And Reuse |
| General physical allocation | Ordinary Allocation Preserves Legacy DMA Reserve; Physical Allocation Exhaustion And Reuse |
| Constrained / low-memory physical allocation | Constrained Low-Memory Allocation; RAM-Limit Lifecycle Tests |
| Physical refcount API | Physical Refcount Semantics; Invalid Physical Refcount Operations; Mapping Transfer Semantics |
| Direct map helpers | Physmap Translation Sanity |
| Address-space references and lifetime | Address-Space Reference Semantics; Auto-Reap Address Space; Retained Auto-Reap Address Space; Repeated Shared-Address-Space Lifecycle |
| Map / unmap / translate | Map/Unmap Translation Correctness; Mapping Argument Validation; Sparse Map/Unmap Reclaims Intermediate Page Tables; TLB Invalidates On Kernel Mapping Removal |
| Page-table pruning | Sparse Map/Unmap Reclaims Intermediate Page Tables; RAM-Limit Lifecycle Tests |
| Kernel heap / `ksbrk()` | ksbrk Growth, Shrink, And Rollback; Kernel Heap Growth Under Load |
| `kmalloc()` / `kfree()` | kmalloc/kfree Reuse And Coalescing; Kernel Heap Growth Under Load |
| Guarded kernel stacks | Kernel Stack Guard Fault; Scheduler State Consistency Across Context Switches |
| Program mapping helpers | Program Image Mapping Rollback; Program API Argument Validation; User Stack Guard Fault; User Initial Context And Stack Entry |
| Thread object lifecycle | Thread Lifecycle State Machine; Thread Registry And Identity Queries; Created But Not Started Thread; Repeated Lifecycle Stress |
| Thread privilege/address-space rules | Privilege/Address-Space Validation; Kernel Thread In Private Address Space; Two Independent User Tasks In Separate Address Spaces; Two CPL3 Threads Sharing One Address Space |
| `thread_current()` / CPU-local state / TSS | Thread Registry And Identity Queries; Scheduler Preparation And Preemption Policy; Idle Task Current-Thread Semantics; Scheduler State Consistency Across Context Switches; Blocking Syscall Continuation |
| Deferred reaping | User Exit Syscall; Thread Reaper Always Present; Repeated Lifecycle Stress; Repeated Shared-Address-Space Lifecycle |
| Syscall dispatch ABI | Single User Task Basic Syscall Loop; Invalid Syscall Number Safe Rejection; Syscall Register Preservation And Result Mapping; Blocking Syscall Continuation |
| Blocking syscall continuation | Block/Wake API Semantics; Blocking Syscall Continuation; Mixed Blocking And Exception Interaction |
| CPL3 entry / return | Single User Task Basic Syscall Loop; Two Independent User Tasks In Separate Address Spaces; Two CPL3 Threads Sharing One Address Space |
| User exception handling | User Invalid Opcode Exception; User Null Page Fault; User Supervisor-Address Access Fault; User Privileged Instruction Fault; Exception Isolation With Survivor Task |
| Shared-vs-separate address spaces | Two Independent User Tasks In Separate Address Spaces; Two CPL3 Threads Sharing One Address Space; Repeated Shared-Address-Space Lifecycle |
| Mixed kernel/user execution | Mixed Kernel And User Scheduling |
| Build system and profile selection | Header Dependency Tracking; Kernel Boot Banner And ELF Layout; existing profile rows below |
| Existing startup profiles | `normal`: Kernel Boot Banner And ELF Layout; `syscall`: Two Independent User Tasks In Separate Address Spaces; `exceptions`: Exception Isolation With Survivor Task; `vm`: Kernel Thread In Private Address Space plus page-reclaim tests; `lifecycle`: Repeated Lifecycle Stress; `two_tasks_one_space`: Two CPL3 Threads Sharing One Address Space; `syscall_block`: Blocking Syscall Continuation |
| Placeholder IPC/VM syscall groups | Invalid Syscall Number Safe Rejection until implementation exists |

The matrix above is the completeness check. If a future subsystem or public interface is added and does not appear here, the testing plan should be updated at the same time.
