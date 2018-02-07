#include "rpl-eval-common.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

PROCESS(rpl_eval_sink, "RPL eval process");
AUTOSTART_PROCESSES(&rpl_eval_sink);

static uint16_t seq_id;
static struct uip_udp_conn *server_conn;

static void
tcpip_handler(void)
{
  char *str;

  if(uip_newdata()) {
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    seq_id++;
    printf("DATA;recv;");
    uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    printf(";%s\n", str);
  }
}

static void
init_dag(uip_ipaddr_t *ipaddr)
{
  struct uip_ds6_addr *root_if = uip_ds6_addr_lookup(ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)ipaddr);
    if (dag) {
      uip_ip6addr(ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
      rpl_set_prefix(dag, ipaddr, 64);
      printf("created a new RPL dag\n");
    } else {
      printf("failed to create a new RPL DAG\n");
    }
    //PRINTF("created a new RPL dag\n");
  } else {
    //PRINTF("failed to create a new RPL DAG\n");
    printf("Failed to look up root interface address\n");
  }
}

PROCESS_THREAD(rpl_eval_sink, ev, data)
{
  static struct etimer periodic;
  static uip_ipaddr_t server_addr;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  NETSTACK_RADIO.set_value(RADIO_PARAM_CHANNEL, RF_CHANNEL);

  //rpl_init();

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

  PRINTF("Created a server connection with remote address ");
  PRINT6ADDR(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));

  PRINTF("RPL print process started nbr:%d routes:%d\n",
         NBR_TABLE_CONF_MAX_NEIGHBORS, UIP_CONF_MAX_ROUTES);

  //powertrace_sniff(POWERTRACE_ON);
  //powertrace_start(CLOCK_SECOND * PERIOD);

  rpl_eval_print_local_addresses();

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
      }
    }

    if(etimer_expired(&periodic)) {
      etimer_reset(&periodic);

      rpl_eval_print_status();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
