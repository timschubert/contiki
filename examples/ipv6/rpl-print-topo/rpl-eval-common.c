#include "rpl-eval-common.h"

void
rpl_eval_print_neighbors(void)
{
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  printf("NEIGHBORS");
  while(nbr != NULL) {
    printf(";%x,%d,%d", nbr->ipaddr.u8[sizeof(nbr->ipaddr.u8) - 1], nbr->isrouter, nbr->state);
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }
  printf("\n");
}

void
rpl_eval_print_routes(void)
{
  uip_ds6_route_t *r;
  uip_ipaddr_t *nexthop;
  uip_ds6_defrt_t *defrt;
  uip_ipaddr_t *ipaddr;
  defrt = NULL;
  if((ipaddr = uip_ds6_defrt_choose()) != NULL) {
    defrt = uip_ds6_defrt_lookup(ipaddr);
  }
  if(defrt != NULL) {
    printf("DefRT: :: -> %02d", defrt->ipaddr.u8[15]);
    printf(" lt:%lu inf:%d\n", stimer_remaining(&defrt->lifetime),
           defrt->isinfinite);
  } else {
    printf("DefRT: :: -> NULL\n");
  }

  for(r = uip_ds6_route_head();
      r != NULL;
      r = uip_ds6_route_next(r)) {
    nexthop = uip_ds6_route_nexthop(r);
    printf("Route: %02d -> %02d", r->ipaddr.u8[15], nexthop->u8[15]);
    /* PRINT6ADDR(&r->ipaddr); */
    /* PRINTF(" -> "); */
    /* PRINT6ADDR(nexthop); */
    printf(" lt:%lu\n", r->state.lifetime);
  }
}

void
rpl_eval_print_local_addresses(void)
{
  int i;
  uint8_t state;

  printf("Client IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      printf("\n");
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
        uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
  printf("\n");
}

void
rpl_eval_set_global_address(void)
{
  uip_ipaddr_t ipaddr;

  uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);
}

void
rpl_eval_set_server_address(uip_ipaddr_t *server_ipaddr)
{
/* The choice of server address determines its 6LoWPAN header compression.
 * (Our address will be compressed Mode 3 since it is derived from our
 * link-local address)
 * Obviously the choice made here must also be selected in udp-server.c.
 *
 * For correct Wireshark decoding using a sniffer, add the /64 prefix to the
 * 6LowPAN protocol preferences,
 * e.g. set Context 0 to fd00::. At present Wireshark copies Context/128 and
 * then overwrites it.
 * (Setting Context 0 to fd00::1111:2222:3333:4444 will report a 16 bit
 * compressed address of fd00::1111:22ff:fe33:xxxx)
 *
 * Note the IPCMV6 checksum verification depends on the correct uncompressed
 * addresses.
 */

#if 0
/* Mode 1 - 64 bits inline */
  uip_ip6addr(server_ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 1);
#elif 1
/* Mode 2 - 16 bits inline */
  uip_ip6addr(server_ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0x00ff, 0xfe00, 1);
#else
/* Mode 3 - derived from server link-local (MAC) address */
  uip_ip6addr(server_ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0x0250, 0xc2ff, 0xfea8, 0xcd1a); //redbee-econotag
#endif
}
