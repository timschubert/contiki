#include "web_service.h"
#include "contiki-net.h"
#include <stdio.h>
#include <string.h> // strlen
#define DEBUG 1
#include "net/uip-debug.h"

/*---------------------------------------------------------------------------*/
void web_service_send_data(uip_ipaddr_t *dest_addr, const char *message)
{
  printf("not implemented yet\n");
}
/*---------------------------------------------------------------------------*/
void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Node's IPv6 addresses:\n");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused && (state == ADDR_TENTATIVE || state
                                          == ADDR_PREFERRED)) {
      PRINTF("  ");
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      if(state == ADDR_TENTATIVE) {
        uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
