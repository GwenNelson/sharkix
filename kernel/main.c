#include <stdint.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile uint16_t *const vga = ( volatile uint16_t * ) 0xB8000;
static uint8_t vga_x = 0;
static uint8_t vga_y = 0;
static const uint16_t vga_attr = 0x0F00;

static void outb( uint16_t port, uint8_t value )
{
    __asm__ volatile ( "outb %0, %1" : : "a"( value ), "Nd"( port ) );
}

static uint8_t inb( uint16_t port )
{
    uint8_t value;
    __asm__ volatile ( "inb %1, %0" : "=a"( value ) : "Nd"( port ) );
    return value;
}

static void serial_write_char( char c )
{
    while( ( inb( 0x3F8 + 5 ) & 0x20 ) == 0 )
    {
    }
    outb( 0x3F8, ( uint8_t ) c );
}

static void serial_write_string( const char *s )
{
    while( *s != '\0' )
    {
        if( *s == '\n' )
        {
            serial_write_char( '\r' );
        }
        serial_write_char( *s++ );
    }
}

static void serial_init( void )
{
    outb( 0x3F8 + 1, 0x00 );
    outb( 0x3F8 + 3, 0x80 );
    outb( 0x3F8 + 0, 0x03 );
    outb( 0x3F8 + 1, 0x00 );
    outb( 0x3F8 + 3, 0x03 );
    outb( 0x3F8 + 2, 0xC7 );
    outb( 0x3F8 + 4, 0x0B );
}

static void vga_putc( char c )
{
    if( c == '\n' )
    {
        vga_x = 0;
        if( ++vga_y >= VGA_HEIGHT )
        {
            vga_y = 0;
        }
        return;
    }

    vga[ ( size_t ) vga_y * VGA_WIDTH + vga_x ] = vga_attr | ( uint8_t ) c;
    if( ++vga_x >= VGA_WIDTH )
    {
        vga_x = 0;
        if( ++vga_y >= VGA_HEIGHT )
        {
            vga_y = 0;
        }
    }
}

static void vga_write_string( const char *s )
{
    while( *s != '\0' )
    {
        vga_putc( *s++ );
    }
}

static void console_write( const char *s )
{
    vga_write_string( s );
    serial_write_string( s );
}

static void blink_task( void *arg )
{
    ( void ) arg;
    for( ;; )
    {
        console_write( "hello world from FreeRTOS\n" );
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}

void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;
    taskDISABLE_INTERRUPTS();
    for( ;; )
    {
    }
}

void kernel_main( uint32_t magic, uint32_t info )
{
    ( void ) magic;
    ( void ) info;

    serial_init();
    console_write( "booting kernel\n" );

    xTaskCreate( blink_task, "blink", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();

    console_write( "scheduler failed\n" );
    for( ;; )
    {
    }
}
