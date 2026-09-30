#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sharkix/libsharkix/syscalls.h>

#define TEST_ARRAY_SIZE (16 * 1024)

uint8_t test_array[TEST_ARRAY_SIZE];

static uint64_t data_value = 0x123456789abcdef0ULL;
static const uint64_t rodata_value = 0xfedcba9876543210ULL;

#define TEST(fn, str) do {                       \
	sharkix_debug_puts("Testing " str ": "); \
	if(fn())                                  \
		sharkix_debug_puts("PASS\n");      \
	else                                      \
		sharkix_debug_puts("FAIL\n");      \
} while(0)

bool test_global_array(void) {
	for(size_t i = 0; i < TEST_ARRAY_SIZE; i++) {
		if(test_array[i] != 0)
			return false;
	}

	return true;
}

bool test_rodata(void) {
	volatile const uint64_t *value = &rodata_value;

	return *value == 0xfedcba9876543210ULL;
}

bool test_data(void) {
	volatile uint64_t *value = &data_value;

	return *value == 0x123456789abcdef0ULL;
}

bool test_data_write(void) {
	volatile uint64_t *value = &data_value;

	*value = 0x1122334455667788ULL;

	return *value == 0x1122334455667788ULL;
}

bool test_bss_write(void) {
	test_array[0] = 0x12;
	test_array[TEST_ARRAY_SIZE / 2] = 0x34;
	test_array[TEST_ARRAY_SIZE - 1] = 0x56;

	if(test_array[0] != 0x12)
		return false;

	if(test_array[TEST_ARRAY_SIZE / 2] != 0x34)
		return false;

	if(test_array[TEST_ARRAY_SIZE - 1] != 0x56)
		return false;

	return true;
}

void _start(void) {
	sharkix_debug_puts("\n\n\n\n");
	sharkix_debug_puts("Hello from payload!\n\n");

	TEST(test_global_array, "global uninitialized array is all 0");
	TEST(test_rodata,      ".rodata value is correctly initialized");
	TEST(test_data,        ".data value is correctly initialized");
	TEST(test_data_write,  ".data is writable");
	TEST(test_bss_write,   ".bss is writable");

	sharkix_debug_puts("\nAbout to attempt write to .rodata...\n");

	volatile uint64_t *evil =
	    (volatile uint64_t *)(uintptr_t)&rodata_value;

	*evil = 0xdeadbeefdeadbeefULL;

	sharkix_debug_puts(
	    "FAIL: if you can see this output, .rodata is not readonly!\n");

	for(;;)
		__asm__ volatile ("pause");
}
