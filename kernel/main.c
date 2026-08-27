#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "memory.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_PHYS 0xb8000ULL
static volatile uint16_t *const vga = (volatile uint16_t *)(PHYSMAP_BASE + VGA_PHYS);
static uint8_t vga_x, vga_y;

static void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static void serial_init(void)
{
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
}
static void serial_putc(char c) { while ((inb(0x3fd) & 0x20) == 0) {} outb(0x3f8, (uint8_t)c); }
static void console_write(const char *s)
{
    while (*s) {
        char c = *s++;
        if (c == '\n') { serial_putc('\r'); serial_putc('\n'); vga_x = 0; if (++vga_y == VGA_HEIGHT) vga_y = 0; }
        else { serial_putc(c); vga[(size_t)vga_y * VGA_WIDTH + vga_x] = 0x0f00 | (uint8_t)c; if (++vga_x == VGA_WIDTH) { vga_x = 0; if (++vga_y == VGA_HEIGHT) vga_y = 0; } }
    }
}
static void hello_task(void *argument)
{
    (void)argument;
    for (;;) {
        console_write("hello world from FreeRTOS\n");
        for (volatile uint64_t delay = 0; delay < 10000000ULL; ++delay) {
            __asm__ volatile ("pause");
        }
    }
}
static void keepalive_task(void *argument)
{
    (void)argument;
    for (;;) { __asm__ volatile ("pause"); }
}
void vApplicationMallocFailedHook(void) { taskDISABLE_INTERRUPTS(); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; taskDISABLE_INTERRUPTS(); for (;;) {} }

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    (void)magic; (void)info;
    serial_init();
    console_write("SharkKernel x86_64\n");
    console_write("kernel virtual base: 0xffffffff80000000\n");
    console_write("physmap base:        0xffff800000000000\n");
    console_write("kernel heap base:    0xffffc00000000000\n");
    if (virt_to_phys(phys_to_virt(VGA_PHYS)) == VGA_PHYS) console_write("physmap translation: ok\n");
    memory_init();
    console_write("starting FreeRTOS...\n");
    xTaskCreate(hello_task, "hello", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(keepalive_task, "keepalive", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    console_write("scheduler failed\n");
    for (;;) {}
}
