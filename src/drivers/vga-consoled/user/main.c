#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

// identity mapped for simplicity - but we could in theory map this anywhere
// at some point point in the future this will come from a virtual address allocator instead
// for now, we just use this
#define VGA_MAP_VADDR 0xB8000ULL
#define VGA_VRAM_LEN  0x8000ULL

// various defines needed for the VGA driver to do its thing
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_PHYS 0xb8000ULL
#define VGA_ATTRIBUTE 0x0f00U
#define VGA_CRTC_BASE 0x3d4U
#define VGA_CRTC_INDEX_OFFSET 0U
#define VGA_CRTC_DATA_OFFSET 1U

// this is temporary until we move a lot of the ABI stuff somewhere nicer
#define VMO_NONE          UINT64_C(0)
#define VMO_MAP          (UINT64_C(1) << 0)
#define VMO_READ         (UINT64_C(1) << 1)
#define VMO_WRITE        (UINT64_C(1) << 2)
#define VMO_EXEC         (UINT64_C(1) << 3)

static uint64_t vga_output_cap;
static uint64_t vga_ready_cap;
static uint64_t vga_vram_pmem_cap;
static uint64_t vga_crtc_cap;
static uint64_t vga_vram_vmo_cap;

static uint16_t *vga = NULL;
static uint8_t vga_x, vga_y;

static void internal_console_vga_putc(char c);
static uint16_t vga_cursor_position(void);
static void vga_port_outb(uint32_t offset, uint8_t value);
static uint8_t vga_port_inb(uint32_t offset);

static uint64_t syscall_call(uint64_t number, uint64_t arg0, uint64_t arg1)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = number;
    regs.rdi = arg0;
    regs.rsi = arg1;
    (void)sharkix_syscall(&regs);
    return regs.rax;
}

static void test_exit(void)
{
    (void)syscall_call(SYSCALL_TEST_EXIT, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}


static void vga_log(char* msg) {
}

static void vga_panic(char* msg) {
	sharkix_debug_puts("VGA PANIC! ");
	sharkix_debug_puts(msg);
	test_exit();
}

static void vga_signal_ready(void) {
	sharkix_syscall_regs_t regs = { 0 };
	// we don't actually care what we send, just need to signal to the kernel we are ready
	// maybe at some point in future we can use this endpoint to signal other stuff too
	// such as error states for example?
	regs.rax = SYSCALL_IPC_SEND;
	regs.rdi = vga_ready_cap;
	sharkix_syscall(&regs);
	if(regs.rax != 0) {
		vga_panic("Failed signalling ready state to kernel via SYSCALL_IPC_SEND!");
	}
}

static uint64_t vmo_from_pmem(uint64_t pmem_cap, uint64_t flags) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = SYSCALL_PMEM_NEW_VMO;
	regs.rdi = pmem_cap;
	regs.rsi = flags;
	sharkix_syscall(&regs);
	if(regs.rax != 0) {
		vga_panic("Could not create VMO for VRAM from PMEM!");
	}
	return regs.rdx;
}

static void map_vmo(uint64_t vmo_cap, uint64_t virt_addr, uint64_t offset, uint64_t len, uint64_t map_rights) {
	sharkix_syscall_regs_t regs = { 0 };
	regs.rax = SYSCALL_VM_MAP;
	regs.rdi = vmo_cap;
	regs.rsi = virt_addr;
	regs.rdx = offset;
	regs.r10 = len;
	regs.r8  = map_rights;
	regs.r9  = 0;
	sharkix_syscall(&regs);
	if(regs.rax != 0) {
		vga_panic("Could not map VMO!\n");
	}
}

static void vga_port_outb(uint32_t offset, uint8_t value)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_OUTB;
    regs.rdi = vga_crtc_cap;
    regs.rsi = offset;
    regs.rdx = value;
    sharkix_syscall(&regs);

    if (regs.rax != 0) {
        vga_panic("vga_port_outb() failed!");
    }
}

static uint8_t vga_port_inb(uint32_t offset)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_INB;
    regs.rdi = vga_crtc_cap;
    regs.rsi = offset;
    sharkix_syscall(&regs);

    if (regs.rax != 0) {
        vga_panic("vga_port_inb() failed!");
    }

    return (uint8_t)regs.rdx;
}

static uint16_t vga_cursor_position(void)
{
    vga_port_outb(VGA_CRTC_INDEX_OFFSET, 0x0e);
    uint16_t position = (uint16_t)vga_port_inb(VGA_CRTC_DATA_OFFSET) << 8;
    vga_port_outb(VGA_CRTC_INDEX_OFFSET, 0x0f);
    return position | vga_port_inb(VGA_CRTC_DATA_OFFSET);
}

static void vga_set_cursor(void)
{
    uint16_t position = (uint16_t)vga_y * VGA_WIDTH + vga_x;
    vga_port_outb(VGA_CRTC_INDEX_OFFSET, 0x0e);
    vga_port_outb(VGA_CRTC_DATA_OFFSET, (uint8_t)(position >> 8));
    vga_port_outb(VGA_CRTC_INDEX_OFFSET, 0x0f);
    vga_port_outb(VGA_CRTC_DATA_OFFSET, (uint8_t)position);
}

static void vga_scroll_if_needed(void)
{
    if (vga_y < VGA_HEIGHT) return;
    for (size_t cell = 0; cell < (VGA_HEIGHT - 1) * VGA_WIDTH; ++cell)
        vga[cell] = vga[cell + VGA_WIDTH];
    for (size_t column = 0; column < VGA_WIDTH; ++column)
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + column] = VGA_ATTRIBUTE | ' ';
    vga_y = VGA_HEIGHT - 1;
}

void run_vga_service(void) {
	uint16_t position = vga_cursor_position();
	if (position >= VGA_WIDTH * VGA_HEIGHT) position = 0;
	vga_x = (uint8_t)(position % VGA_WIDTH);
	vga_y = (uint8_t)(position / VGA_WIDTH);

	vga_log("READY!");
	vga_signal_ready();

	sharkix_syscall_regs_t regs = { 0 };
        regs.rax = SYSCALL_IPC_RECV;
        regs.rdi = vga_output_cap;
        for (;;) {
            uint64_t words[4];
            uint64_t count;

            sharkix_syscall(&regs);
            if (regs.rax != 0) {   /* loop until we get an actual message */
                regs.rax = SYSCALL_IPC_RECV;
                regs.rdi = vga_output_cap;
                continue;
            }
            count = regs.rsi;
            if (count > 32) count = 32;
            words[0] = regs.rdx;
            words[1] = regs.r10;
            words[2] = regs.r8;
            words[3] = regs.r9;
            for (uint64_t i = 0; i < count; ++i)
                 internal_console_vga_putc((char)(words[i / 8] >> ((i % 8) * 8)));
            regs.rax = SYSCALL_IPC_RECV;
            regs.rdi = vga_output_cap;
        }
	
}

static void internal_console_vga_putc(char c) {
    if (c == '\n') {
        vga_x = 0;
        ++vga_y;
        vga_scroll_if_needed();
        vga_set_cursor();
        return;
    }
    vga[(size_t)vga_y * VGA_WIDTH + vga_x] = VGA_ATTRIBUTE | (uint8_t)c;
    if (++vga_x == VGA_WIDTH) {
        vga_x = 0;
        ++vga_y;
        vga_scroll_if_needed();
    }
    vga_set_cursor();
}


void driver_user_main(uint64_t *bootstrap) {
    uint64_t handles[4];
    char *capv[] = {
        "vga.output",
        "vga.ready",
        "vga.vram",
        "vga.crtc",
    };

    if (!bootstrap || bootstrap[0] != 4) {
	vga_panic("invalid bootstrap!");
    }

    if (sharkix_get_bootstrap(handles, capv, 4, bootstrap) !=
        SHARKIX_BOOTSTRAP_OK) {
        vga_panic("bootstrap discovery failed\n");
    }

    vga_output_cap = handles[0];
    vga_ready_cap = handles[1];
    vga_vram_pmem_cap = handles[2];
    vga_crtc_cap = handles[3];

    vga_log("obtained required caps");
    
    vga_log("creating VMO");
    // now let's create our VMO for vram and map it
    vga_vram_vmo_cap = vmo_from_pmem(vga_vram_pmem_cap, VMO_MAP | VMO_READ | VMO_WRITE);
   
    vga_log("mapping VMO"); 
    // we'll identity-map VRAM
    map_vmo(vga_vram_vmo_cap,VGA_MAP_VADDR,0,VGA_VRAM_LEN,VMO_READ|VMO_WRITE);
    
    vga_log("mapped VRAM OK!");
    vga = (uint16_t*)VGA_MAP_VADDR;

    run_vga_service();

    test_exit();
}
