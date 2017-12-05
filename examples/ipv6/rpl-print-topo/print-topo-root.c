/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-udp-packet.h"
#include "sys/ctimer.h"
#ifdef WITH_COMPOWER
#include "powertrace.h"
#endif
#include <stdio.h>
#include <string.h>

#include "dev/serial-line.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/ip/uip-debug.h"
#include "net/rpl/rpl.h"

#ifndef PERIOD
#define PERIOD 2
#endif

#define START_INTERVAL		(15 * CLOCK_SECOND)
#define SEND_INTERVAL		(PERIOD * CLOCK_SECOND)
#define SEND_TIME		(SEND_INTERVAL)

PROCESS(rpl_print_process, "RPL print process");
AUTOSTART_PROCESSES(&rpl_print_process);
/*---------------------------------------------------------------------------*/
static void
print_neighbors()
{
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  printf("NEIGHBORS");
  while(nbr != NULL) {
    printf(";%x,%d,%d", nbr->ipaddr.u8[sizeof(nbr->ipaddr.u8) - 1], nbr->isrouter, nbr->state);
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }
  printf("\n");
}
/*---------------------------------------------------------------------------*/
static void
print_routes()
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
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
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
/*---------------------------------------------------------------------------*/
static void
print_state(void *v)
{
  print_neighbors();
  print_routes();
  rpl_print_neighbor_list();
}
/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ipaddr_t ipaddr;
  struct uip_ds6_addr *root_if;

  uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  root_if = uip_ds6_addr_lookup(&ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;
    dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)&ipaddr);
    uip_ip6addr(&ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
    rpl_set_prefix(dag, &ipaddr, 64);
    PRINTF("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rpl_print_process, ev, data)
{
  static struct etimer periodic;
  static struct ctimer backoff_timer;
#if WITH_COMPOWER
  static int print = 0;
#endif

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  NETSTACK_RADIO.set_value(RADIO_PARAM_CHANNEL, 16);

  rpl_init();

  set_global_address();

  print_local_addresses();

  printf("RPL print process started nbr:%d routes:%d\n",
         NBR_TABLE_CONF_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

  printf("RPL_CONF_STATS is %d\n", RPL_CONF_STATS);
#if WITH_COMPOWER
  powertrace_sniff(POWERTRACE_ON);
#endif

  etimer_set(&periodic, SEND_INTERVAL);
  while(1) {
    PROCESS_YIELD();

    if(ev == serial_line_event_message && data != NULL) {
      char *str;
      str = data;
      if(str[0] == 'r') {
        // TODO switch to hardened version
      }
    }

    if(etimer_expired(&periodic)) {
      etimer_reset(&periodic);
      ctimer_set(&backoff_timer, SEND_TIME, print_state, NULL);

#if WITH_COMPOWER
      if (print == 0) {
	powertrace_print("#P");
      }
      if (++print == 3) {
	print = 0;
      }
#endif

    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
