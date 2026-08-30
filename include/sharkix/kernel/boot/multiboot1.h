#ifndef SHARKIX_BOOT_MULTIBOOT1_H
#define SHARKIX_BOOT_MULTIBOOT1_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2badb002U

#define MB_INFO_MEMORY        (1U << 0)
#define MB_INFO_BOOT_DEVICE   (1U << 1)
#define MB_INFO_CMDLINE       (1U << 2)
#define MB_INFO_MODS          (1U << 3)
#define MB_INFO_AOUT_SYMS     (1U << 4)
#define MB_INFO_ELF_SHDR      (1U << 5)
#define MB_INFO_MMAP          (1U << 6)
#define MB_INFO_DRIVES        (1U << 7)
#define MB_INFO_CONFIG_TABLE  (1U << 8)
#define MB_INFO_BOOT_LOADER   (1U << 9)
#define MB_INFO_APM_TABLE     (1U << 10)
#define MB_INFO_VBE           (1U << 11)

#define MULTIBOOT_MEMORY_AVAILABLE              1U
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3U
#define MULTIBOOT_MEMORY_NVS                    4U
#define MULTIBOOT_MEMORY_BADRAM                 5U

typedef struct multiboot_module {
    uint32_t start;
    uint32_t end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} __attribute__((packed)) multiboot_info_t;

typedef struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

#endif
