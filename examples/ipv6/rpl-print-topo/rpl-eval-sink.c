#include "rpl-eval-common.h"

PROCESS(rpl_eval_sink, "RPL eval process");
AUTOSTART_PROCESSES(&rpl_eval_sink);

static uint16_t reply;
static uint16_t seq_id;
static struct uip_udp_conn *server_conn;

static void
tcpip_handler(void)
{
  char *str;

  if(uip_newdata()) {
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    reply++;
    printf("DATA recv '%s' (s:%d, r:%d)\n", str, seq_id, reply);
  } else {
    printf("DATA recv <empty>\n");
  }
}

static void
init_dag(uip_ipaddr_t *ipaddr)
{
  struct uip_ds6_addr *root_if = uip_ds6_addr_lookup(ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;
    dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)ipaddr);
    uip_ip6addr(ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
    rpl_set_prefix(dag, ipaddr, 64);
    PRINTF("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }
}

PROCESS_THREAD(rpl_eval_sink, ev, data)
{
  static struct etimer periodic;
  static uip_ipaddr_t server_addr;
#if WITH_COMPOWER
  static int print = 0;
#endif

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  NETSTACK_RADIO.set_value(RADIO_PARAM_CHANNEL, RF_CHANNEL);

  rpl_init();

  rpl_eval_set_global_address();
  rpl_eval_set_server_address(&server_addr);
  uip_ds6_addr_add(&server_addr, 0, ADDR_MANUAL);

  init_dag(&server_addr);

  rpl_eval_print_local_addresses();

  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);
  if(server_conn == NULL) {
    PRINTF("No UDP connection available, exiting the process!\n");
    PROCESS_EXIT();
  }
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  printf("Created a server connection with remote address ");
  PRINT6ADDR(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));

  printf("RPL print process started nbr:%d routes:%d\n",
         NBR_TABLE_CONF_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

#if WITH_COMPOWER
  powertrace_sniff(POWERTRACE_ON);
#endif

  etimer_set(&periodic, SEND_INTERVAL);
  while(1) {
    PROCESS_YIELD();

    if(ev == tcpip_event) {
      tcpip_handler();
    }

    if(ev == serial_line_event_message && data != NULL) {
      char *str;
      str = data;
      if(str[0] == 'r') {
        // TODO switch to hardened version
      }
    }

    if(etimer_expired(&periodic)) {
      etimer_reset(&periodic);
      rpl_eval_print_neighbors();
      rpl_eval_print_routes();
      rpl_print_neighbor_list();

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
