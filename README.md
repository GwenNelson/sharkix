# Sharkix

Sharkix is an OS project built around a capability-based microkernel.

The core microkernel enforces permissions via capabilities - all syscalls work via capability handles only, never directly on hardware resources - and drivers are as far as possible implemented in userspace.

At present, it is x86_64/amd64 only, but the longer term goal is to be cross platform.

Beyond that, the goal is to provide for different personality layers that are easy to implement,by providing different syscall tables from userspace. This will start with a unix-like environment, as is traditional
in the osdev community, but should be flexible enough for any design.

The design is heavily inspired by seL4, but with some changes due to the author's preferences - for example, there is no such thing as a root task, instead there is an init system (or rather, will be - still under development).

The init system will read metadata from ELF files in initrd and sort dependency ordering for drivers and system services, eventually producing the actual OS environment and personality layer.

By default, tasks in initrd can acquire whatever their metadata says they require - but no more. This is intended as a halfway choice between a strict root task only and "everything goes".

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

Future contributions from third parties are welcome, including from third parties who themselves use AI, but you must warrant that it is your own work, or that any AI assistance is assistance only and not entirely machine generated.

In other words, if you can not explain what the code does, do not understand the design, patches will be rejected. This goes especially for any particularly large diffs - the larger the diff, the more scrutiny it deserves.
If you're not certain, err on the side of "rewrite it manually and comment it properly so that fellow human beings can understand it".

I do not keep track formally anymore of which parts of the overall codebase are AI generated vs entirely my own work, because there is too much intermingling to make the distinction by this point, and there is no legal requirement to do in order to properly claim copyright on the end results.

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
