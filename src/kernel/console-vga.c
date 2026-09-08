#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "console-vga.h"
#include "memory.h"
#include "pmem.h"
#include "sync.h"
#include "kvalloc.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_PHYS 0xb8000ULL
#define VGA_ATTRIBUTE 0x0f00U
#define VGA_CRTC_INDEX 0x3d4
#define VGA_CRTC_DATA 0x3d5
//static volatile uint16_t *const vga = (volatile uint16_t *)(PHYSMAP_BASE + VGA_PHYS);
static volatile uint16_t *vga = NULL;

static uintptr_t vga_va;
static uint8_t vga_x, vga_y;
static pmem_handle_t vga_pmem = PMEM_INVALID_HANDLE;
static bool vga_ready;

static void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }

static uint16_t vga_cursor_position(void)
{
    outb(VGA_CRTC_INDEX, 0x0e);
    uint16_t position = (uint16_t)inb(VGA_CRTC_DATA) << 8;
    outb(VGA_CRTC_INDEX, 0x0f);
    return position | inb(VGA_CRTC_DATA);
}

static void vga_set_cursor(void)
{
    uint16_t position = (uint16_t)vga_y * VGA_WIDTH + vga_x;
    outb(VGA_CRTC_INDEX, 0x0e);
    outb(VGA_CRTC_DATA, (uint8_t)(position >> 8));
    outb(VGA_CRTC_INDEX, 0x0f);
    outb(VGA_CRTC_DATA, (uint8_t)position);
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
     
     // allocate a virtual address space for it
     if (kvalloc(0x8000, &vga_va) != 0) {
        console_write("console-vga.c:console_vga_init() - failed kvalloc() of VRAM!\n");
        return;
     }

     // map it into kernel space (later on this will be userspace)
     // also, eventually we should probably actually grab it from the pmem_t above
     if (address_space_map_range(address_space_kernel(),
                            vga_va,
                            0xB8000,
                            0x8000,
                            PAGE_WRITABLE | PAGE_NX) != 0) {
				     kvfree(vga_va);
				     console_write("console-vga.c:console_vga_init() - failed to map VRAM!\n");
				     return;
			    }

     // and if we get here, we should be able to talk to VRAM!
     vga = (uint16_t*)vga_va;

     // now setup the cursor stuff and other nonsense
     uint16_t position = vga_cursor_position();
     if (position >= VGA_WIDTH * VGA_HEIGHT) position = 0;
     vga_x = (uint8_t)(position % VGA_WIDTH);
     vga_y = (uint8_t)(position / VGA_WIDTH);
     vga_ready = true;
}

bool console_vga_isready(void)
{
    return vga_ready;
}

void console_vga_putc(char c)
{
    kcritical_enter();
    if (c == '\n') {
        vga_x = 0;
        ++vga_y;
        vga_scroll_if_needed();
        vga_set_cursor();
        kcritical_exit();
        return;
    }
    vga[(size_t)vga_y * VGA_WIDTH + vga_x] = VGA_ATTRIBUTE | (uint8_t)c;
    if (++vga_x == VGA_WIDTH) {
        vga_x = 0;
        ++vga_y;
        vga_scroll_if_needed();
    }
    vga_set_cursor();
    kcritical_exit();
}
