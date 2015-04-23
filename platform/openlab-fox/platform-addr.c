#include "contiki.h"
#include "contiki-net.h"
#include "drivers/unique_id.h"

void set_rime_addr()
{

#if RIMEADDR_SIZE == 8

    /* Company 3 Bytes */
    rimeaddr_node_addr.u8[0] = 0x01;
    rimeaddr_node_addr.u8[1] = 0x23;
    rimeaddr_node_addr.u8[2] = 0x45;

    /* Platform identifier */
    rimeaddr_node_addr.u8[3] = 0x00;

    /* Generate 4 remaining bytes using uid of processor */
    int i;
    for (i = 0; i < 4; i++)
        rimeaddr_node_addr.u8[i+4] = uid->uid8[i+6];
#else
    // Adapt something from the 8 byte addr, not tested
    int i;
    rimeaddr_node_addr.u8[0] = 0;
    rimeaddr_node_addr.u8[1] = 0;
    for (i = 0; i < 4;) {
        rimeaddr_node_addr.u8[0] ^= uid->uid8[6 + i++];
        rimeaddr_node_addr.u8[1] ^= uid->uid8[6 + i++];
    }
#endif
}
