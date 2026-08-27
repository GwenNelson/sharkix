#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "memory.h"
#include "task_loader.h"

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
static void console_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[19] = "0x0000000000000000";
    for (unsigned i = 0; i < 16; ++i) text[17 - i] = digits[(value >> (i * 4)) & 0xf];
    console_write(text);
}
static void print_task_mapping(const char *name, const isolated_task_t *task)
{
    console_write(name); console_write(" CR3: "); console_hex(task->address_space->pml4_phys);
    console_write(" code phys: "); console_hex(task->code_physical); console_write("\n");
}
static void kernel_task(void *argument)
{
    (void)argument;
    for (;;) {
        uint64_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        if (cr3 != kernel_address_space.pml4_phys) serial_putc('!');
        serial_putc('K');
        taskYIELD();
    }
}
void vApplicationMallocFailedHook(void) { taskDISABLE_INTERRUPTS(); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; taskDISABLE_INTERRUPTS(); for (;;) {} }

void kernel_high_entry(uint32_t magic, uint32_t info)
{
    extern const uint8_t taskA_image_start[], taskA_image_end[];
    extern const uint8_t taskB_image_start[], taskB_image_end[];
    isolated_task_t task_a, task_b;
    (void)magic; (void)info;
    serial_init();
    console_write("SharkKernel x86_64\n");
    console_write("kernel virtual base: 0xffffffff80000000\n");
    console_write("physmap base:        0xffff800000000000\n");
    console_write("kernel heap base:    0xffffc00000000000\n");
    if (virt_to_phys(phys_to_virt(VGA_PHYS)) == VGA_PHYS) console_write("physmap translation: ok\n");
    memory_init();
    if (isolated_task_create(taskA_image_start, (size_t)(taskA_image_end - taskA_image_start), "taskA", &task_a) != 0 ||
        isolated_task_create(taskB_image_start, (size_t)(taskB_image_end - taskB_image_start), "taskB", &task_b) != 0) {
        console_write("isolated task setup failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    console_write("kernel CR3: "); console_hex(kernel_address_space.pml4_phys); console_write("\n");
    print_task_mapping("task A", &task_a);
    print_task_mapping("task B", &task_b);
    if (task_a.address_space->pml4_phys == task_b.address_space->pml4_phys ||
        task_a.code_physical == task_b.code_physical ||
        address_space_translate(task_a.address_space, 0x400000) != task_a.code_physical ||
        address_space_translate(task_b.address_space, 0x400000) != task_b.code_physical) {
        console_write("isolated mapping check failed\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    console_write("isolated lower mappings: ok\n");
    console_write("starting FreeRTOS...\n");
    xTaskCreate(kernel_task, "kernel", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();
    console_write("scheduler failed\n");
    for (;;) {}
}
