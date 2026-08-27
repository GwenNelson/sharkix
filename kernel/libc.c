#include <stddef.h>

void *memset( void *dst, int c, size_t n )
{
    unsigned char *p = ( unsigned char * ) dst;
    while( n-- != 0 )
    {
        *p++ = ( unsigned char ) c;
    }
    return dst;
}

void *memcpy( void *dst, const void *src, size_t n )
{
    unsigned char *d = ( unsigned char * ) dst;
    const unsigned char *s = ( const unsigned char * ) src;
    while( n-- != 0 )
    {
        *d++ = *s++;
    }
    return dst;
}

size_t strlen( const char *s )
{
    size_t n = 0;
    while( s[ n ] != '\0' )
    {
        ++n;
    }
    return n;
}
