# Driver initialization metadata requirements

This document records the ring0 work performed by the current drivers under
`src/drivers/` and the operations a future init system would need to describe
to replace that driver-specific kernel code with metadata. It describes the
current implementation as inspected; it does not prescribe a new driver
framework or change any driver behaviour.

The boundary is that ring0 establishes resources and authority, then launches
the ring3 program. Device protocol and service work belong in ring3. A future
init system should represent resource creation, lookup, wiring, capability
rights, bootstrap data, ordering, readiness, and failure policy as explicit
inputs to initialization, rather than embedding each recipe as a C function.

## Current build and startup paths

`src/drivers/module.mk` defines `ring3-driver`. For every selected driver it
collects `kernel/*.c` into the kernel source list, compiles `user/*.c`, links a
ring3 ELF with the common entry point and linker script, converts the ELF to a
binary, and embeds that binary in the kernel image. The per-driver
`module.mk` invokes this macro. `mk/platform/pc-generic.mk` currently selects
`vga-consoled`, `serial-consoled`, `ps2-bus`, and `ps2-keyboard`.

The current task construction recipe in the driver kernel files is repeated:

1. Create an address space.
2. Map the embedded flat ring3 image at `PROGRAM_DEFAULT_LOAD_ADDRESS`.
3. Map a one-page user stack at `PROGRAM_DEFAULT_STACK_BASE`.
4. Translate the stack address where the bootstrap words will be written.
5. Create a user-privilege thread at the image entry point, with its initial
   stack pointer below the bootstrap words.
6. Set names on the capabilities and add them to the new address space's
   capset.
7. Write the bootstrap count and capability handles to the user stack.
8. Start the thread only after resources, capset membership, names, registry
   entries, and bootstrap data are ready.

VGA and serial also start one kernel worker each. It blocks on that driver's
readiness endpoint, records the ready boolean after receiving a message, then
remains alive yielding forever. The PS/2 readiness helpers instead poll a
notification and cache the observed ready bit; the current PS/2 test profile
busy-waits on the bus helper.

The bootstrap layout is `bootstrap[0] = capability_count`, followed by the
capability handles in the driver's current declared order. The ring3 programs
use `sharkix_get_bootstrap()` to resolve those handles by capability name;
the supplied names must therefore be unique within that bootstrap and each
capability must have `CAP_RIGHT_GETNAME`.

| Driver | Ring3 task name | Bootstrap caps | Bootstrap words |
| --- | --- | ---: | ---: |
| `vga-consoled` | `vga-consoled` | 4 | 5 |
| `serial-consoled` | `serial-consoled` | 8 | 9 |
| `ps2-bus` | `ps2bus` | 10 | 11 |
| `ps2-keyboard` | `ps2-keyboardd` | 6 | 7 |

Each task receives a one-page user stack; the final column counts the
bootstrap count word plus the capability handles stored there.

Startup is not uniform today:

- VGA and serial use `REGISTER_CONSOLE_DRIVER`. `console_init_late()` creates
  and registers `console.output`, then calls the selected console drivers'
  init functions. Each returns after starting its ring3 task and its kernel
  readiness worker.
- PS/2 bus has the explicit `ps2bus_init()` entry point. The
  `ps2bus_test` startup profile calls it and waits for `ps2bus_isready()`.
- PS/2 keyboard has the explicit `ps2_keyboard_init()` entry point. No current
  startup profile calls it. It requires the PS/2 bus endpoints to have been
  registered before it is called.

Kernel initialization sets up IPC and its registry, PMEM, port I/O,
notifications, IRQs, capabilities, virtual memory, and the kernel address
space before starting the kernel start task. That task runs
`console_init_late()` before the selected startup profile. An init system must
respect service dependencies: console consumers need `console.output`; the
keyboard driver needs the PS/2 bus's four endpoints and should be started only
after the bus is operational.

## Ring0 operations shared by these drivers

The existing implementations use some or all of the following operations.
These are the concrete operation categories the metadata must be able to
express:

- **Resolve an existing named IPC endpoint.** The registry returns a kernel
  endpoint handle; ring0 creates a capability for that object with only the
  rights the child needs.
- **Create an IPC endpoint.** Current uses include normal endpoints for
  readiness or byte streams. The distinction between a normal endpoint and a
  publisher matters: console drivers subscribe to the existing publisher,
  while PS/2 byte channels and readiness channels are normal endpoints.
- **Subscribe to an IPC publisher.** VGA and serial obtain their input stream
  by subscribing to `console.output`; they do not create another console
  publisher.
- **Create hardware/resource objects.** Current examples are a PMEM region,
  port-I/O ranges, IRQ objects, and notification objects. Each requires
  type-specific parameters and capability rights.
- **Wire event sources to a notification.** The PS/2 bus binds IRQs and
  readable TX endpoints to bits of its private notification. Bindings are
  separate from the driver's lifecycle-ready notification.
- **Name and install capabilities.** The task's capset receives the child
  capabilities, not raw kernel object handles. Capability names are also the
  ring3 bootstrap lookup keys.
- **Register named IPC endpoints.** PS/2 bus publishes its port endpoints;
  PS/2 keyboard publishes its output endpoint. VGA and serial consume the
  console registry entry and publish no endpoint of their own.
- **Construct and start the ring3 task.** Metadata must identify the embedded
  image, task name, priority, stack size, capability set, and bootstrap
  contract, while ensuring the thread starts after setup completes.
- **Observe lifecycle readiness.** Current drivers use either a readiness
  IPC message or a notification bit. Readiness is distinct from runtime
  events and from endpoint readability.
- **Report and handle setup failure.** Current error handling is inconsistent
  and often halts the kernel. A future initializer needs explicit failure
  status and cleanup/rollback behaviour for partially created resources.

The child capset is the authority boundary. Descriptions should record
capability type and exact rights, including `GETNAME` where bootstrap name
resolution needs it. Do not treat a registry name or a raw kernel handle as a
substitute for a child capability.

## Driver-specific initialization recipes

### `vga-consoled`

`console_vga_init()` is registered as the `vga` console driver. It performs:

1. Create a PMEM object for physical range `0xB8000`–`0xBFFFF` (base
   `0xB8000`, length `0x8000`). Give ring3 `MAP | READ | WRITE | GETNAME`.
2. Look up `console.output`, subscribe to it, and give ring3 the subscriber
   endpoint with `IPC_RECV | GETNAME`, named `vga.output`.
3. Create a normal readiness endpoint. Give ring3 `IPC_SEND | GETNAME`, named
   `vga.ready`.
4. Create a read/write port-I/O object at `0x3D4`, length 2, and give ring3
   `PORTIO_READ | PORTIO_WRITE | GETNAME`, named `vga.crtc`.
5. Launch `vga-consoled` with four bootstrap capabilities, in this order:
   `vga.output`, `vga.ready`, `vga.vram`, `vga.crtc`.
6. Start a kernel worker that blocks receiving the ring3 readiness message;
   after the message, set `vga_ready`. `console_vga_isready()` reports this
   state. The worker then remains alive yielding.

Ring3 creates a VMO from the PMEM capability, maps it, initializes its VGA
output state, signals readiness, then receives console output and writes the
screen. VRAM mapping and console rendering are ring3 work.

### `serial-consoled`

`console_serial_init()` is registered as the `serial` console driver. It
performs:

1. Look up `console.output`, subscribe to it, and give ring3 the subscriber
   endpoint with `IPC_RECV | GETNAME`, named `serial.out`.
2. Create a normal readiness endpoint. Give ring3 `IPC_SEND | GETNAME`, named
   `serial.ready`.
3. Create six separate one-byte port-I/O objects for COM1:

   | Capability name | Port | Ring3 rights |
   | --- | ---: | --- |
   | `serial.ier` | `0x3F9` | `PORTIO_WRITE | GETNAME` |
   | `serial.lcr` | `0x3FB` | `PORTIO_WRITE | GETNAME` |
   | `serial.mcr` | `0x3FC` | `PORTIO_WRITE | GETNAME` |
   | `serial.iir` | `0x3FA` | `PORTIO_WRITE | GETNAME` |
   | `serial.lsr` | `0x3FD` | `PORTIO_READ | GETNAME` |
   | `serial.com1` | `0x3F8` | `PORTIO_WRITE | GETNAME` |

4. Launch `serial-consoled` with eight bootstrap capabilities: `serial.out`,
   `serial.ready`, then the six port-I/O capabilities in table order.
5. Start a kernel worker that blocks receiving the readiness message; after
   it arrives, set `serial_ready`. `console_serial_isready()` reports this
   state. The worker then remains alive yielding.

Ring3 configures the UART, signals readiness, receives console output, and
writes characters. Register setup and character transmission are ring3 work.

### `ps2-bus`

`ps2bus_init()` is an explicit startup-profile entry point, not a console
driver registration. It performs:

1. Create IRQ objects for hardware IRQ 1 and IRQ 12. Give ring3 each IRQ with
   `IRQ_WAIT | IRQ_ACK | GETNAME`, named `ps2bus.irq.1` and `ps2bus.irq.12`.
2. Create one-byte port-I/O objects at `0x60` and `0x64`. Give ring3 both
   `PORTIO_READ | PORTIO_WRITE | GETNAME`, named `ps2bus.pio.data` and
   `ps2bus.pio.cmd`.
3. Create four normal endpoints. The ring3 bus task receives from TX and
   sends to RX:

   | Endpoint registry name | Ring3 rights | Purpose |
   | --- | --- | --- |
   | `ps2.port1.tx` | `IPC_RECV | GETNAME` | Client-to-device bytes for port 1 |
   | `ps2.port1.rx` | `IPC_SEND | GETNAME` | Device-to-client bytes for port 1 |
   | `ps2.port2.tx` | `IPC_RECV | GETNAME` | Client-to-device bytes for port 2 |
   | `ps2.port2.rx` | `IPC_SEND | GETNAME` | Device-to-client bytes for port 2 |

4. Create a lifecycle-ready notification and give ring3
   `NOTIFY_SIGNAL | GETNAME`, named `ps2bus.notify`.
5. Create a private runtime-event notification and give ring3
   `NOTIFY_WAIT | NOTIFY_ACK | GETNAME`, named `ps2bus.events`.
6. Bind IRQ 1 to runtime bit 0, IRQ 12 to bit 1, `ps2.port1.tx` readability
   to bit 2, and `ps2.port2.tx` readability to bit 3.
7. Install capabilities and register `ps2.port1.tx`, `ps2.port1.rx`,
   `ps2.port2.tx`, and `ps2.port2.rx`. It also registers the legacy alias
   `ps2.port1` for the port 1 RX endpoint.
8. Launch `ps2bus` with ten bootstrap capabilities, in order: both IRQs, data
   port, command port, port 1 TX/RX, port 2 TX/RX, lifecycle notification,
   runtime notification.

Ring3 initializes the i8042 controller, signals the lifecycle-ready
notification, then waits on the private runtime notification and services the
bound IRQ/IPC inputs. Controller mechanics and byte routing belong in ring3.
`ps2bus_isready()` polls the lifecycle notification's high bit and caches the
result. The startup profile currently busy-waits on this function.

The future initializer must preserve the distinction between the exported
lifecycle-ready object and the private event-loop notification, along with
the four source-to-bit bindings. Those binding records need source object,
notification object, and bitmask fields.

### `ps2-keyboard`

`ps2_keyboard_init()` is an explicit entry point; no current startup profile
calls it. It depends on all four PS/2 bus registry entries already existing.
It performs:

1. Look up `ps2.port1.tx`, `ps2.port1.rx`, `ps2.port2.tx`, and `ps2.port2.rx`.
   Give ring3 SEND for each TX endpoint and RECV for each RX endpoint; all
   four caps also have `GETNAME` and retain the registry names.
2. Create one normal IPC endpoint for the scan-code output. Give ring3
   `IPC_SEND | GETNAME`, named `ps2.keyboard.output`, and register the
   endpoint under `ps2.keyboard.output`.
3. Create a lifecycle-ready notification. Give ring3
   `NOTIFY_SIGNAL | GETNAME`, named `ps2-keyboard.notify`.
4. Launch `ps2-keyboardd` with six bootstrap capabilities, in order: port 1
   TX/RX, port 2 TX/RX, output endpoint, readiness notification.

Ring3 performs device identification and keyboard protocol initialization,
then signals the readiness notification. `ps2_keyboard_isready()` polls bit
63 and caches the result. Ring0 does not receive scan codes and does not
interpret keyboard responses.

The init/readiness entry points are currently declared in the driver-specific
`include/sharkix/kernel/ps2-keyboard.h`, and `ps2_keyboard_init()` has no caller
in the current source tree. This is a current driver-to-startup linkage
dependency, not a generic kernel driver interface.

## Current failure and cleanup gaps

The implementations should be treated as recipes for required operations,
not as a rollback model. VGA and serial halt after many setup failures. Their
`console.output` lookup failure only logs before proceeding, so later setup
may fail on the invalid endpoint. PS/2 bus halts on setup errors. PS/2 keyboard
logs and returns on setup errors, but does not undo objects or registry state
already created; a thread-start error is logged after registration and capset
installation. No driver has a complete transactional cleanup path for partial
initialization. A future init system needs a defined failure result and
reverse-order cleanup policy for every completed operation.

## Metadata contract checklist

To replace the recipes above without losing current behaviour, the init
description for each driver needs enough information to state:

1. **Driver image and task:** selected ring3 image, task name, privilege,
   priority, stack size, entry point, and start-after-setup ordering.
2. **Prerequisites:** named services or registry objects that must already
   exist, plus readiness conditions where mere existence is insufficient.
3. **Resource operations:** create, lookup, subscribe, register, or bind;
   object type; operation parameters; and any ordering constraints.
4. **Capability grants:** source resource, child-visible name, object type, and
   exact rights. A grant must not accidentally expose unrelated capabilities.
5. **Bootstrap ABI:** count, names, and current handle-slot order. Keep the
   bootstrap storage mapped and populated before starting the child.
6. **Event wiring:** for each binding, source, destination notification, and
   opaque signal mask. Keep runtime-event notifications separate from
   lifecycle readiness.
7. **Readiness:** who signals readiness, its object/type and condition, and
   how the init system observes it. Do not infer readiness from task creation
   or resource registration.
8. **Failure and cleanup:** how partially created resources, endpoint
   registrations, bindings, capset entries, address spaces, and threads are
   unwound or reported. Current drivers do not implement a consistent
   rollback policy; several halt the kernel on setup errors.
9. **Identity and ordering:** preserve endpoint names and aliases used by
   clients, and start providers before consumers. In particular, initialize
   the PS/2 bus before the keyboard driver.

This list describes current concrete needs; it does not require that future
metadata use any particular file format, generic device model, or object
framework.
