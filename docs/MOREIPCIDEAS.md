# Sharkix IPC / Capability / Personality Notes

## General philosophy

Keep the kernel mechanism-oriented and avoid teaching it higher-level concepts unnecessarily.

In particular, the kernel does not need to understand RPC, requests/replies, filesystems, POSIX semantics, driver protocols, etc. These should emerge from simple IPC and capability primitives.

There is no privileged "root task". The kernel/boot configuration may start as many initial tasks as necessary. Their privilege is determined entirely by the capabilities they receive.

Address space and security domain are currently treated as the same thing. Threads sharing an address space therefore share its security boundary/capability environment and probably its personality syscall table.


## Syscall namespaces / personalities

Use signed 64-bit syscall numbers:

- Negative syscall numbers are native Sharkix kernel syscalls.
- Nonnegative syscall numbers are reserved for personality syscalls.

This gives each personality the entire nonnegative `int64_t` namespace without colliding with kernel operations.

A personality may expose native facilities, completely mediate them, or have processes use IPC directly instead of personality syscalls.

Eventually support syscall tables which map personality syscall numbers to handlers/IPC endpoints.

Different security domains may have different syscall tables.

Keep separate authority for:

- modifying a syscall table;
- attaching a syscall table to a security domain;
- assigning meanings to ranges of syscall numbers.

Syscall-number authority can use range capabilities rather than requiring one cap per syscall. The same general range-cap mechanism may also be useful for physical memory, I/O ports, etc.


## Task creation / resource authority

Do not adopt seL4's Untyped model.

Task/process creation should instead be explicit capability authority. A personality/service may, for example, possess authority to create security domains/threads and separately have access to memory/resource allocation.

Keep "permission to create something" separate from "resources available to create it".

No special root process is required.


## Boot / drivers

The bootloader supplies the kernel with the initial driver/program images and configuration.

The kernel contains enough ELF-loading/bootstrap support to create the required initial tasks.

Drivers may be split into:

1. a very small ring-0 bootstrap/probe component;
2. a long-running ring-3 driver/service.

For buses such as PCI, the ring-0 component may perform the privileged initial probing/resource acquisition and then permanently hand management authority to the ring-3 bus driver before exiting.

Ordinary PCI/USB device drivers should ideally require no ring-0 component. They declare the devices they support and receive capabilities for the resources belonging to the matched device.

Drivers can be substantial programs. "Microkernel driver" does not imply a deliberately stupid/minimal driver; the important boundary is what actually requires kernel privilege.


## Registry

The native registry is hierarchical and string-addressed, e.g.:

    sys.hw.bus.pci....
    user.ipc.endpoints
    user.ipc.shm

`sys.*` is primarily system/internal/hardware infrastructure.

`user.*` contains facilities intended to be directly usable by userspace or projected through a personality abstraction.

Names provide discovery; capabilities provide authority.

The registry is not `/dev` and should not acquire filesystem semantics merely because a POSIX personality may choose to project some registry objects into `/dev`.


## IPC

Keep kernel IPC fundamentally message-oriented.

Current basic payload is five machine words.

Provide two explicit message shapes:

    WORDS:      5 arbitrary words
    CAP:        4 arbitrary words + 1 capability

Likely primitive operations:

    IPC_SEND
    IPC_SEND_NB
    IPC_SEND_CAP
    IPC_SEND_CAP_NB

    IPC_RECV
    IPC_RECV_CAP

`IPC_SEND` may block when the destination FIFO is full.

`IPC_SEND_NB` never waits for FIFO capacity; if the message cannot be enqueued immediately, it returns an error such as `WOULD_BLOCK`/`FIFO_FULL`.

`IPC_RECV` blocks waiting for an appropriate message.

Do not have a special kernel `IPC_CALL` concept.

RPC is a userspace convention:

    SEND_CAP(server, reply_endpoint_cap, request...)
    RECV(reply_endpoint, response...)

A synchronous call is simply an asynchronous request followed immediately by waiting for its response.

A caller may instead continue working and receive the response later.

The kernel therefore knows about:

- endpoints;
- FIFOs;
- messages;
- words;
- optional capability passing;
- blocking/nonblocking queue operations.

It does not need to know about:

- calls;
- replies;
- RPC;
- clients/servers;
- transactions;
- callbacks.

A "reply cap" does not need to be a special kernel object. It can simply be an ordinary endpoint capability which userspace chooses to use for replies.

The caller also chooses the trust/lifetime policy. It can derive a fresh restricted reply capability for every request, or repeatedly give a trusted service the same persistent endpoint capability.

The same mechanism naturally supports replies, callbacks, subscriptions, asynchronous completion and resource handoff.


## Global capability handles

Capability handles are globally unique numbers.

Tasks/security domains still have capability possession tables, but these are sets/tables of capabilities they possess rather than local handle namespaces.

The same capability therefore has the same numeric handle everywhere.

Knowing a handle does not confer authority: the capability must be present in the caller's possession table.

This avoids handle translation when capabilities move between domains.


## Capability propagation rights

Distinguish ordinary object rights from authority governing how a capability itself may propagate.


### TRANSFER

`TRANSFER` means:

> You may surrender this capability to another security domain.

The sender gives it up completely.

This is useful when responsibility/ownership genuinely moves elsewhere.


### FORWARD

`FORWARD` means:

> While this capability remains untouched, you may pass the whole capability onward and retain nothing yourself.

Forwarding preserves `FORWARD`, allowing chains such as:

    A -> B -> C -> D

provided every intermediate holder acts only as a courier.

However, **any local exercise of the capability consumes `FORWARD`**.

This includes deriving from it.

Conceptually:

    receive X + FORWARD

          /                 \
      forward              use/derive
         |                     |
    surrender X            lose FORWARD
         |                     |
    next holder            retain authority
    may forward            locally

Once a holder has used the capability, it cannot send that authority elsewhere using `FORWARD`.

If it possesses neither `TRANSFER` nor `DELEGATE`, the capability is then permanently confined to that security domain until dropped/destroyed.

This provides a useful "sealed envelope" property:

> You may use this authority yourself, or pass it onward untouched, but not both.

A failed forwarding operation must not consume `FORWARD`.

For example, if `IPC_SEND_CAP_NB` fails because the FIFO is full, the sender retains the capability unchanged. Removal from the sender and insertion into the message/receiver must occur atomically with successful enqueueing.

Introspection which does not exercise the underlying authority should not consume `FORWARD`.


### DELEGATE

`DELEGATE` permits creating derived authority which may itself retain propagation rights such as `FORWARD` and `TRANSFER`.

Ordinary derivation is always possible, but deriving without the necessary `DELEGATE` authority consumes `FORWARD` and must not allow propagation rights to be preserved/escalated.

In particular this must be impossible:

    X = READ | WRITE | FORWARD

    derive Y = READ
    keep Y
    forward X

Deriving `Y` commits the holder to exercising authority from `X`, so `X` immediately loses `FORWARD`.

Broadly:

    TRANSFER = "I may surrender this."
    FORWARD  = "I may pass this onward untouched."
    DELEGATE = "I may manufacture derived authority which can itself propagate."


## Zero-copy / forwarding example

This is particularly useful for layered I/O.

A client can give a filesystem a capability for an output buffer with `FORWARD`.

For a simple read, the filesystem may translate the file request into block extents without ever exercising the buffer capability:

    client
       |
       | buffer + FORWARD
       v
    filesystem
       |
       | forward untouched
       v
    block driver
       |
       | read/DMA
       v
    client's memory

The filesystem remains in the control path but disappears from the data path.

If instead the filesystem needs to map/read/write/transform the buffer itself, doing so consumes `FORWARD`. It cannot subsequently pass that authority to the block driver unless it independently has `TRANSFER` or `DELEGATE` authority.

This lets the client enforce:

> Either process my buffer yourself, or pass the untouched buffer authority down the stack. You cannot inspect/use it and then forward it as though you were merely a conduit.

This should make zero-copy pipelines through filesystems, storage stacks and other layered services possible without making zero-copy a special kernel concept.

A capset capability can eventually be used where a protocol genuinely needs several capabilities in one message, while retaining the simple common IPC form of four words plus one capability.


## Capability propagation rule still to specify precisely

The exact derivation semantics around `DELEGATE` still need to be nailed down.

In particular:

- Which propagation rights may a derived capability retain?
- Must the parent possess every propagation right placed on the child?
- Precisely which operations count as "using" a capability and therefore consume `FORWARD`?
- Which introspection operations are explicitly non-consuming?

The intended core distinction is nevertheless:

    TRANSFER
        I can surrender this authority.

    FORWARD
        I can pass this authority onward untouched,
        but if I use it myself I lose that privilege.

    DELEGATE
        I can derive authority in a form which is
        itself permitted to propagate.
