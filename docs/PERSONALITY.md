# Sharkix Personality Syscall Handling

## Status

**Design proposal / initial specification.**

This document describes the initial mechanism by which Sharkix will support
foreign syscall ABIs ("personalities").

The initial implementation deliberately prioritises simple semantics and a
clean C interface over minimising register copies. A future implementation may
provide a faster register-preserving path without changing the observable
personality semantics described here.

## Overview

Native Sharkix syscalls use **negative syscall numbers**.

Non-negative syscall numbers (`RAX >= 0`) are reserved for personality use.
This includes syscall number zero, since foreign ABIs are free to assign a
meaning to it.

A thread may have a personality handler registered for it. When such a thread
executes a non-negative syscall, Sharkix does not interpret the syscall number
itself. Instead, the kernel suspends the calling thread and transfers the
syscall to its registered personality handler.

The personality handler is an ordinary userspace function and may be written
in normal C.

Conceptually:

```text
managed task
    |
    | SYSCALL, RAX >= 0
    v
Sharkix
    |
    | save userspace register state
    | create one-shot return capability
    | construct personality context
    v
personality_dispatch(context)
    |
    | emulate foreign syscall
    | modify returned register state
    v
SYS_PERSONALITY_RETURN(return_cap, regs)
    |
    v
managed task resumes
```

Sharkix does not assign meaning to the foreign syscall number or its arguments.
Dispatching individual syscalls is entirely the responsibility of the
personality.

## Registration

A personality registers a handler for a managed thread.

Registration provides, at minimum:

- the thread to be managed;
- the personality handler entry point;
- a stack in the personality address space on which the handler may execute;
- a personality-defined cookie associated with the managed thread.

The cookie is opaque to Sharkix.

A personality may therefore use it as an index, pointer, task identifier, or
any other value useful to its implementation.

Conceptually:

```c
personality_register(
    thread,
    handler_entry,
    handler_stack,
    cookie
);
```

The precise syscall interface is TBD.

## Personality Entry

When a managed thread executes a syscall with `RAX >= 0`, Sharkix:

1. Suspends the calling thread.
2. Captures its userspace architectural register state.
3. Creates a kernel object representing the suspended personality invocation.
4. Creates a **single-use capability** referring to that object.
5. Constructs a personality context on the registered personality stack.
6. Enters the registered handler using the normal userspace C ABI.

A possible initial context representation is:

```c
struct personality_regs {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;

    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t rip;
    uint64_t rflags;
};

struct personality_context {
    cap_handle_t return_cap;
    uintptr_t cookie;
    struct personality_regs regs;
};
```

The exact structure layout is ABI and architecture dependent and remains TBD.

On entry, `regs.rax` contains the original foreign syscall number.

The handler itself may therefore be an ordinary C function:

```c
void personality_dispatch(struct personality_context *ctx);
```

For example:

```c
void
unix_dispatch(struct personality_context *ctx)
{
    switch (ctx->regs.rax) {
    case FOREIGN_SYS_READ:
        emulate_read(ctx);
        break;

    case FOREIGN_SYS_WRITE:
        emulate_write(ctx);
        break;

    default:
        ctx->regs.rax = FOREIGN_ENOSYS;
        break;
    }

    personality_return(ctx->return_cap, &ctx->regs);
}
```

No assembly stub or special compiler calling convention is required for the
basic personality ABI.

## Personality Return Capability

The return capability represents the authority to complete exactly one
suspended personality invocation.

The capability refers to a kernel object which records the suspended Sharkix
thread and any kernel state necessary to complete the invocation.

The capability is **single-use**.

This deliberately separates two concepts:

```text
return capability    -> which invocation may be resumed, and authority to do so
register structure   -> how that invocation should resume
```

The return capability may use normal Sharkix capability transfer semantics.

In particular, a personality dispatcher may transfer the return capability to
another task if it wishes to delegate completion of the syscall. As with all
Sharkix capability transfers, transfer moves the capability; the sender does
not retain it.

This permits asynchronous personality implementations without requiring a
special asynchronous syscall mechanism.

## Returning From a Personality Syscall

A native Sharkix syscall completes the personality invocation.

Conceptually:

```c
personality_return(
    cap_handle_t return_cap,
    const struct personality_regs *regs
);
```

The syscall requires only:

1. the single-use return capability; and
2. a pointer to the desired userspace register state.

The register structure need not be the original structure constructed by
Sharkix. The return capability identifies the invocation independently of the
memory containing its desired return state.

On successful completion Sharkix:

1. validates the return capability;
2. copies and validates the supplied architectural state;
3. commits to completing the invocation;
4. consumes the single-use return capability;
5. destroys/releases the personality invocation object as appropriate;
6. restores the supplied userspace state to the suspended thread;
7. makes that thread runnable again.

The return capability MUST NOT be consumed merely because the caller supplied
an invalid userspace pointer or invalid architectural state.

It is consumed once the kernel has successfully validated the request and
committed to completing the invocation.

After this point the same personality invocation cannot be completed again.

## Validation of Returned State

Personality handlers are userspace code. Returned architectural state MUST
therefore be treated as untrusted userspace input.

At minimum, Sharkix must validate state which could affect privilege or violate
architectural constraints.

In particular, **RFLAGS must be validated and/or sanitised** so that a
personality cannot cause a managed task to resume with privileged or otherwise
invalid flag state.

Return RIP and RSP must also represent valid userspace addresses and satisfy
the architectural requirements for return to userspace.

Where practical, Sharkix should reject invalid state explicitly rather than
relying solely on the processor to fault during return. This permits useful
diagnostics and avoids turning personality implementation mistakes into obscure
return-path faults.

The CPU remains the final enforcement boundary, but hardware enforcement is
not a substitute for sensible kernel validation.

## Personality Stack

The personality supplies a stack within its own address space when registering
the handler.

Sharkix constructs the initial personality context on this stack and enters
the handler with a stack conforming to the normal userspace ABI, including
required alignment and x86-64 SysV red-zone semantics.

The managed task's original `RSP` is preserved in:

```c
ctx->regs.rsp
```

It is therefore available to personalities which need to inspect or emulate
foreign stack-based syscall conventions.

The managed task's stack does not need to be mapped into the personality
address space merely to service the syscall.

## Blocking and Delegation

The managed thread remains suspended until its return capability is consumed
by a successful personality return.

The personality handler may make ordinary native Sharkix syscalls while
servicing the invocation.

It may therefore use normal Sharkix IPC, VMOs, notifications and other
services to implement the foreign operation.

For example:

```text
foreign read()
      |
      v
Unix personality
      |
      | Sharkix IPC
      v
filesystem service
      |
      v
personality_return()
      |
      v
foreign read() returns
```

Alternatively, the personality may transfer the single-use return capability
to another task and allow that task to complete the invocation later.

The kernel therefore does not require the personality invocation to remain
associated with the userspace thread which initially received it.

## Capability Model

A task managed by a personality does not necessarily require any native
Sharkix capabilities of its own.

The personality may maintain an entirely separate model of authority.

For example, an seL4 personality could maintain an emulated CSpace without
placing corresponding Sharkix capabilities into the managed task's native
capset. Similarly, a Unix personality could maintain file descriptors,
credentials and other process state entirely within personality services.

Only the personality and its helper services need possess the native Sharkix
capabilities required to implement those abstractions.

This keeps foreign ABI policy outside the kernel and avoids requiring Sharkix
to reproduce the internal object model of every supported personality.

## Future Optimisation

The initial design intentionally copies architectural register state into a
userspace-visible structure.

This is expected to be somewhat slower than directly forwarding register state,
but has several advantages:

- personality handlers can be written in ordinary C;
- the ABI is straightforward to document and debug;
- complete machine state is explicit;
- there is no special register-preservation convention;
- handlers can be single-stepped normally;
- correctness can be established before syscall forwarding is optimised.

If profiling later demonstrates that register copying is significant, Sharkix
may add an optional register-preserving fast personality entry mechanism.

Such an optimisation should preserve the semantics defined here.

In particular, the fundamental model remains:

> A non-negative syscall made by a managed task creates a suspended invocation
> which is delegated to its personality. A single-use capability represents
> the authority to complete that invocation, and completion supplies the
> userspace architectural state with which the managed task will resume.

## Design Principle

Personality handling is not ordinary IPC.

It is delegation of a trapped userspace syscall to another userspace component.

Sharkix provides only the mechanism required to suspend the caller, expose its
architectural state, delegate authority to complete the invocation, and safely
resume it.

The meaning of the syscall remains entirely userspace policy.
