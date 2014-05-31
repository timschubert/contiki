#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/rpl/rpl.h"

#include "net/netstack.h"
#include "uiplib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "httpd-simple.h"
#include "state.h"

/*---------------------------------------------------------------------------*/
static const char *TOP = "<html><head><title>ContikiRPL</title></head><body>\n";
static const char *SCRIPT = "<script src=\"script.js\"></script>\n";
static const char *BOTTOM = "</body></html>\n";
static char buf[512];
static int blen;
#define ADD(...) do {                                                   \
    blen += snprintf(&buf[blen], sizeof(buf) - blen, __VA_ARGS__);      \
  } while(0)
#define flush(x) { SEND_STRING(x, buf); blen = 0; }
/*---------------------------------------------------------------------------*/
static void
add_ipaddr_(const uip_ipaddr_t *addr, int link)
{
  uint16_t a;
  int i, f;
  if (link) ADD("<a>");
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) ADD("::");
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        ADD(":");
      }
      ADD("%x", a);
    }
  }
  if (link) ADD("</a>");
}
/*---------------------------------------------------------------------------*/
static inline void
add_ipaddr(const uip_ipaddr_t *addr)
{
  add_ipaddr_(addr, 1);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(generate_script(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sout);
  SEND_STRING(&s->sout, "\
  onload = function() {\n\
	p = location.host.replace(/::.*/, '::').substr(1);\n\
	a = document.getElementsByTagName('a');\n\
	for (i=0; i < a.length; i++) {\n\
		txt = a[i].innerHTML.replace(/^FE80::/, p);\n\
		a[i].href = 'http://['+txt+']';\n\
	}\n\
  }");
  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(generate_home_page(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sout);

  SEND_STRING(&s->sout, TOP);
  ADD("<pre>\n");
  ADD("button_state=%s\n", state.button_state ? "on" : "off");
  ADD("dest_address=");
  if (state.dest_addr_set) add_ipaddr_(&state.dest_addr, 0);
  ADD("\n");
  ADD("</pre>\n");
  flush(&s->sout);
  SEND_STRING(&s->sout, BOTTOM);

  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(generate_set_destination(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sout);

  if (s->response_header != httpd_response_200) {
    ADD("Incorrect IPV6 address (needs all 8 blocks).\n");
  }
  else {
    state.dest_addr_set = 1;
    ADD("dest_address=");
    add_ipaddr(&state.dest_addr);
    ADD("\n");
  }
  flush(&s->sout);

  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(generate_network_status(struct httpd_state *s))
{
  PSOCK_BEGIN(&s->sout);

  SEND_STRING(&s->sout, TOP);
  SEND_STRING(&s->sout, SCRIPT);
/*---------------------------------------------------------------------------*/
{
  ADD("Neighbors<pre>\n");

  static uip_ipaddr_t *preferred_parent_ip;
  { /* assume we have only one instance */
  rpl_dag_t *dag = rpl_get_any_dag();
  preferred_parent_ip = rpl_get_parent_ipaddr(dag->preferred_parent);
  }

  static uip_ds6_nbr_t *nbr;
  for(nbr = nbr_table_head(ds6_neighbors);
      nbr != NULL;
      nbr = nbr_table_next(ds6_neighbors, nbr)) {

      add_ipaddr(&nbr->ipaddr);

      uint8_t j=blen+25;
      while (blen < j) ADD(" ");
      switch (nbr->state) {
      case NBR_INCOMPLETE: ADD(" INCOMPLETE");break;
      case NBR_REACHABLE: ADD(" REACHABLE");break;
      case NBR_STALE: ADD(" STALE");break;
      case NBR_DELAY: ADD(" DELAY");break;
      case NBR_PROBE: ADD(" NBR_PROBE");break;
      }

      if (uip_ipaddr_cmp(&nbr->ipaddr, preferred_parent_ip))
        ADD(" PREFERRED");
      ADD("\n");
      if(blen > sizeof(buf) - 45) {
        flush(&s->sout);
      }
  }
  ADD("</pre>\n");
  flush(&s->sout);
}
/*---------------------------------------------------------------------------*/
{
  ADD("Default Route<pre>\n");
  add_ipaddr(uip_ds6_defrt_choose());
  ADD("\n</pre>\n");

  ADD("Routes<pre>\n");
  flush(&s->sout);

  static uip_ds6_route_t *r;
  for(r = uip_ds6_route_head(); r != NULL; r = uip_ds6_route_next(r)) {

    add_ipaddr(&r->ipaddr);
    ADD("/%u (via ", r->length);
    add_ipaddr(uip_ds6_route_nexthop(r));
    if(1 || (r->state.lifetime < 600)) {
      ADD(") %us\n", (unsigned int)r->state.lifetime);
    } else {
      ADD(")\n");
    }
    flush(&s->sout);
  }
  ADD("</pre>\n");
  flush(&s->sout);
}
/*---------------------------------------------------------------------------*/
  //PT_WAIT_THREAD(&s->outputpt, generate_neighbors(s));
  //PT_WAIT_THREAD(&s->outputpt, generate_routes(s));
  SEND_STRING(&s->sout, BOTTOM);

  PSOCK_END(&s->sout);
}
/*---------------------------------------------------------------------------*/
static struct httpd_query_map queries_map[] = {
  { "", generate_home_page },
  { "network", generate_network_status },
  { "set_destination?", generate_set_destination },
  { "script.js", generate_script },
  {}
};
/*---------------------------------------------------------------------------*/
static void validator(const char* params, struct httpd_state *s)
{
  if (s->generator == generate_set_destination) {
	if (!uiplib_ipaddrconv(params, &state.dest_addr))
		s->response_header = httpd_response_400;
  }
}
/*---------------------------------------------------------------------------*/
void http_server_init()
{
  httpd_init(queries_map, validator);
}
/*---------------------------------------------------------------------------*/
