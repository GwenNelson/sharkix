#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"
#include <sharkix/kernel/pmem.h>
#include <sharkix/kernel/portio.h>
#include <sharkix/kernel/console-vga.h>

static bool vga_ready = false;

enum {
    VGA_CONSOLED_STACK_WORDS = 5,
    VGA_CONSOLED_STACK_BYTES = VGA_CONSOLED_STACK_WORDS * sizeof(uint64_t)
};

extern const uint8_t vga_consoled_image_start[];
extern const uint8_t vga_consoled_image_end[];

// VRAM physical memory
// we don't need to bother with creating the actual VMO - the ring3 driver can do that itself
static pmem_handle_t vram_pem           = PMEM_INVALID_HANDLE;
static cap_handle_t  vram_pmem_cap      = CAP_INVALID_HANDLE;

// port IO
static portio_handle_t vga_portio       = PORTIO_INVALID_HANDLE;
static cap_handle_t    vga_portio_cap   = CAP_INVALID_HANDLE;

// endpoint for receiving actual bytes to write to the screen
// kernel >> user
static ipc_handle_t    vga_endpoint     = IPC_INVALID_HANDLE;
static cap_handle_t    vga_endpoint_cap = CAP_INVALID_HANDLE;

// endpoint for the VGA driver to inform kernel once it's ready to rock
// user >> kernel
static ipc_handle_t    vga_ready_endpoint     = IPC_INVALID_HANDLE;
static cap_handle_t    vga_ready_endpoint_cap = CAP_INVALID_HANDLE;

static void kernel_worker(void *argument) {
    ipc_message_t message = { 0 };

    (void)argument;
    if (ipc_recv(vga_ready_endpoint, &message) != IPC_OK) {
        console_write("vga-consoled ready receive failed\n");
        return;
    }

    console_write("[vga-consoled] VGA driver ready\n");
    vga_ready = true;

    for(;;) thread_yield();
    // for now, this just does a simple loop of spamming ABABABAB over and over
/*    message = (ipc_message_t) {
        .type = IPC_MSGTYPE_SEND,
        .words = { 1, (uint64_t)'A', 0, 0, 0 }
    };
    for (;;) {
        if (ipc_send(thread_current(), vga_endpoint, &message) != IPC_OK) {
            console_write("vga-consoled output send failed\n");
            return;
        }
        message.words[1] = (uint64_t)'B';
        if (ipc_send(thread_current(), vga_endpoint, &message) != IPC_OK) {
            console_write("vga-consoled output send failed\n");
            return;
        }
        message.words[1] = (uint64_t)'A';
    }*/
}

void console_vga_putc(char c) {
	ipc_message_t msg = { .type = IPC_MSGTYPE_SEND,
		              .words = {1,(uint64_t)c,0,0,0 }};
	ipc_send_nb(thread_current(),vga_endpoint,&msg);
}

// utility function that does what the name implies
static int create_user_task(address_space_t **out_as, thread_t **out_thread, uint64_t **out_bootstrap) {
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = vga_consoled_image_start,
        .size = (size_t)(vga_consoled_image_end - vga_consoled_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;

    physical = address_space_translate(address_space,
                                       stack_top - VGA_CONSOLED_STACK_BYTES);
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - VGA_CONSOLED_STACK_BYTES,
        .name = "vga-consoled", .priority = tskIDLE_PRIORITY + 2
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread)
        return -1;

    *out_as = address_space;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

void console_vga_init(void) {
     address_space_t *user_as = NULL;
     thread_t *user_thread = NULL;
     uint64_t *bootstrap = NULL;
  
     // setup the pmem for vram first
     thread_t *worker_thread;

     if (kpmem_create(&vram_pem, (uintptr_t)0xB8000, 0x8000) != 0 ||
         kcap_create(vram_pem, CAP_TYPE_PMEM,
                     CAP_RIGHT_PMEM_MAP | CAP_RIGHT_PMEM_READ |
                     CAP_RIGHT_PMEM_WRITE | CAP_RIGHT_GETNAME,
                     &vram_pmem_cap) != 0) {
         console_write("vga-consoled VRAM setup failed\n");
         for (;;) __asm__ volatile ("cli; hlt");
     }

     // setup the endpoints
     if (ipc_create(&vga_endpoint) != IPC_OK ||
         kcap_create(vga_endpoint, CAP_TYPE_IPC_ENDPOINT,
                     CAP_RIGHT_IPC_RECV | CAP_RIGHT_GETNAME,
                     &vga_endpoint_cap) != 0 ||
         ipc_create(&vga_ready_endpoint) != IPC_OK ||
         kcap_create(vga_ready_endpoint, CAP_TYPE_IPC_ENDPOINT,
                     CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                     &vga_ready_endpoint_cap) != 0) {
         console_write("vga-consoled endpoint setup failed\n");
         for (;;) __asm__ volatile ("cli; hlt");
     }

     // setup the portio
     if (kportio_create(&vga_portio, UINT16_C(0x3D4), 2) != 0 ||
         kcap_create(vga_portio, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_READ | CAP_RIGHT_PORTIO_WRITE |
                     CAP_RIGHT_GETNAME,
                     &vga_portio_cap) != 0) {
         console_write("vga-consoled port I/O setup failed\n");
         for (;;) __asm__ volatile ("cli; hlt");
     }

     // setup the capset for userspace and create the task
     if (create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
         kcap_set_name(vram_pmem_cap, "vga.vram",
                       sizeof("vga.vram") - 1) != 0 ||
         kcap_set_name(vga_portio_cap, "vga.crtc",
                       sizeof("vga.crtc") - 1) != 0 ||
         kcap_set_name(vga_endpoint_cap, "vga.output",
                       sizeof("vga.output") - 1) != 0 ||
         kcap_set_name(vga_ready_endpoint_cap, "vga.ready",
                       sizeof("vga.ready") - 1) != 0 ||
         kcapset_addcap(user_as->capset, vram_pmem_cap) != 0 ||
         kcapset_addcap(user_as->capset, vga_portio_cap) != 0 ||
         kcapset_addcap(user_as->capset, vga_endpoint_cap) != 0 ||
         kcapset_addcap(user_as->capset, vga_ready_endpoint_cap) != 0) {
        console_write("vga-consoled startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    
    // setup the bootstrap with all these caps 
    bootstrap[0] = 4;
    bootstrap[1] = vram_pmem_cap;
    bootstrap[2] = vga_portio_cap;
    bootstrap[3] = vga_endpoint_cap;
    bootstrap[4] = vga_ready_endpoint_cap;

    // start the kernel worker thread
    worker_thread = startup_kernel_thread(kernel_worker, "vga-consoled-worker",
                                          tskIDLE_PRIORITY + 2);
    if (!worker_thread || thread_start(user_thread) != 0) {
        console_write("vga-consoled thread startup failed\n");
        return;
    }

}

bool console_vga_isready(void) {
     return vga_ready;
}
