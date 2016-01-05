/*
 * Copyright (c) 2013, Matthias Kovatsch
 * All rights reserved.
 *
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
 */

/**
 * \file
 *      Erbium (Er) REST Engine example (with CoAP-specific code)
 * \author
 *      Matthias Kovatsch <kovatsch@inf.ethz.ch>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "dev/serial-line.h"
#include "erbium.h"

/* Define which resources to include to meet memory constraints. */
#define REST_RES_HELLO 1
#define REST_RES_CHUNKS 1
#define REST_RES_PUSHING 1
#define REST_RES_EVENT 1

/* For CoAP-specific example: not required for normal RESTful Web service. */
#if WITH_COAP == 3
#include "er-coap-03.h"
#elif WITH_COAP == 7
#include "er-coap-07.h"
#elif WITH_COAP == 12
#include "er-coap-12.h"
#elif WITH_COAP == 13
#include "er-coap-13.h"
#else
#warning "Erbium example without CoAP-specifc functionality"
#endif /* CoAP-specific example */

#define DEBUG 1
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif


RESOURCE(helloworld, METHOD_GET, "hello", "title=\"Hello world: ?len=0..\";rt=\"Text\"");

void
helloworld_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *len = NULL;
  /* Some data that has the length up to REST_MAX_CHUNK_SIZE. For more, see the chunk resource. */
  char const * const message = "Hello World! ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy";
  int length = 12; /*           |<-------->| */

  /* The query string can be retrieved by rest_get_query() or parsed for its key-value pairs. */
  if (REST.get_query_variable(request, "len", &len)) {
    length = atoi(len);
    if (length<0) length = 0;
    if (length>REST_MAX_CHUNK_SIZE) length = REST_MAX_CHUNK_SIZE;
    memcpy(buffer, message, length);
  } else {
    memcpy(buffer, message, length);
  }

  REST.set_header_content_type(response, REST.type.TEXT_PLAIN); /* text/plain is the default, hence this option could be omitted. */
  REST.set_header_etag(response, (uint8_t *) &length, 1);
  REST.set_response_payload(response, buffer, length);
}

RESOURCE(chunks, METHOD_GET, "test/chunks", "title=\"Blockwise demo\";rt=\"Data\"");

#define CHUNKS_TOTAL    2050

void
chunks_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  int32_t strpos = 0;
  PRINTF("OFFSET : %d\n", (int)*offset);
  PRINTF("PREFERRED_SIZE : %d\n", (int)preferred_size);
  /* Check the offset for boundaries of the resource data. */
  if (*offset>=CHUNKS_TOTAL)
  {
    REST.set_response_status(response, REST.status.BAD_OPTION);
    /* A block error message should not exceed the minimum block size (16). */

    const char *error_msg = "BlockOutOfScope";
    REST.set_response_payload(response, error_msg, strlen(error_msg));
    return;
  }

  /* Generate data until reaching CHUNKS_TOTAL. */
  while (strpos<preferred_size)
  {
    strpos += snprintf((char *)buffer+strpos, preferred_size-strpos+1, "|%ld|", *offset);
    PRINTF("STRPOS : %d\n", (int)strpos);
  }
  if (strpos > preferred_size)
  {
    strpos = preferred_size;
  }

  /* Truncate if above CHUNKS_TOTAL bytes. */
  if (*offset+(int32_t)strpos > CHUNKS_TOTAL)
  {
    strpos = CHUNKS_TOTAL - *offset;
  }
  PRINTF("STRPOS : %d\n", (int)strpos);
  PRINTF("BUFFER : %s\n", buffer);
  REST.set_response_payload(response, buffer, strpos);

  /* IMPORTANT for chunk-wise resources: Signal chunk awareness to REST engine. */
  *offset += strpos;

  /* Signal end of resource representation. */
  if (*offset>=CHUNKS_TOTAL)
  {
    *offset = -1;
  }
}

PERIODIC_RESOURCE(pushing, METHOD_GET, "test/push", "title=\"Periodic demo\";obs", 5*CLOCK_SECOND);

void
pushing_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);

  /* Usually, a CoAP server would response with the resource representation matching the periodic_handler. */
  const char *msg = "It's periodic!";
  REST.set_response_payload(response, msg, strlen(msg));

  /* A post_handler that handles subscriptions will be called for periodic resources by the REST framework. */
}

void
pushing_periodic_handler(resource_t *r)
{
  static uint16_t obs_counter = 0;
  static char content[11];

  ++obs_counter;

  PRINTF("PERIODIC TICK %u for /%s\n", obs_counter, r->url);

  /* Build notification. */
  coap_packet_t notification[1]; /* This way the packet can be treated as pointer as usual. */
  coap_init_message(notification, COAP_TYPE_NON, REST.status.OK, 0 );
  coap_set_payload(notification, content, snprintf(content, sizeof(content), "TICK %u", obs_counter));

  /* Notify the registered observers with the given message type, observe option, and payload. */
  REST.notify_subscribers(r, obs_counter, notification);
}

EVENT_RESOURCE(event, METHOD_GET, "sensors/button", "title=\"Event demo\";obs");

void
event_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  /* Usually, a CoAP server would response with the current resource representation. */
  const char *msg = "It's eventful!";
  REST.set_response_payload(response, (uint8_t *)msg, strlen(msg));

  /* A post_handler that handles subscriptions/observing will be called for periodic resources by the framework. */
}

void
event_event_handler(resource_t *r)
{
  static uint16_t event_counter = 0;
  static char content[30];
  char *data = rest_get_user_data(r);

  ++event_counter;

  PRINTF("EVENT TICK %u for /%s\n", event_counter, r->url);
  PRINTF("data : %s\n", data);

  /* Build notification. */
  coap_packet_t notification[1]; /* This way the packet can be treated as pointer as usual. */
  coap_init_message(notification, COAP_TYPE_CON, REST.status.OK, 0 );

  coap_set_payload(notification, content, snprintf(content, sizeof(content), "EVENT %u: %s", event_counter, data));

  /* Notify the registered observers with the given message type, observe option, and payload. */
  REST.notify_subscribers(r, event_counter, notification);
}


PROCESS(rest_server_example, "IoT-LAB CoAP Server");
AUTOSTART_PROCESSES(&rest_server_example);

PROCESS_THREAD(rest_server_example, ev, data)
{
  PROCESS_BEGIN();

  PRINTF("REST Max Chunk Size: %u\n", REST_MAX_CHUNK_SIZE);

  /* Initialize the REST engine. */
  rest_init_engine();

  /* Activate the application-specific resources. */
#if REST_RES_HELLO
  rest_activate_resource(&resource_helloworld);
#endif
#if REST_RES_CHUNKS
  rest_activate_resource(&resource_chunks);
#endif
#if REST_RES_PUSHING 
  rest_activate_periodic_resource(&periodic_resource_pushing);
#endif
#if REST_RES_EVENT 
  rest_activate_event_resource(&resource_event);
#endif
  
  while(1) {
    PROCESS_WAIT_EVENT();
    if (ev == serial_line_event_message) {
      PRINTF("Echo cmd: '%s'\n", (char*)data);
      rest_set_user_data(&resource_event, data);
#if REST_RES_EVENT
      /* Call the event_handler for this application-specific event. */
      event_event_handler(&resource_event);
#endif 
    }
  } /* while (1) */
   
  PROCESS_END();
}
