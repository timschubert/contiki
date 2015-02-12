#include "contiki.h"
#include "contiki-net.h"
#include "drivers/unique_id.h"

#if RIMEADDR_SIZE != 8
#error "RIME address size should be set to 8"
#endif /*RIMEADDR_SIZE == 8*/

void set_rime_addr()
{

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

}
