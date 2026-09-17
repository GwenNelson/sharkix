# Sharkix Scheduler

Sharkix owns its scheduler. The portable implementation is intentionally small:

- `src/kernel/scheduler.c` owns run-queue policy, the idle thread, tick accounting, sleeping, blocking, wakeup, and dispatch state.
- `src/kernel/thread.c` owns thread allocation, lifetime, address spaces, kernel stacks, and the public thread API.
- `src/kernel/arch/<architecture>/` owns saved-register layouts, interrupt entry and return, the timer source, and the actual context switch.
- `include/sharkix/kernel/arch.h` is the boundary a new architecture must implement.

No generic scheduler or thread code depends on x86 register layouts, selectors, interrupt instructions, the PIT, or CR3.

## Current policy

The current policy is strict round robin over one FIFO ready queue. A runnable thread that yields or is preempted goes to the tail. A newly started, awakened, or expired sleeping thread also goes to the tail. The idle thread is selected only when that queue is empty.

All policy selection is deliberately concentrated in:

```c
static thread_t *scheduler_choose_next_locked(void)
```

That is the starting point for replacing round robin later. Queue mechanics and state transitions should remain separate from policy whenever possible.

`thread_priority_t` remains in the creation API so a later policy can use it without another API migration. The round-robin policy intentionally ignores it.

## Thread states and queue ownership

- `NEW`: allocated but not fully initialized.
- `READY`: fully initialized but not eligible to run. `thread_start()` is the only normal transition out.
- `RUNNABLE`: eligible for selection and either in the ready queue or the idle fallback.
- `RUNNING`: the single thread currently executing on this single-CPU scheduler.
- `BLOCKED`: waiting for an explicit wake or a scheduler tick deadline.
- `TERMINATING`: exited and awaiting the first reaper pass.
- `DEAD`: retained for one reaper interval so callers can observe thread death.
- `INVALID`: no registered thread has that ID.

A thread must never be in both the ready and sleep queues. Queue membership flags make violations fail early instead of silently corrupting an intrusive list.

## Context-switch contract

The portable scheduler treats `thread_t.saved_context` as an opaque integer token. On x86_64 it is a saved stack pointer, but generic code must not depend on that interpretation.

The architecture provides:

- `arch_thread_context_init()` to create the first opaque context.
- `arch_scheduler_start()` to restore the first selected context.
- `arch_scheduler_yield()` to enter the dispatcher voluntarily.
- `arch_wait_for_interrupt()` for the idle loop.
- interrupt-mask and critical-section helpers used by portable kernel code.

Yield and timer interrupt entry save a context and call `scheduler_on_yield()` or `scheduler_on_tick()`. The scheduler returns the opaque context to restore. Architecture assembly must not inspect `thread_t` layout or choose a thread.

Before returning a selected context, the scheduler calls `thread_prepare_current()`. That updates CPU-local identity, the architecture's kernel-entry stack, and the active address space.

## Time

Portable code uses `scheduler_tick_t` and `SCHEDULER_TICKS_PER_SECOND`. The x86_64 port currently programs the PIT at 10 Hz. A future platform may use any timer source as long as each timer interrupt calls `scheduler_on_tick()` once.

Sleeping uses tick deadlines and signed subtraction, so ordinary deadlines continue to work across counter wrap. Delays of half the counter range or more are not supported.

## Synchronization scope

This migration preserves the existing synchronization behavior: contended mutexes and semaphores yield and retry. They are not scheduler wait queues yet. This keeps the scheduler independent of `libfifo` and leaves blocking-primitive design as a separate change.

The scheduler is single CPU. Interrupt masking protects scheduler structures locally; it is not an SMP locking scheme.

## Porting to another architecture

1. Add the architecture implementation selected by `KERNEL_ARCH_MODULE_ROOTS`.
2. Implement every function declared in `include/sharkix/kernel/arch.h`.
3. Build initial kernel and user contexts in `arch_thread_context_init()` without exposing the frame layout to generic code.
4. Route voluntary yield and the platform timer to `scheduler_on_yield()` and `scheduler_on_tick()`.
5. Restore the opaque context those functions return.
6. Implement address-space activation and kernel-entry-stack setup used by `thread_prepare_current()`.
7. Verify cooperative round robin, timer preemption, delay wakeup, syscall block/wake, user entry, exceptions, exit, and deferred reaping.

The x86_64 port is an example implementation, not part of the portable scheduler contract.

## Runtime checks

The most useful existing profiles are:

- `PROFILE=preemption`: non-cooperative timer preemption and bounded kernel-stack use.
- `PROFILE=lifecycle`: exit, observable death, reaping, and page recovery.
- `PROFILE=syscall_block`: persistent syscall frame plus cross-thread wakeup.
- `PROFILE=testipc`: ordinary IPC tests and the 100-worker, 100,000-hop ring.
- `PROFILE=exceptions`: user exception delivery and teardown.
- `PROFILE=testbin_capnames`: user entry, syscalls, capabilities, and userspace return.

Build a profile with `make CONFIG=pc-x86_64-debug PROFILE=<name>` and boot it with the corresponding run target or QEMU command.
