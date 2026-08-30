#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "arch.h"
#include "thread.h"

#define IDT_PRESENT 0x80
#define IDT_INTERRUPT_GATE 0x0e
#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA 0xa1
#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define MSR_EFER 0xc0000080U
#define MSR_STAR 0xc0000081U
#define MSR_LSTAR 0xc0000082U
#define MSR_FMASK 0xc0000084U
#define MSR_GS_BASE 0xc0000101U
#define MSR_KERNEL_GS_BASE 0xc0000102U

typedef struct __attribute__((packed)) { uint16_t low, selector; uint8_t ist, attr; uint16_t mid; uint32_t high, reserved; } idt_entry_t;
typedef struct __attribute__((packed)) { uint16_t limit; uint64_t base; } descriptor_ptr_t;
typedef struct __attribute__((packed)) {
    uint32_t reserved0; uint64_t rsp0, rsp1, rsp2, ist[7];
    uint64_t reserved1; uint16_t reserved2, iomap_base;
} tss_t;

static idt_entry_t idt[256] __attribute__((aligned(16)));
static uint64_t kernel_gdt[8] __attribute__((aligned(8))) = {
    0x0000000000000000ULL,
    0x00cf9a000000ffffULL, /* 0x08: bootstrap-compatible code */
    0x00cf92000000ffffULL, /* 0x10: kernel data */
    0x00af9a000000ffffULL, /* 0x18: kernel 64-bit code */
    0x00cff2000000ffffULL, /* 0x20: user data */
    0x00affa000000ffffULL  /* 0x28: user 64-bit code */
};
static tss_t tss __attribute__((aligned(16)));
volatile UBaseType_t ulCriticalNesting = 0;

extern void vPortStartFirstTask(void), vPortTimerHandler(void), vPortYieldHandler(void);
extern void vPortDefaultHandler(void), vPortPageFaultHandler(void), vPortInvalidOpcodeHandler(void);
extern void vPortGeneralProtectionHandler(void);
extern void vPortSyscallEntry(void);
extern void vPortTaskBootstrap(void);

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)) : "memory");
}
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high; __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void vPortSetKernelStack(uintptr_t top) { tss.rsp0 = top; }

void vPortInstallKernelGDT(void)
{
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    kernel_gdt[6] = limit | ((base & 0xffffffULL) << 16) | (0x89ULL << 40) | (((base >> 24) & 0xffULL) << 56);
    kernel_gdt[7] = base >> 32;
    descriptor_ptr_t gdtr = { (uint16_t)(sizeof(kernel_gdt) - 1), (uint64_t)(uintptr_t)kernel_gdt };
    __asm__ volatile ("lgdt %0; mov $0x30, %%ax; ltr %%ax" : : "m"(gdtr) : "rax", "memory");
}

void arch_init_cpu_local(void)
{
    /* cpu0 is accessed RIP-relatively until user TLS requires SWAPGS. */
}

void arch_init_syscalls(void)
{
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1ULL | (1ULL << 11)); /* SCE and NXE */
    wrmsr(MSR_STAR, ((uint64_t)0x1b << 48) | ((uint64_t)0x18 << 32));
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)vPortSyscallEntry);
    wrmsr(MSR_FMASK, (1ULL << 9) | (1ULL << 10));
}

static void idt_set_gate(unsigned n, void (*handler)(void), unsigned dpl)
{
    uint64_t a = (uint64_t)(uintptr_t)handler;
    idt[n].low = (uint16_t)a; idt[n].selector = 0x18; idt[n].ist = 0;
    idt[n].attr = IDT_PRESENT | IDT_INTERRUPT_GATE | (uint8_t)(dpl << 5);
    idt[n].mid = (uint16_t)(a >> 16); idt[n].high = (uint32_t)(a >> 32); idt[n].reserved = 0;
}
static void pic_init(void)
{
    uint8_t master = inb(PIC1_DATA), slave = inb(PIC2_DATA);
    outb(PIC1_COMMAND, 0x11); outb(PIC2_COMMAND, 0x11); outb(PIC1_DATA, 0x20); outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 4); outb(PIC2_DATA, 2); outb(PIC1_DATA, 1); outb(PIC2_DATA, 1);
    outb(PIC1_DATA, master & ~1u); outb(PIC2_DATA, slave);
}
static void pit_init(void)
{
    uint16_t divisor = (uint16_t)(1193182u / configTICK_RATE_HZ); if (!divisor) divisor = 1;
    outb(PIT_COMMAND, 0x36); outb(PIT_CHANNEL0, (uint8_t)divisor); outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}
static void port_init_interrupts(void)
{
    for (unsigned i = 0; i < 256; ++i) idt_set_gate(i, vPortDefaultHandler, 0);
    idt_set_gate(6, vPortInvalidOpcodeHandler, 0);
    idt_set_gate(13, vPortGeneralProtectionHandler, 0);
    idt_set_gate(14, vPortPageFaultHandler, 0);
    idt_set_gate(0x20, vPortTimerHandler, 0);
    idt_set_gate(0x21, vPortYieldHandler, 3);
    descriptor_ptr_t idtr = { (uint16_t)(sizeof(idt) - 1), (uint64_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr)); pic_init(); pit_init();
}

StackType_t *pxPortInitialiseStack(StackType_t *top, StackType_t *end, TaskFunction_t code, void *argument)
{
    (void)end;
    uint64_t *stack = (uint64_t *)((uintptr_t)top & ~(uintptr_t)portBYTE_ALIGNMENT_MASK);
    /* Kernel threads are cooperative; user IRET frames explicitly enable
     * interrupts.  Keeping IF clear here prevents a PIT interrupt from
     * arriving in the kernel-task construction/restore window. */
    *--stack = 0x2;
    *--stack = 0x18;
    *--stack = (uint64_t)(uintptr_t)vPortTaskBootstrap;
    *--stack = 0;                            /* rax */
    *--stack = 0;                            /* rbx */
    *--stack = 0;                            /* rcx */
    *--stack = 0;                            /* rdx */
    *--stack = 0;                            /* rbp */
    *--stack = 0;                            /* rsi */
    *--stack = 0;                            /* rdi */
    *--stack = 0;                            /* r8 */
    *--stack = 0;                            /* r9 */
    *--stack = 0;                            /* r10 */
    *--stack = 0;                            /* r11 */
    *--stack = (uint64_t)(uintptr_t)code;    /* r12 */
    *--stack = (uint64_t)(uintptr_t)argument;/* r13 */
    *--stack = 0;                            /* r14 */
    *--stack = 0;                            /* r15 */
    return (StackType_t *)stack;
}

BaseType_t xPortStartScheduler(void) { ulCriticalNesting = 0; port_init_interrupts(); vPortStartFirstTask(); return 0; }
void vPortEndScheduler(void) { for (;;) __asm__ volatile ("cli; hlt"); }
void vPortEnterCritical(void) { portDISABLE_INTERRUPTS(); ++ulCriticalNesting; }
void vPortExitCritical(void)
{
    if (ulCriticalNesting) --ulCriticalNesting;
    /* Kernel Sharkix threads are cooperatively scheduled.  In particular,
     * do not reopen the PIT interrupt window while kernel C code is still
     * returning through a scheduler/FreeRTOS transition.  User frames set
     * IF explicitly when they are entered. */
    if (!ulCriticalNesting &&
        (!thread_current() || thread_current()->privilege == THREAD_PRIVILEGE_USER))
        portENABLE_INTERRUPTS();
}
uint32_t ulPortSetInterruptMask(void) { uint64_t flags; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory"); return (uint32_t)(flags & (1u << 9)); }
void vPortClearInterruptMask(uint32_t value) { if (value) portENABLE_INTERRUPTS(); }
