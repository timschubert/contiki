#include "web_service.h"
#include "contiki-net.h"
#include <stdio.h>
#include <string.h> // strlen
#define DEBUG 1
#include "net/uip-debug.h"

static struct {
  struct psock ps;
  char buffer[100];
  uip_ipaddr_t dest_addr;
  int port;
} glob;

#define log_trace(...)
#define log_error(...) // printf(__VA_ARGS__)

PROCESS(psock_client_process, "protosocket client");
/*---------------------------------------------------------------------------*/
void web_service_send_data(uip_ipaddr_t *dest_addr, const char *message)
{
  glob.dest_addr = *dest_addr;
  glob.port = 80;
  strncpy(glob.buffer, message, sizeof(glob.buffer));
  glob.buffer[sizeof(glob.buffer)-1] = 0;
  process_start(&psock_client_process, NULL);
}
/*---------------------------------------------------------------------------*/
static char format_str_buf[32];
#define format_str(...) (sprintf(format_str_buf, __VA_ARGS__), format_str_buf)
/*---------------------------------------------------------------------------*/
static int
handle_connection(struct psock *p)
{
  PSOCK_BEGIN(p);

  PSOCK_SEND_STR(p, "POST / HTTP/1.0\r\n");
  PSOCK_SEND_STR(p, "User-Agent: Contiki/2.7 protosocket client\r\n");
  PSOCK_SEND_STR(p, "Content-Type: text/plain\r\n");
  PSOCK_SEND_STR(p, "Content-Length: ");
  PSOCK_SEND_STR(p, format_str("%d\r\n", strlen(glob.buffer)));
  PSOCK_SEND_STR(p, "\r\n");
  PSOCK_SEND_STR(p, glob.buffer);
  PSOCK_SEND_STR(p, "\r\n");
#if 0
  while(1) {
    PSOCK_READTO(p, '\n');
    printf("Got: %s", glob.buffer);
  }
#else
  PSOCK_CLOSE(p);
#endif
  PSOCK_END(p);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(psock_client_process, ev, data)
{
  PROCESS_BEGIN();

  tcp_connect(&glob.dest_addr, UIP_HTONS(glob.port), NULL);
  log_trace("Connecting...\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
  if(uip_aborted() || uip_timedout() || uip_closed()) {
    log_error("Could not establish connection\n");
  } else if(uip_connected()) {
    log_trace("Connected\n");
    PSOCK_INIT(&glob.ps, (unsigned char*)glob.buffer, sizeof(glob.buffer));
    do {
      handle_connection(&glob.ps);
      PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
    } while(!(uip_closed() || uip_aborted() || uip_timedout()));
    log_trace("Connection closed.\n");
  }
  PROCESS_END();
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
