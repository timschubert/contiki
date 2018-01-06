#include "rpl-eval-common.h"

void
rpl_eval_print_neighbors(void)
{
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  while(nbr != NULL) {
    printf("NEIGHBOR;");
    uip_debug_ipaddr_print(&nbr->ipaddr);
    printf(";%d;%d\n", nbr->isrouter, nbr->state);
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }
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
    printf("DEFAULT;");
    uip_debug_ipaddr_print(&defrt->ipaddr);
    printf(";%lu;%d\n", stimer_remaining(&defrt->lifetime), defrt->isinfinite);
  } else {
    printf("DEFAULT;0;0;0\n");
  }

  for(r = uip_ds6_route_head();
      r != NULL;
      r = uip_ds6_route_next(r)) {
    nexthop = uip_ds6_route_nexthop(r);
    printf("ROUTE;");
    uip_debug_ipaddr_print(&r->ipaddr);
    printf(";");
    uip_debug_ipaddr_print(nexthop);
    printf(";%lu;%u;%u\n", r->state.lifetime, r->state.dao_seqno_out, r->state.dao_seqno_in);
  }
}

void
rpl_eval_print_local_addresses(void)
{
  int i;
  uint8_t state;

  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      printf("ADDR;");
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      //printf("%hhu", uip_ds6_if.addr_list[i].ipaddr.u8[sizeof(server_ipaddr.u8) - 1]);
      printf("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
        uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
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

void
rpl_eval_rpl_print_neighbor_list(void)
{
  rpl_instance_t *default_instance = rpl_get_default_instance();

  if(default_instance != NULL && default_instance->current_dag != NULL &&
      default_instance->of != NULL) {
    int curr_dio_interval = default_instance->dio_intcurrent;
    int curr_rank = default_instance->current_dag->rank;
    rpl_parent_t *p = nbr_table_head(rpl_parents);

    // RPL;dio;<MOP>;<OCP>;<rank>;<dioint>;<nbr count>
    printf("DAG;%u;%u;%u;%u;%u\n",
        default_instance->mop, default_instance->of->ocp, curr_rank, curr_dio_interval, uip_ds6_nbr_num());
    while(p != NULL) {
      const struct link_stats *stats = rpl_get_parent_link_stats(p);
      // RPL;<parent>;<rank>;<parent metric>,<rank via parent>,<freshness>,<isfresh>,<preferred parent>,<last tx time>
      printf("PARENT;");
      uip_debug_ipaddr_print(rpl_get_parent_ipaddr(p));
      printf(";%u;%u;%u;%u;%d;%d;%u\n",
             p->rank,
             rpl_get_parent_link_metric(p),
             rpl_rank_via_parent(p),
             stats != NULL ? stats->freshness : 0,
             link_stats_is_fresh(stats),
             p == default_instance->current_dag->preferred_parent,
             (unsigned int) stats->last_tx_time
      );
      p = nbr_table_next(rpl_parents, p);
    }
  }
}

void
rpl_eval_print_status(void)
{
  rpl_eval_print_local_addresses();
  rpl_eval_print_neighbors();
  rpl_eval_print_routes();
  rpl_eval_rpl_print_neighbor_list();
  powertrace_print("");
}
