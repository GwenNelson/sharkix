/*
 * Sharkix capability ABI definitions.
 *
 * This file is deliberately composed only of SHARKIX_CAP_* declarations.
 * Consumers define those macros appropriately before including this file.
 *
 * Generic rights:       bits 0-6
 * Reserved rights:      bits 7-9
 * Type-specific rights: bits 10+
 *
 * Type-specific right bit numbers may overlap between different cap types.
 */


/* Generic rights */

SHARKIX_CAP_GENERIC_RIGHT(TRANSFER, 0) /* can transfer this cap to another task                            */
SHARKIX_CAP_GENERIC_RIGHT(FORWARD,  1) /* can forward this cap to another task or use it locally, not both */
SHARKIX_CAP_GENERIC_RIGHT(DERIVE,   2) /* can derive another cap from this cap                             */
SHARKIX_CAP_GENERIC_RIGHT(DESTROY,  3) /* can destroy the underlying object                                */
SHARKIX_CAP_GENERIC_RIGHT(REMOVE,   4) /* can remove the cap - this removes it globally                    */
SHARKIX_CAP_GENERIC_RIGHT(GETNAME,  5) /* can get the ASCII name of the cap                                */
SHARKIX_CAP_GENERIC_RIGHT(SETNAME,  6) /* can set the ASCII name of the cap                                */


/* Reserved generic-right space */

SHARKIX_CAP_RESERVED_RIGHT(7)
SHARKIX_CAP_RESERVED_RIGHT(8)
SHARKIX_CAP_RESERVED_RIGHT(9)


/* IPC endpoint */

SHARKIX_CAP_TYPE_BEGIN(IPC, 1)

SHARKIX_CAP_RIGHT(IPC, SEND,      10)
SHARKIX_CAP_RIGHT(IPC, RECV,      11)
SHARKIX_CAP_RIGHT(IPC, PUBLISH,   12)
SHARKIX_CAP_RIGHT(IPC, SUBSCRIBE, 13)

SHARKIX_CAP_TYPE_END(IPC)


/* Physical memory */

SHARKIX_CAP_TYPE_BEGIN(PMEM, 2)

SHARKIX_CAP_RIGHT(PMEM, MAP,    10)
SHARKIX_CAP_RIGHT(PMEM, READ,   11)
SHARKIX_CAP_RIGHT(PMEM, WRITE,  12)
SHARKIX_CAP_RIGHT(PMEM, EXEC,   13)
SHARKIX_CAP_RIGHT(PMEM, DERIVE, 14)
SHARKIX_CAP_RIGHT(PMEM, MERGE,  15)

SHARKIX_CAP_TYPE_END(PMEM)


/* Virtual memory object */

SHARKIX_CAP_TYPE_BEGIN(VMO, 3)

SHARKIX_CAP_RIGHT(VMO, MAP,    10)
SHARKIX_CAP_RIGHT(VMO, READ,   11)
SHARKIX_CAP_RIGHT(VMO, WRITE,  12)
SHARKIX_CAP_RIGHT(VMO, EXEC,   13)
SHARKIX_CAP_RIGHT(VMO, GETLEN, 14)

SHARKIX_CAP_TYPE_END(VMO)


/* I/O ports */

SHARKIX_CAP_TYPE_BEGIN(PORTIO, 4)

SHARKIX_CAP_RIGHT(PORTIO, READ,  10)
SHARKIX_CAP_RIGHT(PORTIO, WRITE, 11)

SHARKIX_CAP_TYPE_END(PORTIO)


/* IRQ */

SHARKIX_CAP_TYPE_BEGIN(IRQ, 5)

SHARKIX_CAP_RIGHT(IRQ, WAIT, 10)
SHARKIX_CAP_RIGHT(IRQ, ACK,  11)

SHARKIX_CAP_TYPE_END(IRQ)


/* Notification */

SHARKIX_CAP_TYPE_BEGIN(NOTIFY, 6)

SHARKIX_CAP_RIGHT(NOTIFY, WAIT,   10)
SHARKIX_CAP_RIGHT(NOTIFY, SIGNAL, 11)
SHARKIX_CAP_RIGHT(NOTIFY, ACK,    12)

SHARKIX_CAP_TYPE_END(NOTIFY)


/* Address space */

SHARKIX_CAP_TYPE_BEGIN(AS, 7)

SHARKIX_CAP_RIGHT(AS, MAP,     10)
SHARKIX_CAP_RIGHT(AS, UNMAP,   11)
SHARKIX_CAP_RIGHT(AS, PROTECT, 12)

SHARKIX_CAP_TYPE_END(AS)


/*
 * Factories
 *
 * Factories currently have no type-specific rights. Possession of the
 * appropriately typed factory cap is the creation authority.
 */

SHARKIX_CAP_TYPE_BEGIN(FACTORY_VMO, 8)
SHARKIX_CAP_TYPE_END(FACTORY_VMO)

SHARKIX_CAP_TYPE_BEGIN(FACTORY_AS, 9)
SHARKIX_CAP_TYPE_END(FACTORY_AS)

SHARKIX_CAP_TYPE_BEGIN(FACTORY_THREAD, 10)
SHARKIX_CAP_TYPE_END(FACTORY_THREAD)
