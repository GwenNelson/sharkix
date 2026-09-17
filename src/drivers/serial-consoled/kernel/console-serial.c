#include <stddef.h>
#include <stdint.h>

#include "caps.h"
#include "console.h"
#include "ipc.h"
#include "memory.h"
#include "program.h"
#include "startup.h"
#include "thread.h"
#include <sharkix/kernel/portio.h>
#include <sharkix/ddk/console-driver.h>

static bool serial_ready = false;

enum {
    SERIAL_CONSOLED_STACK_WORDS = 9,
    SERIAL_CONSOLED_STACK_BYTES = SERIAL_CONSOLED_STACK_WORDS * sizeof(uint64_t)
};

extern const uint8_t serial_consoled_image_start[];
extern const uint8_t serial_consoled_image_end[];

// IPC
static ipc_handle_t serial_endpoint     = IPC_INVALID_HANDLE;
static cap_handle_t   serial_output_cap   = CAP_INVALID_HANDLE;

static ipc_handle_t serial_ready_endpoint = IPC_INVALID_HANDLE;
static cap_handle_t   serial_ready_cap      = CAP_INVALID_HANDLE;

// Keep the UART registers separate so the user driver receives only the ports it uses.
static cap_handle_t    serial_ier_cap = CAP_INVALID_HANDLE;   // IER (Interrupt Enable)
static portio_handle_t serial_ier     = PORTIO_INVALID_HANDLE;

static cap_handle_t    serial_lcr_cap = CAP_INVALID_HANDLE;   // LCR (Line Control)
static portio_handle_t serial_lcr     = PORTIO_INVALID_HANDLE;

static cap_handle_t    serial_mcr_cap = CAP_INVALID_HANDLE;   // MCR (Modem Control)
static portio_handle_t serial_mcr     = PORTIO_INVALID_HANDLE;

static cap_handle_t    serial_iir_cap = CAP_INVALID_HANDLE;   // IIR/FCR
static portio_handle_t serial_iir     = PORTIO_INVALID_HANDLE;

static cap_handle_t    serial_lsr_cap = CAP_INVALID_HANDLE;   // LSR (Line Status)
static portio_handle_t serial_lsr     = PORTIO_INVALID_HANDLE;

static cap_handle_t    serial_com1_cap = CAP_INVALID_HANDLE;  // THR/RBR/DLL
static portio_handle_t serial_com1     = PORTIO_INVALID_HANDLE;

static void kernel_worker(void *argument) {
    console_write("serial-consoled: trying to run!\n");
    ipc_message_t message = { 0 };

    (void)argument;
    if (ipc_recv(serial_ready_endpoint, &message) != IPC_OK) {
        console_write("serial-consoled ready receive failed\n");
        return;
    }

    console_write("[serial-consoled] serial driver ready\n");
    serial_ready = true;

    for(;;) thread_yield();
    // for now, this just does a simple loop of spamming ABABABAB over and over
    /*message = (ipc_message_t) {
        .type = IPC_MSGTYPE_SEND,
        .words = { 1, (uint64_t)'A', 0, 0, 0 }
    };
    for (;;) {
        if (ipc_send(thread_current(), serial_endpoint, &message) != IPC_OK) {
            console_write("serial-consoled output send failed\n");
            return;
        }
        message.words[1] = (uint64_t)'B';
        if (ipc_send(thread_current(), serial_endpoint, &message) != IPC_OK) {
            console_write("serial-consoled output send failed\n");
            return;
        }
        message.words[1] = (uint64_t)'A';
    }*/
}

void console_serial_putc(char c) {
	ipc_message_t msg = { .type = IPC_MSGTYPE_SEND,
		              .words = {1,(uint64_t)c,0,0,0 }};
	ipc_send_nb(thread_current(),serial_endpoint,&msg);
}

// utility function that does what the name implies
static int create_user_task(address_space_t **out_as, thread_t **out_thread, uint64_t **out_bootstrap) {
    address_space_t *address_space;
    uintptr_t stack_top;
    uint64_t physical;
    thread_create_params_t params;
    const program_image_t image = {
        .data = serial_consoled_image_start,
        .size = (size_t)(serial_consoled_image_end - serial_consoled_image_start)
    };

    address_space = address_space_create(0);
    if (!address_space ||
        program_map_flat_image(address_space, &image,
                               PROGRAM_DEFAULT_LOAD_ADDRESS) != 0 ||
        program_map_user_stack(address_space, PROGRAM_DEFAULT_STACK_BASE,
                               PAGE_SIZE, &stack_top) != 0)
        return -1;

    physical = address_space_translate(address_space,
                                       stack_top - SERIAL_CONSOLED_STACK_BYTES);
    if (physical == UINT64_MAX)
        return -1;

    params = (thread_create_params_t) {
        .entry_rip = PROGRAM_DEFAULT_LOAD_ADDRESS,
        .initial_stack_pointer = stack_top - SERIAL_CONSOLED_STACK_BYTES,
        .name = "serial-consoled", .priority = THREAD_PRIORITY_NORMAL
    };
    *out_thread = thread_create(address_space, THREAD_PRIVILEGE_USER, &params);
    if (!*out_thread)
        return -1;

    *out_as = address_space;
    *out_bootstrap = (uint64_t *)phys_to_virt(physical);
    return 0;
}

void console_serial_init(void) {
     address_space_t *user_as = NULL;
     thread_t *user_thread = NULL;
     uint64_t *bootstrap = NULL;
  
     thread_t *worker_thread;

     // setup the endpoints
     if (ipc_create(&serial_endpoint) != IPC_OK ||
         kcap_create(serial_endpoint, CAP_TYPE_IPC_ENDPOINT,
                     CAP_RIGHT_IPC_RECV | CAP_RIGHT_GETNAME,
                     &serial_output_cap) != 0 ||
         ipc_create(&serial_ready_endpoint) != IPC_OK ||
         kcap_create(serial_ready_endpoint, CAP_TYPE_IPC_ENDPOINT,
                     CAP_RIGHT_IPC_SEND | CAP_RIGHT_GETNAME,
                     &serial_ready_cap) != 0) {
         console_write("serial-consoled endpoint setup failed\n");
         for (;;) __asm__ volatile ("cli; hlt");
     }

     // Set up one-byte capabilities for the COM1 UART registers used by userspace.
     if (kportio_create(&serial_ier, UINT16_C(0x3F9), 1) != 0 ||
         kcap_create(serial_ier, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_WRITE | CAP_RIGHT_GETNAME,
                     &serial_ier_cap) != 0 ||
         kportio_create(&serial_lcr, UINT16_C(0x3FB), 1) != 0 ||
         kcap_create(serial_lcr, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_WRITE | CAP_RIGHT_GETNAME,
                     &serial_lcr_cap) != 0 ||
         kportio_create(&serial_mcr, UINT16_C(0x3FC), 1) != 0 ||
         kcap_create(serial_mcr, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_WRITE | CAP_RIGHT_GETNAME,
                     &serial_mcr_cap) != 0 ||
         kportio_create(&serial_iir, UINT16_C(0x3FA), 1) != 0 ||
         kcap_create(serial_iir, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_WRITE | CAP_RIGHT_GETNAME,
                     &serial_iir_cap) != 0 ||
         kportio_create(&serial_lsr, UINT16_C(0x3FD), 1) != 0 ||
         kcap_create(serial_lsr, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_READ | CAP_RIGHT_GETNAME,
                     &serial_lsr_cap) != 0 ||
         kportio_create(&serial_com1, UINT16_C(0x3F8), 1) != 0 ||
         kcap_create(serial_com1, CAP_TYPE_PORTIO,
                     CAP_RIGHT_PORTIO_WRITE | CAP_RIGHT_GETNAME,
                     &serial_com1_cap) != 0) {
         console_write("serial-consoled port I/O setup failed\n");
         for (;;) __asm__ volatile ("cli; hlt");
     }

     // setup the capset for userspace and create the task
     if (create_user_task(&user_as, &user_thread, &bootstrap) != 0 ||
         kcap_set_name(serial_output_cap, "serial.out",
                       sizeof("serial.out") - 1) != 0 ||
         kcap_set_name(serial_ready_cap, "serial.ready",
                       sizeof("serial.ready") - 1) != 0 ||
         kcap_set_name(serial_ier_cap, "serial.ier",
                       sizeof("serial.ier") - 1) != 0 ||
         kcap_set_name(serial_lcr_cap, "serial.lcr",
                       sizeof("serial.lcr") - 1) != 0 ||
         kcap_set_name(serial_mcr_cap, "serial.mcr",
                       sizeof("serial.mcr") - 1) != 0 ||
         kcap_set_name(serial_iir_cap, "serial.iir",
                       sizeof("serial.iir") - 1) != 0 ||
         kcap_set_name(serial_lsr_cap, "serial.lsr",
                       sizeof("serial.lsr") - 1) != 0 ||
         kcap_set_name(serial_com1_cap, "serial.com1",
                       sizeof("serial.com1") - 1) != 0 ||
         kcapset_addcap(user_as->capset, serial_output_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_ready_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_ier_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_lcr_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_mcr_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_iir_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_lsr_cap) != 0 ||
         kcapset_addcap(user_as->capset, serial_com1_cap) != 0) {
        console_write("serial-consoled startup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    
    // Preserve the capability order expected by serial_consoled_main().
    bootstrap[0] = 8;
    bootstrap[1] = serial_output_cap;
    bootstrap[2] = serial_ready_cap;
    bootstrap[3] = serial_ier_cap;
    bootstrap[4] = serial_lcr_cap;
    bootstrap[5] = serial_mcr_cap;
    bootstrap[6] = serial_iir_cap;
    bootstrap[7] = serial_lsr_cap;
    bootstrap[8] = serial_com1_cap;

    // Match the user task's priority so the ready waiter cannot starve startup.
    worker_thread = startup_kernel_thread(kernel_worker, "serial-consoled-worker",
                                          THREAD_PRIORITY_NORMAL);
    if (!worker_thread || thread_start(user_thread) != 0) {
        console_write("serial-consoled thread startup failed\n");
        return;
    }

}

bool console_serial_isready(void) {
     return serial_ready;
}

REGISTER_CONSOLE_DRIVER(serial,console_serial_init,console_serial_isready,console_serial_putc);
