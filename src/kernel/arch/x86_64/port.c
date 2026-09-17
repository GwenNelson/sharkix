#include <stdint.h>

#include "arch.h"

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

typedef struct __attribute__((packed)) {
    uint16_t low, selector;
    uint8_t ist, attr;
    uint16_t mid;
    uint32_t high, reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} descriptor_ptr_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2, ist[7];
    uint64_t reserved1;
    uint16_t reserved2, iomap_base;
} tss_t;

static idt_entry_t idt[256] __attribute__((aligned(16)));
static uint64_t kernel_gdt[9] __attribute__((aligned(8))) = {
    0x0000000000000000ULL,
    0x00cf9a000000ffffULL, /* 0x08: bootstrap-compatible code */
    0x00cf92000000ffffULL, /* 0x10: kernel data */
    0x00af9a000000ffffULL, /* 0x18: kernel 64-bit code */
    0x00cf92000000ffffULL, /* 0x20: SYSCALL kernel data */
    0x00cff2000000ffffULL, /* 0x28: user data */
    0x00affa000000ffffULL  /* 0x30: user 64-bit code */
};
static tss_t tss __attribute__((aligned(16)));
static unsigned critical_nesting;
static uintptr_t critical_outer_flags;

extern void arch_start_first_thread(uintptr_t saved_context);
extern void arch_timer_handler(void), arch_yield_handler(void);
extern void arch_default_handler(void), arch_page_fault_handler(void);
extern void arch_invalid_opcode_handler(void), arch_general_protection_handler(void);
extern void arch_syscall_entry(void), arch_thread_bootstrap(void);

extern void arch_irq0_handler(void);
extern void arch_irq1_handler(void);
extern void arch_irq2_handler(void);
extern void arch_irq3_handler(void);
extern void arch_irq4_handler(void);
extern void arch_irq5_handler(void);
extern void arch_irq6_handler(void);
extern void arch_irq7_handler(void);
extern void arch_irq8_handler(void);
extern void arch_irq9_handler(void);
extern void arch_irq10_handler(void);
extern void arch_irq11_handler(void);
extern void arch_irq12_handler(void);
extern void arch_irq13_handler(void);
extern void arch_irq14_handler(void);
extern void arch_irq15_handler(void);

static void (*const irq_handlers[16])(void) = {
    arch_irq0_handler,
    arch_irq1_handler,
    arch_irq2_handler,
    arch_irq3_handler,
    arch_irq4_handler,
    arch_irq5_handler,
    arch_irq6_handler,
    arch_irq7_handler,
    arch_irq8_handler,
    arch_irq9_handler,
    arch_irq10_handler,
    arch_irq11_handler,
    arch_irq12_handler,
    arch_irq13_handler,
    arch_irq14_handler,
    arch_irq15_handler,
};


static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32)) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void arch_set_kernel_stack(uintptr_t top)
{
    tss.rsp0 = top;
}

void arch_install_kernel_gdt(void)
{
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    descriptor_ptr_t gdtr;

    kernel_gdt[7] = limit | ((base & 0xffffffULL) << 16) | (0x89ULL << 40) |
                    (((base >> 24) & 0xffULL) << 56);
    kernel_gdt[8] = base >> 32;
    gdtr.limit = (uint16_t)(sizeof(kernel_gdt) - 1);
    gdtr.base = (uint64_t)(uintptr_t)kernel_gdt;
    __asm__ volatile ("lgdt %0; mov $0x38, %%ax; ltr %%ax"
                      : : "m"(gdtr) : "rax", "memory");
}

void arch_init_cpu_local(void)
{
    /* cpu0 is accessed RIP-relatively until user TLS requires SWAPGS. */
}

void arch_init_syscalls(void)
{
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1ULL | (1ULL << 11)); /* SCE and NXE */
    /* SYSCALL selects 0x18/0x20. SYSRETQ derives user SS 0x2b and CS 0x33. */
    wrmsr(MSR_STAR, ((uint64_t)0x23 << 48) | ((uint64_t)0x18 << 32));
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)arch_syscall_entry);
    wrmsr(MSR_FMASK, (1ULL << 9) | (1ULL << 10));
}

static void idt_set_gate(unsigned vector, void (*handler)(void), unsigned dpl)
{
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].low = (uint16_t)address;
    idt[vector].selector = 0x18;
    idt[vector].ist = 0;
    idt[vector].attr = IDT_PRESENT | IDT_INTERRUPT_GATE | (uint8_t)(dpl << 5);
    idt[vector].mid = (uint16_t)(address >> 16);
    idt[vector].high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static void pic_init(void)
{
    uint8_t master = inb(PIC1_DATA);
    uint8_t slave = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    outb(PIC1_DATA, 1);
    outb(PIC2_DATA, 1);
    outb(PIC1_DATA, master & ~1u);
    outb(PIC2_DATA, slave);
}

static void pit_init(void)
{
    uint16_t divisor = (uint16_t)(1193182u / SCHEDULER_TICKS_PER_SECOND);
    if (!divisor) divisor = 1;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)divisor);
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}

static void interrupts_init(void)
{
    descriptor_ptr_t idtr;

    for (unsigned i = 0; i < 256; ++i) idt_set_gate(i, arch_default_handler, 0);
    idt_set_gate(6, arch_invalid_opcode_handler, 0);
    idt_set_gate(13, arch_general_protection_handler, 0);
    idt_set_gate(14, arch_page_fault_handler, 0);
    for (unsigned i = 0; i < 16; ++i)
         idt_set_gate(0x20 + i, irq_handlers[i], 0);
    
    idt_set_gate(0x20, arch_timer_handler, 0);

    idt_set_gate(0x90, arch_yield_handler, 3);
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base = (uint64_t)(uintptr_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    pic_init();
    pit_init();
}

uintptr_t arch_thread_context_init(uintptr_t kernel_stack_top,
                                   thread_entry_t entry, void *argument,
                                   int user, uintptr_t user_stack)
{
    uint64_t *stack = (uint64_t *)(kernel_stack_top & ~(uintptr_t)0xf);
    uint64_t initial_rsp = (uint64_t)(uintptr_t)stack;

    *--stack = user ? 0x2b : 0x10;                      /* SS */
    *--stack = user ? user_stack : initial_rsp;         /* RSP */
    *--stack = 0x202;                                   /* RFLAGS */
    *--stack = user ? 0x33 : 0x18;                      /* CS */
    *--stack = user ? (uintptr_t)entry
                    : (uintptr_t)arch_thread_bootstrap; /* RIP */
    *--stack = 0;                                       /* rax */
    *--stack = 0;                                       /* rbx */
    *--stack = 0;                                       /* rcx */
    *--stack = 0;                                       /* rdx */
    *--stack = 0;                                       /* rbp */
    *--stack = 0;                                       /* rsi */
    *--stack = 0;                                       /* rdi */
    *--stack = 0;                                       /* r8 */
    *--stack = 0;                                       /* r9 */
    *--stack = 0;                                       /* r10 */
    *--stack = 0;                                       /* r11 */
    *--stack = user ? 0 : (uintptr_t)entry;             /* r12 */
    *--stack = user ? 0 : (uintptr_t)argument;          /* r13 */
    *--stack = 0;                                       /* r14 */
    *--stack = 0;                                       /* r15 */
    return (uintptr_t)stack;
}

void arch_scheduler_start(uintptr_t saved_context)
{
    critical_nesting = 0;
    interrupts_init();
    arch_start_first_thread(saved_context);
    arch_halt();
}

void arch_scheduler_yield(void)
{
    __asm__ volatile ("int $0x90" : : : "memory");
}

void arch_wait_for_interrupt(void)
{
    __asm__ volatile ("sti; hlt" : : : "memory");
}

uintptr_t arch_irq_save(void)
{
    uint64_t flags;

    __asm__ volatile ("pushfq; cli; popq %0" : "=r"(flags) : : "memory");
    return (uintptr_t)(flags & (1ULL << 9));
}

void arch_irq_restore(uintptr_t flags)
{
    if (flags & (1ULL << 9)) __asm__ volatile ("sti" : : : "memory");
}

void arch_interrupts_disable(void)
{
    __asm__ volatile ("cli" : : : "memory");
}

void arch_halt(void)
{
    for (;;) __asm__ volatile ("cli; hlt" : : : "memory");
}

void arch_critical_enter(void)
{
    uintptr_t flags = arch_irq_save();

    if (critical_nesting == 0) critical_outer_flags = flags;
    ++critical_nesting;
}

void arch_critical_exit(void)
{
    if (critical_nesting == 0) arch_halt();
    --critical_nesting;
    if (critical_nesting == 0) arch_irq_restore(critical_outer_flags);
}
