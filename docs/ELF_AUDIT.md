Here is the latest updated conclusions from codex as per 2nd October 2026 at 1:20am-ish, followed by the original audit from earlier tonight (technically yesterday):

The revised validate_elf_header() now accepts the payload and is substantially safer. The profile builds and runs successfully through all payload tests and reaches the expected .rodata protection fault.

  No source files were modified; the working tree remains clean.

  ## Current function

  ### Good changes

  - The invalid e_entry > file_len comparison is disabled with the correct note that entry validation belongs in whole-image admission.
  - The program-header count has a sensible limit of 64.
  - Program and section table bounds now use subtraction before multiplication:

  e_phnum <= (file_len - e_phoff) / e_phentsize

  This is the right overflow-safe structure.

  - Extended section numbering is deliberately rejected rather than accidentally interpreted.
  - The zero-section-table case requires e_shoff == 0 and e_shstrndx == SHN_UNDEF.
  - Architecture, ABI, ELF class, endianness, header sizes, and executable type are all constrained appropriately for the current loader.

  ## Remaining issues in this function

  ### 1. The final identification padding byte is not checked

  At src/user/testelf/loader.c:138:

  for(int i=9; i<15; i++)

  This checks bytes 9 through 14 and skips byte 15. The upper bound should conceptually be EI_NIDENT.

  This is not currently exploitable because the ignored byte is unused, but it contradicts the function’s strict-validation contract.

  ### 2. The function relies on its caller to validate the basic buffer

  The function dereferences hdr immediately. Its caller correctly checks:

  elf_len >= sizeof(Elf64_Ehdr)

  before calling it, so the current path is safe.

  For a function described as independently “strongly validating” a header, its preconditions should either be documented or checked internally:

  - hdr != NULL
  - file_len >= sizeof(*hdr)

  Making the pointer const Elf64_Ehdr * would also express that validation does not modify the input.

  ### 3. Structure sizes should be tied to the C definitions

  The constants are currently:

  #define EH_SIZE    64
  #define PHENT_SIZE 56
  #define SHENT_SIZE 64

  Those are correct ELF64 values, but the parser also relies on the C structures having exactly those sizes. Compile-time assertions would ensure compiler layout cannot drift:

  _Static_assert(sizeof(Elf64_Ehdr) == EH_SIZE, ...);
  _Static_assert(sizeof(Elf64_Phdr) == PHENT_SIZE, ...);

  This is mostly defensive because the current x86-64 compiler layout is correct.

  ### 4. Hostile e_phoff can produce a misaligned C pointer

  Later, the loader does:

  Elf64_Phdr *phdrs = (Elf64_Phdr *)(elf + ehdr->e_phoff);

  The header validator proves that the table is inside the file, but does not prove that e_phoff is suitably aligned for Elf64_Phdr.

  x86-64 hardware permits unaligned loads, but dereferencing a misaligned structure pointer is undefined in C. A hostile input parser should not depend on the compiler being forgiving.

  Either:

  - Require suitable e_phoff alignment, or
  - Copy each program header from the byte buffer into an aligned local Elf64_Phdr before inspecting it.

  Copying is the more general parser design; rejecting misalignment is simpler for your deliberately narrow loader.

  ### 5. Section-header validation is optional policy

  The loader does not use section headers to load the process. Your current bounds checks are safe and reasonable if your policy is “reject structurally malformed ELF metadata.”

  Be aware that section headers must never become a source of load decisions. Runtime memory comes from program headers. A section table can be stripped entirely, incomplete for debugging purposes, or deliberately misleading while the
  loadable image remains defined by PT_LOAD.

  ## Is header validation plus each PT_LOAD enough?

  No. You need whole-image validation because several security properties describe relationships between segments rather than one segment in isolation.

  A clean admission process has four layers.

  ### 1. ELF header

  Your current function covers most of this:

  - Identity and architecture
  - Supported ELF type
  - Table location and size
  - Bounded program-header count
  - Supported encoding and ABI

  ### 2. Every program header

  For each PT_LOAD, validate:

  - p_filesz <= p_memsz
  - p_offset + p_filesz remains inside the logical file, using subtraction-form arithmetic
  - p_vaddr + p_memsz cannot overflow
  - Page-rounded virtual range cannot overflow
  - The complete rounded range is in the permitted userspace window
  - p_offset % PAGE_SIZE == p_vaddr % PAGE_SIZE
  - p_align is acceptable and its congruence rule is satisfied
  - Only known PF_R/PF_W/PF_X bits are set
  - PF_R is present because Sharkix/x86 cannot provide present but unreadable pages
  - Decide whether PF_W | PF_X is rejected
  - The segment does not occupy reserved stack, guard, trampoline, or ABI ranges
  - Segment and rounded sizes are within configured resource limits

  ### 3. Whole-image relationships

  After validating every PT_LOAD, but before mapping any of them:

  - Require at least one nonempty PT_LOAD.
  - Require at least one executable load segment.
  - Reject intersections between any page-rounded load ranges.
  - Require e_entry to lie inside the declared byte range of an executable PT_LOAD:

  p_vaddr <= e_entry < p_vaddr + p_memsz

  - Do not accept an entry that lies only in page padding.
  - Sum all rounded mapped bytes with overflow checks.
  - Sum all anonymous/writable bytes with overflow checks.
  - Enforce a total image-memory budget.
  - Enforce a total segment count and copying-work budget.
  - Check all segments against the future stack and guard range.

  This should be a complete first pass with no VMO creation and no target mappings.

  ### 4. Other program-header types

  Your payload contains NOTE, GNU_PROPERTY, and GNU_STACK headers in addition to PT_LOAD, so ignoring every unknown non-load header is too broad a policy.

  For the current static loader, define an explicit policy:

  - PT_LOAD: validate and load.
  - PT_GNU_STACK: validate stack policy; reject an executable-stack request if stacks are always NX.
  - PT_NOTE and GNU property metadata: safely ignore if unsupported.
  - PT_INTERP: reject because there is no interpreter.
  - PT_DYNAMIC: reject if dynamic relocation is unsupported.
  - PT_TLS: reject until initial TLS construction is implemented.
  - Unknown mandatory semantics: reject.
  - Clearly informational headers: ignore only by explicit policy.

  The exact supported list can remain small.

  ## Loading still needs transactional behavior

  Even perfect admission does not make the mapping phase infallible. Allocation can fail, mappings can fail, or another component could alter the target address space.

  The loading pass should record every created resource and installed mapping. On any failure it must:

  1. Unmap mappings installed by this load.
  2. Remove temporary loader mappings.
  3. Release every newly created VMO and cap.
  4. Return one clean failure without leaving the target partially constructed.

  This also depends on fixing anonymous VMO ownership so releasing the final owner actually returns its physical pages.

  ## Recommended structure

  validate header
      ↓
  parse and validate every program header
      ↓
  validate relationships between all PT_LOAD ranges
      ↓
  validate entry point and reserved ranges
      ↓
  enforce total resource budget
      ↓
  perform mappings transactionally
      ↓
  return entry point

  The key distinction is that validating each PT_LOAD independently cannot detect segment overlap, entry-point validity, resource totals, or collision with the future stack. Those require a whole-image admission stage.

  Worked for 1m 45s · done 1:15 AM




And below is the original audit, preserved for analysis and verification later:

 


## Conclusion

  The current loader is not ready to accept fully hostile ELF files.

  I did not find a route by which p_vaddr directly maps over kernel memory: the VM layer restricts ordinary address spaces to the lower canonical half and rejects existing destination mappings atomically. Capability checks are also
  present on target address-space and VMO operations.

  I did find several hostile-input paths that can hang launch coordination, halt the current startup profile, or permanently consume kernel memory. The loader also accepts malformed layouts whose meaning depends on incidental behavior
  in lower layers.

  No files were modified, and the working tree remains clean.

  ## Confirmed defects

  ### 1. Missing offset/address congruence can fault the loader

  For directly mapped segments, the calculations at src/user/testelf/loader.c:365 assume:

  (p_offset % PAGE_SIZE) == (p_vaddr % PAGE_SIZE)

  That is never checked.

  The mixed file/BSS path calculates the amount to copy using the virtual-address page offset, but calculates the source address using the file-offset page. With incongruent values, it can copy the wrong bytes or read beyond the mapped
  source VMO.

  For example, a one-page ELF can specify approximately:

  p_offset = 0
  p_filesz = 4096
  p_memsz  = 4097
  p_vaddr  = 0x1fff

  The current checks accept this. The loader then calculates:

  page_offset  = 4095
  direct_size  = 4096
  mixed_size   = 4095
  source_offset = 4096

  The mixed-page copy consequently starts one byte beyond the mapped source VMO. The loader thread page faults before sending its failure IPC message. The startup thread is blocked in kipc_recv(), producing a launch deadlock rather than
  a clean rejection.

  This must be rejected during admission, before any mappings or allocations:

  if ((p_offset & (PAGE_SIZE - 1)) !=
      (p_vaddr & (PAGE_SIZE - 1)))
          reject;

  p_align should also be checked for valid power-of-two semantics and matching offset/address congruence.

  ### 2. The ELF controls unbounded allocation through the loader’s factory cap

  Every writable PT_LOAD causes an anonymous allocation based directly on attacker-controlled p_memsz at src/user/testelf/loader.c:311.

  The ELF process does not possess the VMO factory cap, but it effectively directs the loader’s factory cap by choosing:

  - Segment count
  - Segment sizes
  - Writable flags
  - File sizes that drive copying work

  There is no limit on:

  - Number of loadable segments
  - Size of an individual segment
  - Total mapped bytes
  - Total anonymous bytes
  - Total executable bytes
  - Total copying work

  This is a confused-deputy resource problem. A malicious ELF can consume all available contiguous physical memory or impose extreme parsing and mapping work without itself receiving an allocation capability.

  The launcher must provide an explicit resource budget. At minimum, admission needs limits for program-header count, individual segment size, total page-rounded image size, total private memory, and total mapped pages.

  ### 3. Anonymous VMO ownership leaks physical memory

  The loader’s private writable backing exposes a larger existing ownership defect:

  - VM_CREATE_ANON allocates physical pages.
  - The returned cap belongs to the loader.
  - Loader capset destruction calls kcap_destroy(), which destroys only the capability.
  - kvmo_destroy() removes and frees only the VMO descriptor.
  - kpmem_destroy() frees only the PMEM descriptor.
  - The original physical-page ownership reference is never released.

  This affects successful loads and failed loads. A VMO allocated before a later malformed segment is rejected becomes especially easy to orphan.

  Consequently, repeated hostile loads can permanently exhaust physical memory. This needs real VMO ownership and mapping lifetime semantics: mappings retain backing, the VMO owns its backing allocation, and the final VMO/mapping
  release returns the physical pages.

  ### 4. Admission and loading are interleaved

  The loader validates one program header and immediately creates VMOs and mappings before examining later headers.

  A malicious ELF can therefore put large valid-looking segments first and an invalid segment last. Rejection occurs only after substantial allocation and target modification.

  The loader needs two phases:

  1. Admission phase: Validate every header, calculate every rounded range, enforce limits, detect overlaps, and validate the entry point without changing any address space.
  2. Commit phase: Create and install mappings while recording each resource for rollback.

  Any commit failure must unmap everything installed by this load and release every VMO/cap created by it.

  ### 5. A segment can occupy the future stack location and halt the system

  The current profile loads the ELF before mapping its stack. The intended stack begins at 0x801000.

  A hostile but structurally valid ELF can place a segment there. The ELF mapping succeeds. Later, program_map_user_stack() detects the occupied page and fails. The startup profile responds with:

  for (;;) __asm__ volatile ("cli; hlt");

  at src/kernel/profiles/testelf.c:194.

  Thus an ELF can deliberately halt the entire system.

  The stack and guard region should be reserved before ELF loading, or the loader must receive a list of forbidden target ranges. A collision should cause ordinary process-load failure and complete cleanup.

  ### 6. The entry point is not validated as executable content

  The loader returns e_entry unconditionally at src/user/testelf/loader.c:461.

  The launcher checks only that the address:

  - Is nonzero
  - Is in the lower canonical half
  - Has some mapping

  It does not verify executable permission.

  Admission should require exactly:

  There is a successfully admitted PT_LOAD with PF_X where:

  p_vaddr <= e_entry < p_vaddr + p_memsz

  The comparison must use the declared byte range, not the rounded page range. Otherwise an entry in executable page padding would be accepted.

  ### 7. Overlapping load pages are rejected only incidentally and too late

  The loader does not inspect segment pairs for overlap.

  Fortunately, the current VM layer preflights destination pages and rejects a mapping if any page is already present. Therefore, I found no current path where a later segment silently overwrites an earlier segment or upgrades its
  permissions.

  However:

  - Rejection depends on lower-layer behavior rather than an explicit loader rule.
  - The second segment may allocate and copy private memory before its target mapping fails.
  - The accepted ELF format is unclear.
  - A future VM replacement policy could silently change the security result.

  The loader should calculate each page-rounded interval and reject any intersection before allocation:

  rounded_start = floor_page(p_vaddr)
  rounded_end   = ceil_page(p_vaddr + p_memsz)

  Rejecting page overlap is the cleanest current policy.

  ### 8. A malformed ELF can kill the loader without reporting failure

  The congruence bug is one example, but the larger protocol problem is that the parent waits synchronously for a status message. A loader page fault calls thread_exit_current() and sends no IPC message.

  The launch coordinator can therefore wait forever.

  Hostile-input validation should prevent expected parsing faults, but the protocol still needs a loader-death or endpoint-closure outcome. A dying loader must wake the coordinator with failure.

  ## Information exposure at partial pages

  Read-only and executable segments can directly map complete source VMO pages even when the segment occupies only part of the page.

  This exposes bytes:

  - Before an unaligned p_vaddr
  - After p_vaddr + p_filesz
  - Potentially after the logical ELF EOF but within its rounded VMO page

  The current profile explicitly zeroes the entire rounded source allocation before copying the ELF at src/kernel/profiles/testelf.c:42, so bytes after EOF are safe in this profile.

  That safety is currently an undocumented source-VMO construction requirement. A general loader should either:

  - Require source VMO padding to be initialized and non-secret, or
  - Privately materialize partial boundary pages and copy only admitted bytes.

  Without that contract, a source VMO containing adjacent metadata or stale memory could disclose it to the loaded process.

  ## Permission policy assumptions

  These are policy decisions rather than demonstrated capability escapes.

  ### Writable and executable segments

  The loader permits PF_W | PF_X. Arbitrary ELF code already has arbitrary control flow inside its process, so W+X does not directly grant additional capabilities. It does weaken code integrity and makes later exploitation easier.

  I recommend rejecting W+X unless a concrete runtime requires it.

  ### Segments without PF_R

  The AS mapping syscall requires readable mappings because x86 cannot represent ordinary present but unreadable pages. The loader does not reject unreadable segments itself, so some such segments allocate resources and then fail during
  mapping.

  Reject any PT_LOAD without PF_R during admission.

  ### Unknown flags and unsupported ELF features

  For a deliberately small static loader, reject unsupported semantics explicitly:

  - Unknown p_flags bits
  - PT_INTERP
  - Dynamic-linking requirements
  - Unsupported TLS requirements
  - Invalid or unsupported p_align
  - No loadable executable segment

  Silently ignoring unsupported metadata can transform a malformed or incompatible executable into unpredictable runtime behavior.

  ### Source immutability

  The current profile creates the source VMO with intrinsic READ | EXEC, without WRITE, which is good.

  For a general loader, the underlying source must remain immutable for as long as any target directly maps its pages. Giving the loader a read-only cap is insufficient if another cap can mutate the same VMO. This eventually needs an
  immutable/sealed VMO property or private copies.

  ### Target address-space identity

  The loader assumes the target is fresh, separate from the loader, and contains no user mappings. The current profile satisfies that assumption.

  A reusable loader interface should enforce or document it. Loading into the loader’s own address space or an address space concurrently controlled by another thread creates substantially more complicated collision and lifetime
  behavior.

  ## Checks that are already sound

  The current code correctly provides several important protections:

  - Program-header table bounds use division rather than overflow-prone multiplication.
  - p_offset + p_filesz is checked without overflowing.
  - p_filesz <= p_memsz is enforced.
  - Segment size and rounded virtual-range arithmetic are checked for overflow.
  - Writable file bytes are copied into zero-filled private backing.
  - BSS remains zero initialized.
  - Ordinary target mappings are restricted to the lower canonical half.
  - Target range mapping preflights all pages and rolls back an individual failed range.
  - Existing target pages cannot currently be replaced.
  - AS and VMO capability rights are checked by the syscall layer.
  - The source VMO is not granted writable mapping authority in the current profile.

  I did not identify a direct path in these operations to map attacker-controlled pages into the kernel half or bypass the AS/VMO capability checks.

  ## Recommended order

  Before treating the loader as a security boundary:

  1. Add a complete, side-effect-free admission pass.
  2. Enforce page congruence, alignment, permission, address-window, resource-limit, forbidden-range, and page-overlap rules.
  3. Validate e_entry against an executable segment’s declared byte range.
  4. Add transactional mapping with complete rollback.
  5. Fix anonymous VMO ownership and final-page reclamation.
  6. Give the loader a bounded allocation authority rather than an unlimited factory.
  7. Make loader death wake the launch coordinator.
  8. Add adversarial tests for every rejected condition.

  Even after these changes, the broader guarantee that arbitrary ring-3 code can affect only capability-authorized resources also depends on the syscall dispatcher, capability lifetime, object lifetime, and page-table implementation.
  This loader audit supports that guarantee but cannot establish it for the entire kernel by itself.


