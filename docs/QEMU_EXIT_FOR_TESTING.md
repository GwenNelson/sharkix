• ## Recommended QEMU termination facility

  Use QEMU’s isa-debug-exit device. It is purpose-built for guest-driven test termination.

  Run QEMU with something like:

  -device isa-debug-exit,iobase=0xf4,iosize=0x04

  A test-only kernel function performs an outl to port 0xF4. QEMU requests shutdown and returns (guest_value << 1) | 1; for example, guest values 0x10 and 0x11 produce host statuses 33 and 35. Using those values avoids confusing a
  successful guest exit with QEMU’s ordinary status 1 error. QEMU implementation.

  Suggested interface:

  enum kernel_test_result {
        KERNEL_TEST_PASS = 0x10,
        KERNEL_TEST_FAIL = 0x11
  };

  void kernel_test_exit(enum kernel_test_result result)
        __attribute__((noreturn));

  The x86 implementation should:

  1. Execute outl directly, without kportio, locks, allocation, or scheduler involvement.
  2. Call arch_halt() afterward as a fallback when running without the QEMU device.
  3. Exist only in test builds.
  4. Remain a ring-0 API without a general userspace syscall.

  Direct I/O is appropriate because this facility must still work while testing broken subsystems.

  ## Build-time isolation

  Add an explicit setting such as:

  TEST_MODE ?= 0

  When enabled:

  - Define something like SHARKIX_TEST_MODE=1.
  - Compile the test reporting and QEMU-exit source.
  - Add the isa-debug-exit QEMU device.
  - Expose the test-only header/API.

  When disabled, the facility should not be linked into the kernel.

  The test setting must also become part of CONFIG_BUILD_ROOT. Currently mk/common.mk:18 separates objects by configuration, profile, and debug/release state. Merely changing KERNEL_CPPFLAGS would allow Make to reuse objects compiled
  with the opposite test setting.

  A suitable layout would conceptually be:

  build/<config>/<profile>/<release-or-debug>/<normal-or-test>/

  Alternatively, use a dedicated test configuration. The important property is separate object directories.

  I would also add a dedicated run-test or runner-controlled launch path rather than changing normal make run (mk/rules.mk:69).

  ## Machine-readable output

  Port E9 is already ideal for the control protocol because console_putc() (src/kernel/core/console.c:58) writes there regardless of the late console driver state.

  Use a versioned, line-oriented protocol with a distinctive prefix:

  SHARKIX-TEST/1 BEGIN testipc
  SHARKIX-TEST/1 CASE PASS ipc.create
  SHARKIX-TEST/1 CASE FAIL ipc.destroy status=-2
  SHARKIX-TEST/1 END FAIL cases=12 failures=1

  Restrict profile and case names to simple identifier characters. This avoids needing JSON parsing inside the kernel.

  The runner should ignore unprefixed diagnostic output. A profile passes only if:

  - It emits one valid BEGIN.
  - It emits one matching END PASS.
  - QEMU exits with the expected debug-exit pass status.
  - It finishes before its timeout.

  An END PASS without the matching exit status should fail, as should a pass exit without the terminal record.

  For robust reporting, a test-only E9 writer is preferable to building protocol lines through several console_write() calls. Multiple calls can interleave, and late console forwarding introduces dependencies on IPC and console drivers.
  Human diagnostics can continue using the ordinary console.

  ## Test framework shape

  Model the presentation after libfifo:

  Testing: IPC create/destroy                           PASS
  Testing: Notification destruction                    FAIL
  ...
  Tests: 42  Passed: 41  Failed: 1

  Inside the kernel, provide a small test context that tracks:

  - Current profile name.
  - Tests run.
  - Tests passed.
  - Tests failed.
  - First or latest failure details.

  Profiles can then use something equivalent to:

  KTEST_CHECK("ipc.create", kipc_create(&endpoint) == IPC_OK);
  KTEST_CHECK("ipc.destroy", kipc_destroy(endpoint) == IPC_OK);
  kernel_test_finish();

  Ordinary assertion failures should record failure and continue when safe. Corruption, fatal initialization failure, or inability to continue should emit a terminal failure and exit immediately.

  Existing profiles such as src/kernel/profiles/testipc.c:13 currently print failures and return, leaving QEMU running. They can be migrated incrementally rather than parsed through their current ad hoc output.

  ## Host runner

  A small Python runner using only the standard library is the cleanest option. It should:

  1. Read a manifest of test profiles.
  2. Build each profile with TEST_MODE=1.
  3. Launch QEMU with separate output files.
  4. Enforce a per-profile timeout.
  5. Decode the QEMU debug-exit status.
  6. Parse E9 protocol records.
  7. Preserve complete logs for failures.
  8. Continue with remaining profiles.
  9. Print a libfifo-style summary.
  10. Return nonzero if any build or runtime test failed.

  Each manifest entry should contain approximately:

  profile
  timeout
  required output channel
  expected test profile identifier
  optional QEMU arguments
  driver selection

  Classify failures distinctly:

  - Build failure
  - QEMU launch failure
  - Timeout or hang
  - Guest-reported failure
  - Unexpected QEMU termination
  - Malformed or incomplete protocol
  - Missing expected serial output

  Keep the runner sequential initially. The top-level kernel.elf symlink and output streams make parallel execution unnecessary complexity at this stage.

  ## Driver selection

  The current PC platform unconditionally adds VGA, serial, PS/2 bus, and PS/2 keyboard drivers in mk/platform/pc-generic.mk:8. Every profile therefore boots with more driver activity than a focused subsystem test needs.

  For reliable tests, profiles should explicitly select their drivers:

  - Most subsystem profiles: no late userspace drivers.
  - Serial tests: serial driver only, plus anything it strictly requires.
  - VGA tests: VGA driver only.
  - PS/2 tests: the relevant PS/2 stack.

  The selected driver set must also affect the build output identity if the same profile can be built with different driver combinations.

  ## Serial-driver tests

  Capture E9 and COM1 separately:

  -debugcon file:<artifact-directory>/e9.log
  -global isa-debugcon.iobase=0xe9
  -serial file:<artifact-directory>/com1.log

  Use E9 for the harness control protocol. Treat serial as the subject being tested.

  A serial test should:

  1. Start the serial service.
  2. Wait for its existing readiness signal.
  3. Send a known nonce or record through the console path.
  4. Have the serial service acknowledge after it has actually written the complete record to COM1.
  5. Emit END PASS over E9.
  6. Invoke the QEMU test exit.

  That acknowledgment matters. Exiting immediately after merely enqueueing a console message could terminate QEMU before userspace transmits it.

  The host then requires both:

  - A valid E9 pass result.
  - The exact expected record in com1.log.

  ## Suggested implementation order

  1. Add the isolated test build mode.
  2. Add the x86 QEMU debug-exit implementation and test launch arguments.
  3. Add the E9 protocol/reporting helper.
  4. Add a one-profile smoke test proving PASS, FAIL, and timeout handling.
  5. Add the host runner and summary.
  6. Convert existing profiles one at a time.
  7. Add explicit profile driver selection.
  8. Add the serial dual-channel test.

  This fits the existing profile architecture well. The main traps are stale object reuse after changing compilation flags, treating arbitrary diagnostic text as test results, and allowing QEMU termination before serial output has
  actually completed.

  No files were modified.

