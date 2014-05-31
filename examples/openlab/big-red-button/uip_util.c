#include "uip_util.h"

#include "contiki-net.h"
#include "uiplib.h"
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
int uip_util_text2addr(const char *src, uip_ipaddr_t *dest, int *port)
{
	if (!uiplib_ipaddrconv(src, dest)) return 0;
	if (!(src = rindex(src, ']'))) return 1;
	if (*(++src) != ':') return 0;
	if (!atoi(src)) return 0;
	if (port == NULL) return 1;
	*port = atoi(src);
	return 1;
}
/*---------------------------------------------------------------------------*/
int uip_util_addr2text(uip_ipaddr_t *addr, char *buf)
{
  char *orig = buf;
  uint16_t a;
  int i, f;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) buf += sprintf(buf, "::");
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        buf += sprintf(buf, ":");
      }
      buf += sprintf(buf, "%x", a);
    }
  }
  return buf - orig;
}
/*---------------------------------------------------------------------------*/
#define DEBUG 1
#include "net/uip-debug.h"
void
uip_util_print_local_addresses(void)
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
