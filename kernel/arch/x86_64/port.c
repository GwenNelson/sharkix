#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define IDT_PRESENT 0x80
#define IDT_INTERRUPT_GATE 0x0e
#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA 0xa1
#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
typedef struct __attribute__((packed)) { uint16_t low, selector; uint8_t ist, attr; uint16_t mid; uint32_t high, reserved; } idt_entry_t;
typedef struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idtr_t;
static idt_entry_t idt[256] __attribute__((aligned(16)));
static uint64_t kernel_gdt[4] __attribute__((aligned(8))) = {
    0x0000000000000000ULL,
    0x00cf9a000000ffffULL,
    0x00cf92000000ffffULL,
    0x00af9a000000ffffULL
};
volatile UBaseType_t ulCriticalNesting = 0;
extern void vPortStartFirstTask(void);
extern void vPortTimerHandler(void);
extern void vPortYieldHandler(void);
extern void vPortDefaultHandler(void);
extern void vPortPageFaultHandler(void);
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
void vPortInstallKernelGDT(void)
{
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } gdtr = {
        (uint16_t)(sizeof(kernel_gdt) - 1), (uint64_t)(uintptr_t)kernel_gdt
    };
    __asm__ volatile ("lgdt %0" : : "m"(gdtr));
}
static void idt_set_gate(unsigned n, void (*handler)(void))
{
    uint64_t a = (uint64_t)(uintptr_t)handler;
    idt[n].low = (uint16_t)a; idt[n].selector = 0x18; idt[n].ist = 0; idt[n].attr = IDT_PRESENT | IDT_INTERRUPT_GATE;
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
    for (unsigned i = 0; i < 256; ++i) idt_set_gate(i, vPortDefaultHandler);
    idt_set_gate(0x20, vPortTimerHandler); idt_set_gate(0x21, vPortYieldHandler); idt_set_gate(14, vPortPageFaultHandler);
    idtr_t idtr = { (uint16_t)(sizeof(idt) - 1), (uint64_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr)); pic_init(); pit_init();
}
StackType_t *pxPortInitialiseStack(StackType_t *top, StackType_t *end,
                                   TaskFunction_t code, void *argument)
{
    (void)end;
    uint64_t *stack = (uint64_t *)((uintptr_t)top & ~(uintptr_t)portBYTE_ALIGNMENT_MASK);
    *--stack = 0x202; *--stack = 0x18; *--stack = (uint64_t)(uintptr_t)code;
    *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0;
    *--stack = (uint64_t)(uintptr_t)argument;
    *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0; *--stack = 0;
    return stack;
}
BaseType_t xPortStartScheduler(void) { ulCriticalNesting = 0; port_init_interrupts(); vPortStartFirstTask(); return 0; }
void vPortEndScheduler(void) { for (;;) __asm__ volatile ("cli; hlt"); }
void vPortEnterCritical(void) { portDISABLE_INTERRUPTS(); ++ulCriticalNesting; }
void vPortExitCritical(void) { if (ulCriticalNesting) --ulCriticalNesting; if (!ulCriticalNesting) portENABLE_INTERRUPTS(); }
uint32_t ulPortSetInterruptMask(void)
{
    uint64_t flags; __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory"); return (uint32_t)(flags & (1u << 9));
}
void vPortClearInterruptMask(uint32_t value) { if (value) portENABLE_INTERRUPTS(); }
