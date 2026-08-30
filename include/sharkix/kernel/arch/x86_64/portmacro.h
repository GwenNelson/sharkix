#ifndef PORTMACRO_H
#define PORTMACRO_H
#include <stdint.h>
#define portCHAR char
#define portFLOAT float
#define portDOUBLE double
#define portLONG long
#define portSHORT short
#define portSTACK_TYPE uint64_t
#define portBASE_TYPE long
typedef uint64_t StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint64_t TickType_t;
#define portMAX_DELAY ((TickType_t)~0ULL)
#define portSTACK_GROWTH (-1)
#define portTICK_PERIOD_MS ((TickType_t)1000 / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT 16
#define portHAS_STACK_OVERFLOW_CHECKING 1
#define portPOINTER_SIZE_TYPE uintptr_t
#define portYIELD() __asm__ volatile ("int $0x21" ::: "memory")
#define portYIELD_FROM_ISR(x) do { if ((x) != pdFALSE) portYIELD(); } while (0)
#define portEND_SWITCHING_ISR(x) portYIELD_FROM_ISR(x)
#define portDISABLE_INTERRUPTS() __asm__ volatile ("cli" ::: "memory")
#define portENABLE_INTERRUPTS() __asm__ volatile ("sti" ::: "memory")
#define portENTER_CRITICAL() vPortEnterCritical()
#define portEXIT_CRITICAL() vPortExitCritical()
#define portSET_INTERRUPT_MASK_FROM_ISR() ulPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x) vPortClearInterruptMask(x)
#define portTASK_FUNCTION_PROTO(vFunction, pvParameters) void vFunction(void *pvParameters)
#define portTASK_FUNCTION(vFunction, pvParameters) void vFunction(void *pvParameters)
#define portNOP() __asm__ volatile ("nop")
extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);
extern uint32_t ulPortSetInterruptMask(void);
extern void vPortClearInterruptMask(uint32_t value);
#define portNUM_VECTORS 256
#endif
