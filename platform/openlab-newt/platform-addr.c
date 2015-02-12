#include "contiki.h"
#include "contiki-net.h"
#include "drivers/unique_id.h"

#if RIMEADDR_SIZE != 8
#error "RIME address size should be set to 8"
#endif /*RIMEADDR_SIZE == 8*/

void set_rime_addr()
{

    /* Company 3 Bytes */
    rimeaddr_node_addr.u8[0] = 0xBA;
    rimeaddr_node_addr.u8[1] = 0xDB;
    rimeaddr_node_addr.u8[2] = 0x0B;
    /* Product Type 1 Byte */
    rimeaddr_node_addr.u8[3] = PLATFORM_TYPE;
    /* Product Version 1 Byte */
    rimeaddr_node_addr.u8[4] = PLATFORM_VERSION;
    /* Serial Number 3 Bytes */
    rimeaddr_node_addr.u8[5] = uid->uid8[9];
    rimeaddr_node_addr.u8[6] = uid->uid8[10];
    rimeaddr_node_addr.u8[7] = uid->uid8[11];

}
