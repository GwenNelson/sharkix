#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sharkix/kernel/console-vga.h>
#include "memory.h"
#include <sharkix/kernel/pmem.h>
#include <sharkix/kernel/portio.h>
#include <sharkix/kernel/sync.h>
#include <sharkix/kernel/kvalloc.h>
#include <sharkix/kernel/caps.h>
#include <sharkix/kernel/ipc.h>
#include <sharkix/kernel/syscall.h>
#include <sharkix/kernel/thread.h>
#include <sharkix/libsharkix/syscalls.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_PHYS 0xb8000ULL
#define VGA_ATTRIBUTE 0x0f00U
#define VGA_CRTC_BASE 0x3d4U
#define VGA_CRTC_INDEX_OFFSET 0U
#define VGA_CRTC_DATA_OFFSET 1U
//static volatile uint16_t *const vga = (volatile uint16_t *)(PHYSMAP_BASE + VGA_PHYS);
static volatile uint16_t *vga = NULL;

static uintptr_t vga_va;
static uint8_t vga_x, vga_y;
static pmem_handle_t vga_pmem = PMEM_INVALID_HANDLE;
static cap_handle_t vga_cap = CAP_INVALID_HANDLE;
static cap_handle_t vga_vmo_cap = CAP_INVALID_HANDLE;
static portio_handle_t vga_portio = PORTIO_INVALID_HANDLE;
static cap_handle_t vga_portio_cap = CAP_INVALID_HANDLE;
static capset_handle_t vga_server_capset;
static cap_handle_t vga_endpoint_cap = CAP_INVALID_HANDLE;
static cap_handle_t vga_endpoint_send_cap = CAP_INVALID_HANDLE;
static thread_t *vga_server_thread;
static ksemaphore_t vga_server_ready;
static bool vga_ready;

static void internal_console_vga_putc(char c);
static uint16_t vga_cursor_position(void);
static void vga_port_outb(uint32_t offset, uint8_t value);
static uint8_t vga_port_inb(uint32_t offset);

static void console_vga_server_thread(void *argument)
{
    capset_handle_t capset = (capset_handle_t)(uintptr_t)argument;
    cap_t pmem_cap;
    cap_t vmo_cap;
    cap_t portio_cap;
    cap_t endpoint_cap;
    sharkix_syscall_regs_t regs = { 0 };

    if (kcapset_resolve_cap(capset, CAP_TYPE_PMEM,
                            CAP_RIGHT_PMEM_MAP | CAP_RIGHT_PMEM_READ |
                            CAP_RIGHT_PMEM_WRITE, &pmem_cap) != 0 ||
        kcapset_resolve_cap(capset, CAP_TYPE_IPC_ENDPOINT,
                            CAP_RIGHT_IPC_RECV, &endpoint_cap) != 0 ||
        kcapset_resolve_cap(capset, CAP_TYPE_PORTIO,
                            CAP_RIGHT_PORTIO_READ | CAP_RIGHT_PORTIO_WRITE,
                            &portio_cap) != 0)
        for (;;) thread_yield();

    (void)pmem_cap;
    vga_portio_cap = portio_cap.cap_handle;

    // first, we already know pmem_cap, so we need to mint the new cap for vmo_cap
    // this will allow us to map the resulting VMO
    regs.rax = SYSCALL_PMEM_NEW_VMO;
    regs.rdi = pmem_cap.cap_handle;
    regs.rsi = VMO_MAP | VMO_READ | VMO_WRITE;
    sharkix_syscall(&regs);
    vga_vmo_cap = regs.rdx;

    if(regs.rax != VM_OK) {
	console_write("console-vga.c: failed on SYS_PMEM_NEW_VMO!\n");
	console_write("RAX: ");
	console_decimal(regs.rax);
	console_write("\n");
	for(;;) thread_yield();
    }

    // for now we still use kvalloc to get the virtual address
    // but once this is true ring3, we'll implement ualloc, or maybe just identity map vram
    if (kvalloc(0x8000, &vga_va) != 0)
        for (;;) thread_yield();

    // so now we can actually map the VMO hopefully
   
    /*
     * Map the whole VGA framebuffer region at our chosen VA.
     *
     * RDI = VMO cap
     * RSI = VA
     * RDX = offset into VMO
     * R10 = length
     * R8  = mapping rights
     * R9  = flags
     */
     regs = (sharkix_syscall_regs_t){0};

     regs.rax = SYSCALL_VM_MAP;
     regs.rdi = vga_vmo_cap;
     regs.rsi = vga_va;
     regs.rdx = 0;                    /* beginning of VMO */
     regs.r10 = 0x8000;               /* VGA PMEM length */
     regs.r8  = VMO_READ | VMO_WRITE;
     regs.r9  = 0;                    /* no VM_MAP flags yet */
     sharkix_syscall(&regs);

     if(regs.rax != VM_OK) {
	console_write("console-vga.c: failed SYS_VM_MAP!\n");
	console_write("RAX: ");
	console_decimal(regs.rax);
	console_write("\n");
	for(;;) thread_yield();
     }
     
     regs = (sharkix_syscall_regs_t){0};

     // now it should be possible to actually use it!
/*    if (address_space_map_range(address_space_kernel(), vga_va, 0xB8000,
                                0x8000, PAGE_WRITABLE | PAGE_NX) != 0) {
        kvfree(vga_va);
        for (;;) thread_yield();
    }*/

    vga = (uint16_t *)vga_va;
    uint16_t position = vga_cursor_position();
    if (position >= VGA_WIDTH * VGA_HEIGHT) position = 0;
    vga_x = (uint8_t)(position % VGA_WIDTH);
    vga_y = (uint8_t)(position / VGA_WIDTH);
    ksem_post(&vga_server_ready);

    regs.rax = SYSCALL_IPC_RECV;
    regs.rdi = endpoint_cap.cap_handle;
    for (;;) {
        uint64_t words[4];
        uint64_t count;

        sharkix_syscall(&regs);
        if (regs.rax != IPC_MSGTYPE_SEND) {
            regs.rax = SYSCALL_IPC_RECV;
            regs.rdi = endpoint_cap.cap_handle;
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
        regs.rdi = endpoint_cap.cap_handle;
    }
}

static void vga_port_outb(uint32_t offset, uint8_t value)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_OUTB;
    regs.rdi = vga_portio_cap;
    regs.rsi = offset;
    regs.rdx = value;
    sharkix_syscall(&regs);

    if (regs.rax != PORTIO_OK)
        for (;;) thread_yield();
}

static uint8_t vga_port_inb(uint32_t offset)
{
    sharkix_syscall_regs_t regs = { 0 };

    regs.rax = SYSCALL_PORT_INB;
    regs.rdi = vga_portio_cap;
    regs.rsi = offset;
    sharkix_syscall(&regs);

    if (regs.rax != PORTIO_OK)
        for (;;) thread_yield();

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

void console_vga_init(void) {
     // start off by allocating the physical memory region object
     if (kpmem_create(&vga_pmem, 0xB8000, 0x8000) != 0) {
         return;
     }

     if (kportio_create(&vga_portio, VGA_CRTC_BASE, 2) != 0) {
         return;
     }

     // create the cap it needs
     if(kcap_create((kobject_handle_t)vga_pmem, CAP_TYPE_PMEM, CAP_RIGHT_PMEM_MAP|CAP_RIGHT_PMEM_READ|CAP_RIGHT_PMEM_WRITE, &vga_cap) != 0) {
        console_write("console_vga.c:console_vga_init() - failed kcap_create() for VRAM!\n");
        return;
     }

     if (kcap_create((kobject_handle_t)vga_portio, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_READ | CAP_RIGHT_PORTIO_WRITE,
                     &vga_portio_cap) != 0) {
         console_write("console_vga.c:console_vga_init() - failed kcap_create() for VGA ports!\n");
         return;
     }

     // for now, we create the vga-consoled thread in ring0

     ipc_handle_t endpoint;
     ksem_init(&vga_server_ready, 0);
     if (ipc_create(&endpoint) != IPC_OK ||
         kcap_create((kobject_handle_t)endpoint, CAP_TYPE_IPC_ENDPOINT,
                     CAP_RIGHT_IPC_RECV, &vga_endpoint_cap) != 0 ||
         kcap_create((kobject_handle_t)endpoint, CAP_TYPE_IPC_ENDPOINT,
                     CAP_RIGHT_IPC_SEND, &vga_endpoint_send_cap) != 0 ||
         kcapset_new(&vga_server_capset) != 0 ||
         kcapset_addcap(vga_server_capset, vga_cap) != 0 ||
         kcapset_addcap(vga_server_capset, vga_portio_cap) != 0 ||
         kcapset_addcap(vga_server_capset, vga_endpoint_cap) != 0 ||
         /* TODO: temporary while the VGA server shares the kernel address
          * space; remove/fix this when it moves to its own address space/ring 3. */
	 kcapset_addcap(address_space_kernel()->capset, vga_cap) != 0 ||
	 kcapset_addcap(address_space_kernel()->capset, vga_portio_cap) != 0 ||
	 kcapset_addcap(address_space_kernel()->capset, vga_endpoint_send_cap) != 0 ||
         kcapset_addcap(address_space_kernel()->capset, vga_endpoint_cap) != 0) {
         console_write("console-vga.c:console_vga_init() - failed to setup server IPC!\n");
         return;
     }

     thread_create_params_t server_params = {
         .entry_rip = (uintptr_t)console_vga_server_thread,
         .kernel_stack_size = 64 * PAGE_SIZE,
         .name = "vga-consoled",
         .priority = tskIDLE_PRIORITY + 2,
         .argument = (void *)(uintptr_t)vga_server_capset
     };
     vga_server_thread = thread_create(address_space_kernel(),
                                       THREAD_PRIVILEGE_KERNEL,
                                       &server_params);
     if (!vga_server_thread) {
         console_write("console-vga.c:console_vga_init() - failed to create server thread!\n");
         return;
     }

     // and start it!

     if(thread_start(vga_server_thread) != 0) {
        console_write("console-vga.c:console_vga_init() - failed to start server thread!\n");
	return;
     }

     ksem_wait(&vga_server_ready);
     vga_ready = true;
}

bool console_vga_isready(void)
{
    return vga_ready;
}

static void internal_console_vga_putc(char c)
{
    kcritical_enter();
    if (c == '\n') {
        vga_x = 0;
        ++vga_y;
        vga_scroll_if_needed();
        kcritical_exit();
        vga_set_cursor();
        return;
    }
    vga[(size_t)vga_y * VGA_WIDTH + vga_x] = VGA_ATTRIBUTE | (uint8_t)c;
    if (++vga_x == VGA_WIDTH) {
        vga_x = 0;
        ++vga_y;
        vga_scroll_if_needed();
    }
    kcritical_exit();
    vga_set_cursor();
}

void console_vga_putc(char c)
{
    sharkix_syscall_regs_t regs = { 0 };
    thread_t *thread = thread_current();

    if (!thread || thread->address_space != address_space_kernel())
        return;

    regs.rax = SYSCALL_IPC_SEND;
    regs.rdi = vga_endpoint_send_cap;
    regs.rsi = 1;
    regs.rdx = (uint8_t)c;
    sharkix_syscall(&regs);
}
