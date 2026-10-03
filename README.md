# Sharkix

Sharkix is an OS project built around a capability-based microkernel.

The core microkernel enforces permissions via capabilities - all syscalls work via capability handles only, never directly on hardware resources - and drivers are as far as possible implemented in userspace.

At present, it is x86_64/amd64 only, but the longer term goal is to be cross platform.

Beyond that, the goal is to provide for different personality layers that are easy to implement, by providing different syscall tables from userspace. This will start with a unix-like environment, as is traditional
in the osdev community, but should be flexible enough for any design.

The design is heavily inspired by seL4, but with some changes due to the author's preferences - for example, there is no such thing as a root task, instead there is an init system (or rather, will be - still under development).

The init system will read metadata from ELF files in initrd and sort dependency ordering for drivers and system services, eventually producing the actual OS environment and personality layer.

By default, tasks in initrd can acquire whatever their metadata says they require - but no more. This is intended as a halfway choice between a strict root task only and "everything goes". (At time of writing this isn't fully implemented yet, work in progress)

## Basic design

Sharkix is split up into a few basic layers:

 1. Architecture and boot protocol specific code (at present there's still a LOT of x86-64 specific stuff not yet properly abstracted, but the long-term intent is to move this stuff into the arch layer).
    This layer is also where you'll find the multiboot1-specific code, and there's a handy little tool called bootstub32 used for qemu's -kernel param, go read that code for details
 2. The kernel core, in src/kernel/core - again, the abstraction isn't perfect yet, but it's intended that absolutely core stuff that can't go elsewhere lives entirely here, you'll find the scheduler, basic virtual memory,
    page allocator and other "every kernel needs this" stuff there, a lot of older code lives here and is scheduled for rewriting
 3. The "subsystems" and kobjects - these represent various "objects" (not in the traditional OOP sense) with handles - every handle is a uint64_t and each subsystem is responsible for allocation and operations on these
    objects. The rest of the kernel should NOT ever directly operate on pointers to these objects as far as possible, unless unavoidable. This has the IPC subsystem, abstract VMOs (Virtual Memory Objects),
    knotify (notification objects) and other useful stuff.
 4. Caps/capabilities - this is technically another subsystem, but it's a special one - a cap is just a handle with a set of permissions attached via a bitmask, and it is ESSENTIAL that nothing in ring3 is able to ever touch
    subsystems except via caps. This is how sharkix guarantees security, and any access via ring3 to a kobject, or worse, into core, is considered a critical security bug.
 5. Syscalls - technically part of core for now, syscalls are like in any other kernel how ring3/userspace interacts with the kernel's facilities - subsystems are exposed via syscalls which operate on cap handles, caps can
    be given names (look at the CAP_SETNAME and CAP_GETNAME perms) - at time of writing this isn't properly abstracted, the longer term intent is for each subsystem to contain its own syscall implementations and to use more
    arch-independent APIs
 6. The profile system - profiles are what controls what the kernel actually DOES at startup, it's used for implementing various tests and is used heavily during development - the pattern is to implement a new feature and then
    add a new profile that exercises and tests it.
 7. Drivers - drivers are at time of writing split into ring0 kernel modules that setup hardware resources from the various subsystems and create caps for the ring3 implementation of the actual driver - ideally nearly ALL drivers
    should be entirely ring3 eventually, and the long-term plan is to have an init system that will load ELFs from initrd according to metadata (see above).
 8. "Personality layer" - not yet implemented, this will eventually be how the positive syscalls are implemented, the design is in docs/PERSONALITY.md, at present positive numbered syscalls are a small set of basic test syscalls.
    Negative numbered syscalls are used for native sharkix caps operations, while positive syscalls will be dealt with by the personality layer.
 9. Init system - not yet implemented, will eventually be responsible for running the drivers, resolving dependencies and handing out resources to them, spawning the personality layer and whatever else makes this thing an actual
    operating system, somewhat akin to (but not quite the same as) an seL4 root task.

## Design philosophy

Sharkix is a microkernel - if you don't know what that means, please research the subject.

A simple summary: Microkernels put as much as possible into ring3 and rely on IPC instead of making the kernel responsible for everything (monolithic).

Sharkix is intended to provide "mechanism not policy" as far as possible, while still being pragmatic. Drivers can be provided with the kernel but don't have to be used in any particular deployment and are easy to disable in the
build system.

Whenever allowing greater flexibility is possible, it should be done. If something wouldn't induce a security hole or reliability issue (or it only induces a security hole if userspace misuses it in weird ways), it's allowed.

A "slogan" might be: Let geniuses be clever.

Let geniuses be clever, but also try and respect the principle of least privilege as far as possible too, and don't be "too clever" in the actual kernel.

"cleverness" should be built on top of sharkix, but not inside sharkix itself - and to be clear, by this I mean that the kernel itself should remain simple and easy to understand as far as possible.

Under no circumstances should the microkernel itself begin adopting drivers that belong in userspace into ring0 - this can be done TEMPORARILY using the profiles system for rapid development, but never as part of the default build.

When using the profiles system, take care to make it easier to port to ring3 later - for example, use caps instead of directly calling subsystems whenever possible, and consider using libsharkix-kernel (grep the code for details).

## IPC subsystem

For a full understanding, you should read the code, but here's the basics:

Sharkix provides for IPC endpoints - these are FIFOs that at present default to 64 entries (this might be revised in future, including dynamic resizing). In the most basic case, a sender can write to the FIFO, and if full the writer
thread blocks until a reader receives. Non-blocking operations are available which will return immediately if the FIFO is full (for send) or if there's nothing available (for receive).

On top of this we also have the PUBSUB system - an endpoint can be created as a PUBSUB publisher or as a "subscription" - with a publisher, all sends result in a write to every subscribed endpoint - the semantics of this are subject
to change in future of course as there are still questions about policy for slow subscribers etc.

Read include/sharkix/kernel/subsystems/kipc.h for a brief idea of the API, and read the various profiles and userspace code to understand more about how IPC works.

IPC is still not fully optimized yet, there is more to be done, but it is currently fairly performant - obviously as a microkernel, sharkix will ultimately live or die by IPC performance, so this subsystem is especially important.

Messages consist of 5 64-bit words, and at present we do not yet have a means for sending caps over IPC, though this is planned as it is obviously going to be needed to make the system of practical use.

Be careful to check the behavioural semantics with PUBSUB, it can sometimes be surprising.

The kernel core currently contains an IPC registry, intended for use by drivers - this will at some point be moved out into userspace, it is currently used for the console system and tracks endpoints for console.input and console.output character streams, which work by sending a single character cast to a uint64_t over IPC. This protocol is intended to be expanded too eventually, word0 is a character count (currently always 1), and then the other 4 words will be packed with bytes, that must be shifted out of the 64-bit words to deserialize.

The IPC system is NOT meant to be the correct place for bulk transfers - for that use shared VMOs (Virtual Memory Objects) instead, and use IPC for control operations.

IPC endpoints can be bound to knotify objects instead of having to poll repeatedly - this is useful for checking multiple endpoints at once, similar to the traditional POSIX select call, so a single thread can service multiple endpoints.

## IRQ handling and knotify

IRQs are handled in userspace by drivers - the kernel kirq subsystem handles kirq_t kobjects which can be accessed via caps, these can also be bound into knotify objects - one knotify object can be bound to up to 64 event sources including IRQs, endpoints or just anything you care to express using knotify_signal() and the related syscalls. knotify is intended as the primary means by which a service makes a number of listeners aware of an event occurring, a 
classic use is to use knotify to indicate that there's something waiting on an IPC endpoint - though this can be somewhat redundant (because IPC endpoints can also be directly bound to a knotify object too), sharkix does not impose
policy.

## Basic instructions

This is still under heavy development, but use "make run PROFILE=whatever" to run a custom profile, or "make run" to run the default "normal" profile.

Note that if you want to actually interact with the serial console, you should use "make run-serial"

VGA can be tested by passing QEMU_DISPLAY= - this is also needed for testing the PS/2 driver (and eventually mouse and graphics and such)

At present, most of this is not very exciting to use, but there's lots of machinery behind the scenes

## Regarding use of AI / LLMs

Some parts of Sharkix have been written with assistance from Codex and other LLM tooling. I want to be open about that rather than pretending otherwise.

I use these tools primarily to speed up well-defined implementation work, particularly when the design and required behaviour have already been decided.

Generated code is reviewed before it is accepted, and I frequently modify, rewrite, or refactor it afterwards. 

Architectural decisions and overall direction remain my responsibility.

A typical example of how I use Codex is:

```c
void somefunc(void) {
     // CODEX BEGIN
     // Please implement this function. It must:
     //   1. bla bla
     //   2. bla bla bla
     // CODEX END
}
```

I would then ask codex to inspect the file, implement only between the two comments, and then review the results and refactor or rewrite.
Some older parts of the codebase especially are more heavily written by codex or similar tools, and are slowly being audited and rewritten.

This is being documented for transparency, and because I want to be clear:
Although portions are AI-generated, the model is being used as essentially an implementation assistant for existing designs, not for "vibe coding". The difference being, I get it to do the boring "plumbing" or boilerplate stuff
while I keep control of high-level algorithm, architectural and design choices as far as possible. I do not ever blindly accept generated code without reviewing it first.

Sharkix is distributed under the terms of the GPLv2, and use of AI contributions does not change the license.

Furthermore, other than this very README, a lot of the documentation under docs/ and the GENERAL-TODO.md etc contain notes maintained by various AI assistants - but these are notes produced as extensive summaries of my own chats
with these agents, a sort of "offloaded memory" for the most part, while others are audit results and analysis from those AIs. I use this to help keep track of my own progress as I work on sharkix, the AI documentation is NOT the
actual "source of truth", and the actual design decisions remain my own, I only use AI-assisted notes to help organize my thoughts, and I use AIs to analyze progress and bughunt.

The actual source of truth is the code, my own statements (such as this file) and anything I explicitly indicate as formal documentation. AI-maintained notes are working material and may be incomplete, stale, or wrong.

In other words, for now assume only this README, code comments and my own words are accurate.

External contributors should NOT modify these files, but may submit new documentation.

## Regarding contributions

Future contributions from third parties are welcome, including from third parties who themselves use AI.

AI-assisted contributions are welcome, but contributors must understand, review and take responsibility for the code they submit. Do not submit substantial machine-generated patches that you cannot independently explain, review and maintain.

In other words, if you can not explain what the code does or do not understand the design your patches will be rejected. This goes especially for any particularly large diffs - the larger the diff, the more scrutiny it deserves.
If you're not certain, err on the side of "rewrite it manually and comment it properly so that fellow human beings can understand it".

I do not keep track formally anymore of which parts of the overall codebase are AI generated vs entirely my own work, because there is too much intermingling to make the distinction by this point, and there is no legal requirement to do in order to properly claim copyright on the end results. But I DO expect any contributions from other parties to reasonably be able to say they do actually understand it, so please don't submit a patch generated by an AI or blindly copy/pasted from somewhere else - especially the latter, if another human wrote it, respect the license and make clear that it isn't your own work, attribute it properly.

Copyright law as it applies to AI output is still a developing area of law, but it is highly unlikely in my view that the courts or legislature will decide that any use of AI anywhere in the project "taints" the whole project and
 renders it unprotected by copyright. Should the law ever change in this area, the author will make efforts to audit for and replace all AI contributions with manually rewritten code so that the project remains suitable for
distribution under the GPLv2 and compatible licenses.

## Regarding "code of conduct" policies

The author does not consider a CoC to be especially important for the vast majority of projects. Use common sense and treat people decently. Don't be a dick basically.
If anyone does not want to use this software or contribute to it due to the absence of a formal CoC policy, that is their own decision.

But for the avoidance of doubt, the author is openly queer, transgender and autistic and so obviously won't tolerate abuse based on those traits or any others in anything under my control. I just don't feel the need to write up a
full formal policy about this.

Focus on the code, be human about it but focus on the code.

It's okay to have fun and I encourage it - many of my commit logs and comments contain humour, some of it some people might find unprofessional - I remind them this is literally a hobbyist project i'm doing for fun.

If you're a potential future employer or client, don't worry - I promise I am more professional in actual professional contexts, but not when i'm doing something for fun and learning in my own spare time.

If somehow sharkix ever gets "big", i'll make sure it gets cleaned of any profanity etc where that's counterproductive, but otherwise i'm here to have fun.
