Keep VGA driver inside kernel still for now, but begin pretending it's in userspace
Implement syscalls for the new pmem_t caps and mapping them etc
Make the kernel startup pass a pmem_t cap to VGA console driver and an IPC endpoint cap
Have the rest of the kernel console subsystem work by just spitting out characters to that IPC endpoint, with the VGA driver doing IPC_RECV in a loop
Implement the port IO stuff - or rather design it?
Once we have port IO stuff setup, we can move both the VGA driver and the serial driver into userspace
    To do this, start with a flat binary for now perhaps?
    Then add an ELF loader
Add a special "early debug only" bochs E9 driver that only exists in the debug branch of the kernel, so it's actually a fucking MICRO kernel

Begin work on moving to starting drivers after the rest of the kernel is ready, and then having those drivers initialized by a config file of some kind telling what memory ranges and ports etc get mapped to what caps
Come up with an ABI for how drivers receive the caps they need to work - probably via SysV AUXV eventually once we have ELF, but for now just shove into the stack

For console driver:
    Probably actually have a userspace "multiplexer" that just passes all characters it receives to all registered console drivers perhaps? Maybe later


