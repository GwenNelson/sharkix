Implement PS/2 port1 in ps2-bus (for keyboard)
IRQ multiwait via notification object with bitmasks and binding
    Add the ability to wait on one or several IRQs
    This does mean a generic notification subsystem
Implement PUBSUB stuff
Make late-stage console I/O use the new IPC registry
Implement userspace thread spawning via CPU and AS caps
Implement ELF loader for drivers

Ready for personality layer after this perhaps?

Review findings / prerequisites:

PS/2 port1 needs to become a real 8042 keyboard service, not only an IRQ1
consumer: controller self-test/configuration, port enablement, bounded
command/response handling, keyboard reset/identify/scancode negotiation,
Set-1/Set-2 decoding, modifier/extended-key state, and a defined event
protocol for consumers.  Decide whether port2/mouse support is explicitly
out of scope or shares the same controller lifecycle.

Notification/IRQ design needs explicit semantics and lifecycle: atomic
observe-and-clear bitmask waits, event coalescing, binding/unbinding,
cancellation/destruction, notification capability rights, and safe waiter
reclamation.  Also decide interrupt acknowledgement ownership and ensure
PS/2 data-port reads and EOI cannot lose an interrupt.

PUBSUB needs an authority and backpressure design: topic creation/discovery,
publisher/subscriber rights, copy versus transferable-cap payloads, bounded
subscriber queues, slow-consumer policy, ordering, unsubscribe, and endpoint
death.  The IPC registry must retain a live endpoint reference (or otherwise
prevent a lookup/destroy race) and gain removal/death integration before it
is safe as service discovery.

Add a common kernel-object lifetime/revocation model.  IPC endpoints already
have references, while IRQ objects do not have complete destruction/reaping;
notifications, subscriptions, CPU/AS objects, and drivers should not each
invent incompatible teardown rules.

Userspace spawning needs a complete service/process ABI: CPU, address-space,
and thread capability types and rights; initial registers/stack/bootstrap-cap
transfer; start, exit, join, kill, and cleanup semantics; quotas/accounting;
and service-death behaviour.

Make the ELF loader part of the generic spawning design.  It needs ELF and
program-header validation, PT_LOAD mapping, per-segment W^X permissions,
address/stack collision policy, entry validation, and an explicit policy for
relocations, PIE, TLS, and dynamic linking.  The current flat-image loader
does not provide these properties.

Define console handover/failure policy for registry-backed late I/O: a stable
service name, bounded nonblocking sends, reconnect/death handling, and
preserved Bochs E9/early-serial fallback.  Console output must not deadlock
while reporting a failed console service.

Before the personality layer, add QEMU/runtime tests for injected keyboard
sequences, notification multiwait/coalescing/unbind/teardown, pubsub
backpressure and subscriber death, malformed/adversarial ELF inputs,
capability denial, and driver crash/restart with console fallback.

Suggested dependency order: common lifecycle rules; notifications and IRQ
binding; PS/2; pubsub plus safe registry; CPU/AS/thread spawning plus ELF
loading; then late-console handover.
