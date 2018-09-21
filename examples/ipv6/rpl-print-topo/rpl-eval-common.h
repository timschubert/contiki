#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-udp-packet.h"
#include "sys/ctimer.h"
#include <stdio.h>
#include <string.h>

#include "dev/serial-line.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/ip/uip-debug.h"
#include "net/rpl/rpl.h"
#include "net/link-stats.h"

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define UDP_EXAMPLE_ID  190

#ifndef PERIOD
#define PERIOD 1
#endif

#define START_INTERVAL		(15 * CLOCK_SECOND)
#define SEND_INTERVAL		(CLOCK_SECOND / 2)
#define SEND_TIME		(random_rand() % SEND_INTERVAL)

void rpl_eval_tcpip_handler(void);
void rpl_eval_print_neighbors(void);
void rpl_eval_print_routes(void);
void rpl_eval_print_local_addresses(void);
void rpl_eval_set_global_address(void);
void rpl_eval_set_server_address(uip_ipaddr_t *server_ipaddr);
void rpl_eval_print_status(void);
