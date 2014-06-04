#include "http-client.h"
#include <string.h> // strlen
#include <stdio.h>

static struct {
  struct psock ps;
  char buffer[1024];
} glob;
/*---------------------------------------------------------------------------*/
#define log_trace(...)
#define log_error(...) // printf(__VA_ARGS__)
/*---------------------------------------------------------------------------*/
PROCESS(http_client_process, "http client");
/*---------------------------------------------------------------------------*/
#define format_str(...) (sprintf(format_str_buf, __VA_ARGS__), format_str_buf)
static char format_str_buf[256];
/*---------------------------------------------------------------------------*/
static int
handle_connection(struct psock *p, struct http_request *req)
{
  PSOCK_BEGIN(p);

  PSOCK_SEND_STR(p, format_str("POST %s HTTP/1.0\r\n", req->path));
  PSOCK_SEND_STR(p, "User-Agent: Contiki/2.7 protosocket client\r\n");
  PSOCK_SEND_STR(p, "Content-Type: text/plain\r\n");
  PSOCK_SEND_STR(p, "Content-Length: ");
  PSOCK_SEND_STR(p, format_str("%d\r\n", strlen(req->data)));
  PSOCK_SEND_STR(p, "\r\n");
  PSOCK_SEND_STR(p, req->data);
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
PROCESS_THREAD(http_client_process, ev, data)
{
  static struct http_request *req;

  PROCESS_BEGIN();
  req = (struct http_request *)data;

  tcp_connect(&req->addr, UIP_HTONS(req->port), NULL);
  log_trace("Connecting...\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
  if(uip_aborted() || uip_timedout() || uip_closed()) {
    log_error("Could not establish connection\n");
  } else if(uip_connected()) {
    log_trace("Connected\n");
    PSOCK_INIT(&glob.ps, (unsigned char*)glob.buffer, sizeof(glob.buffer));
    do {
      handle_connection(&glob.ps, req);
      PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
    } while(!(uip_closed() || uip_aborted() || uip_timedout()));
    log_trace("Connection closed.\n");
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
