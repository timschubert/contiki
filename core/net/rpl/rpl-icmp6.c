/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
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
 *
 */

/**
 * \file
 *         ICMP6 I/O for RPL control messages.
 *
 * \author Joakim Eriksson <joakime@sics.se>, Nicolas Tsiftes <nvt@sics.se>
 * Contributors: Niclas Finne <nfi@sics.se>, Joel Hoglund <joel@sics.se>,
 *               Mathieu Pouillot <m.pouillot@watteco.com>
 *               George Oikonomou <oikonomou@users.sourceforge.net> (multicast)
 */

/**
 * \addtogroup uip6
 * @{
 */

#include "net/ip/tcpip.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uip-nd6.h"
#include "net/ipv6/uip-icmp6.h"
#include "net/rpl/rpl-private.h"
#include "net/rpl/rpl-ns.h"
#include "net/packetbuf.h"
#include "net/ipv6/multicast/uip-mcast6.h"
#include "random.h"

#include <limits.h>
#include <string.h>

#define DEBUG DEBUG_NONE

#include "net/ip/uip-debug.h"

/*---------------------------------------------------------------------------*/
#define RPL_DIO_GROUNDED                 0x80
#define RPL_DIO_MOP_SHIFT                3
#define RPL_DIO_MOP_MASK                 0x38
#define RPL_DIO_PREFERENCE_MASK          0x07

#define UIP_IP_BUF       ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF     ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_ICMP_PAYLOAD ((unsigned char *)&uip_buf[uip_l2_l3_icmp_hdr_len])
/*---------------------------------------------------------------------------*/
static void dis_input(void);
static void dio_input(void);
static void dao_input(void);
static void dao_ack_input(void);

static void dao_output_target_seq(rpl_parent_t *parent, uip_ipaddr_t *prefix,
				  uint8_t lifetime, uint8_t seq_no);

#ifdef RPL_RESTORE_USE_UIDS
static void uid_input(void);
static void uid_request_input(void);

UIP_ICMP6_HANDLER(uid_handler, ICMP6_RPL, RPL_CODE_UID, uid_input);
UIP_ICMP6_HANDLER(uid_request_handler, ICMP6_RPL, RPL_CODE_UID_REQUEST,  uid_request_input);
#endif

/* some debug callbacks useful when debugging RPL networks */
#ifdef RPL_DEBUG_DIO_INPUT
void RPL_DEBUG_DIO_INPUT(uip_ipaddr_t *, rpl_dio_t *);
#endif

#ifdef RPL_DEBUG_DAO_OUTPUT
void RPL_DEBUG_DAO_OUTPUT(rpl_parent_t *);
#endif

static uint8_t dao_sequence = RPL_LOLLIPOP_INIT;

#if RPL_WITH_MULTICAST
static uip_mcast6_route_t *mcast_group;
#endif
/*---------------------------------------------------------------------------*/
/* Initialise RPL ICMPv6 message handlers */
UIP_ICMP6_HANDLER(dis_handler, ICMP6_RPL, RPL_CODE_DIS, dis_input);
UIP_ICMP6_HANDLER(dio_handler, ICMP6_RPL, RPL_CODE_DIO, dio_input);
UIP_ICMP6_HANDLER(dao_handler, ICMP6_RPL, RPL_CODE_DAO, dao_input);
UIP_ICMP6_HANDLER(dao_ack_handler, ICMP6_RPL, RPL_CODE_DAO_ACK, dao_ack_input);
/*---------------------------------------------------------------------------*/

#if RPL_WITH_DAO_ACK
static uip_ds6_route_t *
find_route_entry_by_dao_ack(uint8_t seq)
{
  uip_ds6_route_t *re;
  re = uip_ds6_route_head();
  while(re != NULL) {
    if(re->state.dao_seqno_out == seq && RPL_ROUTE_IS_DAO_PENDING(re)) {
      /* found it! */
      return re;
    }
    re = uip_ds6_route_next(re);
  }
  return NULL;
}
#endif /* RPL_WITH_DAO_ACK */

#if RPL_WITH_STORING
/* prepare for forwarding of DAO */
static uint8_t
prepare_for_dao_fwd(uint8_t sequence, uip_ds6_route_t *rep)
{
  /* not pending - or pending but not a retransmission */
  RPL_LOLLIPOP_INCREMENT(dao_sequence);

  /* set DAO pending and sequence numbers */
  rep->state.dao_seqno_in = sequence;
  rep->state.dao_seqno_out = dao_sequence;
  RPL_ROUTE_SET_DAO_PENDING(rep);
  return dao_sequence;
}
#endif /* RPL_WITH_STORING */
/*---------------------------------------------------------------------------*/
static int
get_global_addr(uip_ipaddr_t *addr)
{
  int i;
  int state;

  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      if(!uip_is_addr_linklocal(&uip_ds6_if.addr_list[i].ipaddr)) {
        memcpy(addr, &uip_ds6_if.addr_list[i].ipaddr, sizeof(uip_ipaddr_t));
        return 1;
      }
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
static uint32_t
get32(uint8_t *buffer, int pos)
{
  return (uint32_t) buffer[pos] << 24 | (uint32_t) buffer[pos + 1] << 16
      | (uint32_t) buffer[pos + 2] << 8 | buffer[pos + 3];
}
/*---------------------------------------------------------------------------*/
static void
set32(uint8_t *buffer, int pos, uint32_t value)
{
  buffer[pos++] = value >> 24;
  buffer[pos++] = (value >> 16) & 0xff;
  buffer[pos++] = (value >> 8) & 0xff;
  buffer[pos++] = value & 0xff;
}
/*---------------------------------------------------------------------------*/
static uint16_t get16(uint8_t *buffer, int pos)
{
  return (uint16_t) buffer[pos] << 8 | buffer[pos + 1];
}
/*---------------------------------------------------------------------------*/
static void
set16(uint8_t *buffer, int pos, uint16_t value)
{
  buffer[pos++] = value >> 8;
  buffer[pos++] = value & 0xff;
}
/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
rpl_icmp6_update_nbr_table(uip_ipaddr_t *from, nbr_table_reason_t reason, void *data)
{
  uip_ds6_nbr_t *nbr;

  if((nbr = uip_ds6_nbr_lookup(from)) == NULL) {
    if((nbr = uip_ds6_nbr_add(from, (uip_lladdr_t *)
                              packetbuf_addr(PACKETBUF_ADDR_SENDER),
                              0, NBR_REACHABLE, reason, data)) != NULL) {
      PRINTF("RPL: Neighbor added to neighbor cache ");
      PRINT6ADDR(from);
      PRINTF(", ");
      PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
      PRINTF("\n");
    }
  }

  return nbr;
 }
/*---------------------------------------------------------------------------*/
#ifdef RPL_RESTORE

/*
 * some global offsets, and some data, so we know if and where to store the informations
 */

static uip_ipaddr_t parents[NBR_TABLE_MAX_NEIGHBORS];

static uint16_t neighborUpdateHashes[NBR_TABLE_MAX_NEIGHBORS];
static uint16_t neighborUpdateOffsets[NBR_TABLE_MAX_NEIGHBORS];
static uint16_t neighborClockOffsets[NBR_TABLE_MAX_NEIGHBORS];
static unsigned char neighborUpdates[NBR_TABLE_MAX_NEIGHBORS][NEIGHBOR_UPDATE_CONTENT_SIZE];

static uint16_t neighborCountOffset = 0;
static uint16_t dioIntOffset = 0;
static uint8_t baseConfigSaved = 0;
static uint8_t cancelRestore = 0;

static unsigned char baseConfig[47];

uint16_t clock;

/*
 * increase the clocks value. called by several functions in the rpl-dag.c
 */
void
clock_tick(uint8_t tick, char *text)
{
  clock += tick;
  PRINTF("RPL: new clock %u %s\n", clock, text);
}

/*
 * returns an array, [0]=actual index, [1]=bool: was this neighbor new?
 */
static uint8_t *
getNeighborIndex(uip_ipaddr_t in)
{
  static uint8_t ret[2];
  uint8_t i, j;
  for (i = 0; i < NBR_TABLE_MAX_NEIGHBORS; i++) {
    uint16_t used = 0;
    uint16_t equal = 1;
    for (j = 0; j < 8; j++) {
      used |= parents[i].u16[j];
      if (parents[i].u16[j] != in.u16[j]) {
        equal = 0;
      }
    }
    if (!used) {
      parents[i] = in;
      ret[0] = i;
      ret[1] = 1;
      return ret;
    }
    if (equal) {
      ret[0] = i;
      ret[1] = 0;
      return ret;
    }
  }
  ret[0] = 0;
  ret[1] = 0;
  return ret;
}

/*
 * returns the amount of saved neighbors
 */
static uint8_t
getNeighborCount()
{
  uint8_t i, j;
  for (i = 0; i < NBR_TABLE_MAX_NEIGHBORS; i++) {
    uint16_t used = 0;
    for (j = 0; j < 8; j++) {
      used |= parents[i].u16[j];
    }
    if (!used) {
      return i;
    }
  }
  return -1;
}

#ifndef RPL_RESTORE_SINGLE_ON_UID
/*
 * restore all saved neighbors
 */
static void
restore_all_dios()
{
  uint16_t i = 0;

  //first read some default values

  uint8_t mop = baseConfig[1];
  uip_ipaddr_t dag_id;

  for (i = 0; i < 8; i++) {
    uint16_t t = get16(baseConfig, 16 + i * 2);
    dag_id.u16[i] = t;
  }
  PRINTF("read dag_id: "); PRINT6ADDR(&dag_id); PRINTF("\n");

  uip_ipaddr_t prefix;
  for (i = 0; i < 8; i++) {
    uint16_t t = get16(baseConfig, 32 + i * 2);
    prefix.u16[i] = t;
  }

  PRINTF("read prefix: "); PRINT6ADDR(&prefix); PRINTF("\n");

  uint8_t intdoubl = baseConfig[2];
  uint8_t intmin = baseConfig[3];
  uint8_t redund = baseConfig[4];

  uint16_t dag_max_rankinc = get16(baseConfig, 8);
  uint16_t dag_min_hoprankinc = get16(baseConfig, 10);
  uint16_t ocp = get16(baseConfig, 12);
  uint8_t default_lifetime = baseConfig[5];
  uint16_t lifetime_unit = get16(baseConfig, 14);

  uint8_t prefix_length = baseConfig[6];
  uint8_t prefix_flags = baseConfig[7];

  uint32_t prefix_lifetime = 0; //this is always 0 in contikiRPL

  //PRINTF("read dag_id "); PRINT6ADDR(&dag_id); PRINTF(" prefix "); PRINT6ADDR(&prefix); PRINTF("\n");

  uint8_t id = 0;
  for (i = 0; i < getNeighborCount(); i++) { //cycle through the saved neighbors
    PRINTF("reading %u of %u dios\n", (i+1), getNeighborCount());

    rpl_dio_t dio;
    uint16_t j;
    uint16_t k = 0;

    memset(&dio, 0, sizeof(dio));

    /* Set default values in case the DIO configuration option is missing. */
    dio.dag_intdoubl = intdoubl;
    dio.dag_intmin = intmin;
    dio.dag_redund = redund;
    dio.dag_min_hoprankinc = dag_max_rankinc;
    dio.dag_max_rankinc = dag_min_hoprankinc;
    dio.ocp = ocp;
    dio.default_lifetime = default_lifetime;
    dio.lifetime_unit = lifetime_unit;

    getNeighborIndex(parents[i]);

    PRINTF("RPL: restoring "); PRINT6ADDR(&parents[i]);  PRINTF("\n");

    if (neighborUpdates[i][0] == 0 && neighborUpdates[i][1] == 0) {

      PRINTF("no neighbor update read yet\n");
      j = 0;
      uint16_t neighborUpdateOffset = 0;
      unsigned char neighborUpdate[256];
      while (j < 16) {
        k = 0;
        xmem_pread(neighborUpdate, 256,
            FIRST_NEIGHBOR_UPDATE_PAGE
                + NEIGHBOR_UPDATE_PAGE_SIZE * i + j * 256);
        while (k < 256) {
          if (neighborUpdate[k] == 1) {
            neighborUpdateOffset++;
          } else {
            neighborUpdateOffsets[i] = neighborUpdateOffset;
            j = 20;
            k = 256;
          }
          k++;
        }
        j++;
      }PRINTF("neighborUpdateOffset %u\n",neighborUpdateOffset);

      xmem_pread(neighborUpdates[i], 7,
          FIRST_NEIGHBOR_UPDATE_PAGE
              + NEIGHBOR_UPDATE_PAGE_SIZE
                  * i+ (neighborUpdateOffset-1)*NEIGHBOR_UPDATE_CONTENT_SIZE
                  + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE
                  + NEIGHBOR_CLOCK_PAGE_SIZE);

    }

    //fill the dio structure with the read data

    dio.instance_id = neighborUpdates[i][0];
    dio.version = neighborUpdates[i][1];
    dio.rank = get16(neighborUpdates[i], 5);

    PRINTF("RPL: Incoming DIO (id, ver, rank) = (%u,%u,%u)\n",
        (unsigned)dio.instance_id,
        (unsigned)dio.version,
        (unsigned)dio.rank);

    dio.grounded = neighborUpdates[i][3];
    dio.mop = mop;
    dio.preference = neighborUpdates[i][4];
    dio.dtsn = neighborUpdates[i][2];

    dio.dag_id = dag_id;

    PRINTF("RPL: DAG conf:dbl=%d, min=%d red=%d maxinc=%d mininc=%d ocp=%d d_l=%u l_u=%u\n",
        dio.dag_intdoubl, dio.dag_intmin, dio.dag_redund,
        dio.dag_max_rankinc, dio.dag_min_hoprankinc, dio.ocp,
        dio.default_lifetime, dio.lifetime_unit);

    dio.prefix_info.length = prefix_length;
    dio.prefix_info.flags = prefix_flags;

    dio.prefix_info.lifetime = prefix_lifetime;

    dio.prefix_info.prefix = prefix;

    PRINTF("RPL: Copying prefix information, length %u, flags %u, lifetime %u, prefix ", dio.prefix_info.length, dio.prefix_info.flags, dio.prefix_info.lifetime); PRINT6ADDR(&dio.prefix_info.prefix); PRINTF("\n");

    id = dio.instance_id;

    //let rpl do the rest
    rpl_process_dio(&parents[i], &dio);

  }

  //restore the (local) current dio interval

  rpl_instance_t *instance;
  instance = rpl_get_instance(id);

  unsigned char intCurrent[256];
  xmem_pread(intCurrent, 256, DIO_INT_CURRENT_PAGE);

  i = 0;
  uint8_t current = RPL_DIO_INTERVAL_MIN;
  while (i < 256) {
    if (intCurrent[i] != 0) {
      current = intCurrent[i];
    } else {
      dioIntOffset = i;
      i = 256;
    }
    i++;
  }
  PRINTF("intCurrent %u\n", current);
  instance->dio_intcurrent = current;

  PRINTF("done restoring\n");
}
#endif //RPL_RESTORE_SINGLE_ON_UID


#ifdef RPL_RESTORE_USE_UIDS

#ifdef RPL_RESTORE_ALL_ON_UID_THRESHOLD
static uint8_t uids_requested = 0;
static uint8_t uids_positive = 0;
static uint8_t restored = 0;

/*
 * handler for incomming uids from neighbor nodes, that restores all neighbors if enough positive uids were received
 */
static void
uid_input(void)
{
  PRINTF("RPL: received uid");
  if (restored) {
    PRINTF(", already restored -> ignore\n");
    return;
  }

#ifdef RPL_RESTORE_CANCEL_ON_NEGATIVE_UID
  if(cancelRestore) return;
#endif //RPL_RESTORE_CANCEL_ON_NEGATIVE_UID

  uip_ipaddr_t from;
  uip_ipaddr_copy(&from, &UIP_IP_BUF->srcipaddr);
  unsigned char *buffer;
  buffer = UIP_ICMP_PAYLOAD;
  uint16_t i, j, k;
  uint8_t index = getNeighborIndex(from)[0];

   PRINTF(" from "); PRINT6ADDR(&from); PRINTF("\n");

  if(getNeighborIndex(from)[1]) {
    PRINTF("received uid from unknown neighbor\n");
    return;
  }

  uint8_t dag_id_ok = 1;
  //check the received dag_id against the saved dag_id of this node and set a flag accordingly
  for (i = 0; i < 8; i++) {
    if (get16(buffer, 0 + i * 2) != get16(baseConfig, 16 + i * 2)) {
      dag_id_ok = 0;
    }
  }

  //read the rest of the needed values: instance_id, version, clock. they are stored in global arrays so they can be used later when the actual restoring process gets started
  j = 0;
  uint16_t neighborUpdateOffset = 0;
  unsigned char neighborUpdate[256];
  while (j < 16) {
    k = 0;
    xmem_pread(neighborUpdate, 256,
        FIRST_NEIGHBOR_UPDATE_PAGE + NEIGHBOR_UPDATE_PAGE_SIZE * index
            + j * 256);
    while (k < 256) {
      if (neighborUpdate[k] == 1) {
        neighborUpdateOffset++;
      } else {
        neighborUpdateOffsets[index] = neighborUpdateOffset;
        j = 20;
        k = 256;
      }
      k++;
    }
    j++;
  }PRINTF("neighborUpdateOffset %u\n",neighborUpdateOffset);

  xmem_pread(neighborUpdates[index], 7,
      FIRST_NEIGHBOR_UPDATE_PAGE
          + NEIGHBOR_UPDATE_PAGE_SIZE
              * index+ (neighborUpdateOffset-1)*NEIGHBOR_UPDATE_CONTENT_SIZE
              + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE
              + NEIGHBOR_CLOCK_PAGE_SIZE);



  j = 0;
  k = 0;
  uint16_t oldClock = 0;
  unsigned char clockArray[256];
  while (j < 16) {
    k = 0;
    xmem_pread(clockArray, 256,
        FIRST_NEIGHBOR_UPDATE_PAGE + NEIGHBOR_UPDATE_PAGE_SIZE * index
            + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE + j * 256);
    while (k < 256) {
      if ((clockArray[k] != 0) || (clockArray[k + 1] != 0)) {
        neighborClockOffsets[index] += 2;
      } else {
        //neighborUpdateOffsets[i] = neighborUpdateOffset;
        oldClock = get16(clockArray, k - 2);
        //upToDate = 1;
        j = 20;
        k = 256;
      }
      k += 2;
    }
    j++;
  } PRINTF("read old clock %u\n", oldClock);

  uint16_t newClock = get16(buffer, 18);

  PRINTF("[uid check] old data (instance_id, version, clock): %u, %u, %u\n",
      neighborUpdates[index][0], neighborUpdates[index][1], oldClock);
  PRINTF("[uid check] new data (instance_id, version, clock): %u, %u, %u\n",
      buffer[16], buffer[17], newClock);
  PRINTF("[uid check] dag_id_ok: %u\n", dag_id_ok);

  //check if the received parameters are still up-to-date
  if (dag_id_ok && //dag_id
      (neighborUpdates[index][0] == buffer[16]) && //instance_id
      (neighborUpdates[index][1] == buffer[17]) && //version
      ((newClock - oldClock) < CLOCK_DIFF_THRESHOLD)) //clock
      {
    PRINTF("[uid check] OK\n");
    uids_positive++;
  } else {
    cancelRestore = 1;
  }

  //if enough positive uids were received, restore all neighbors
  if ((uids_positive >= RPL_RESTORE_UID_THRESHOLD)) {
    restore_all_dios();
    restored = 1;
  }

}
#endif //RPL_RESTORE_ALL_ON_UID_THRESHOLD

#ifdef RPL_RESTORE_SINGLE_ON_UID
static uint8_t
intCurrentSet = 0;

//icmp6 handler for incomming uids from neighbor nodes, that restores each neighbor seperately if the corresponding uid was positive
static void
uid_input(void)
{
  uip_ipaddr_t from;
  uip_ipaddr_t uid_dag_id;
  uip_ipaddr_copy(&from, &UIP_IP_BUF->srcipaddr);
  unsigned char *uid;
  uid = UIP_ICMP_PAYLOAD;
  uint16_t i;
  rpl_dio_t dio;
  uint16_t j;

  PRINTF("RPL: received uid from ");
  PRINT6ADDR(&from);
  PRINTF("\n");

  //read dag_id from the uid and from the saved state

  for (i = 0; i < 8; i++) {
    uid_dag_id.u16[i] = get16(uid, 2 * i);
  }

  PRINTF("RPL: uid[dag_id, instance_id, version, clock] ");
  PRINT6ADDR(&uid_dag_id);
  PRINTF(", %u, %u, %u\n", uid[16], uid[17], get16(uid, 18));

  unsigned char baseConfig[47];
  xmem_pread(baseConfig, 47, GENERAL_CONFIG_PAGE);

  PRINTF("reading some invariant informations\n");

  uip_ipaddr_t dag_id;

  for (i = 0; i < 8; i++) {
    uint16_t t = get16(baseConfig, 16 + i * 2);
    dag_id.u16[i] = t;
    if(dag_id.u16[i] != uid_dag_id.u16[i]) return; // dont restore this neighbor if dag_id is not consistent
  }

  PRINTF("read dag_id: ");PRINT6ADDR(&dag_id);PRINTF("\n");

  //read saved prefix
  uip_ipaddr_t prefix;
  for (i = 0; i < 8; i++) {
    uint16_t t = get16(baseConfig, 32 + i * 2);
    prefix.u16[i] = t;
  }

  PRINTF("read prefix: ");PRINT6ADDR(&prefix);PRINTF("\n");

  PRINTF("read dag_id "); PRINT6ADDR(&dag_id); PRINTF(" prefix "); PRINT6ADDR(&prefix); PRINTF("\n");

  uint8_t *index;
  index = getNeighborIndex(from);

  PRINTF("parentIndex %u\n", index[0]);

  //read the saved clock
  j = 0;
  uint16_t k = 0;
  uint16_t oldClock = 0;
  unsigned char clockArray[256];
  while (j < 16) {
    k = 0;
    xmem_pread(clockArray, 256,
        FIRST_NEIGHBOR_UPDATE_PAGE
        + NEIGHBOR_UPDATE_PAGE_SIZE * index[0]
        + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE
        + j * 256);
    while (k < 256) {
      if ((clockArray[k] != 0) || (clockArray[k + 1] != 0)) {
        neighborClockOffsets[index[0]] += 2;
      } else {
        oldClock = get16(clockArray, k - 2);
        j = 20;
        k = 256;
      }
      k += 2;
    }
    j++;
  }
  PRINTF("read clock %u\n", oldClock);

  uint16_t newClock = get16(uid, 18);

  if(!((newClock - oldClock) < CLOCK_DIFF_THRESHOLD)) return; // check clocks for threshold

  //read and check for instance_id and version
  j = 0;
  uint16_t neighborUpdateOffset = 0;
  unsigned char neighborUpdate[256];
  while (j < 16) {
    k = 0;
    xmem_pread(neighborUpdate, 256,
        FIRST_NEIGHBOR_UPDATE_PAGE + NEIGHBOR_UPDATE_PAGE_SIZE * index[0]
            + j * 256);
    while (k < 256) {
      if (neighborUpdate[k] == 1) {
        neighborUpdateOffset++;
      } else {
        neighborUpdateOffsets[index[0]] = neighborUpdateOffset;
        j = 20;
        k = 256;
      }
      k++;
    }
    j++;
  }PRINTF("neighborUpdateOffset %u\n",neighborUpdateOffset);

  xmem_pread(neighborUpdates[index[0]], 7,
      FIRST_NEIGHBOR_UPDATE_PAGE
          + NEIGHBOR_UPDATE_PAGE_SIZE
              * index[0]+ (neighborUpdateOffset-1)*NEIGHBOR_UPDATE_CONTENT_SIZE
              + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE
              + NEIGHBOR_CLOCK_PAGE_SIZE);


  if((neighborUpdates[index[0]][0] != uid[16]) || (neighborUpdates[index[0]][1] != uid[17])) return; // check for instance_id and version consistency



  //if we are here, the received data from this neighbor is consistent with what we saved. now set some default values, read the saved state and hand the dio structure over to rpl
  memset(&dio, 0, sizeof(dio));

  /* Set default values in case the DIO configuration option is missing. */
  dio.dag_intdoubl = baseConfig[2];
  dio.dag_intmin = baseConfig[3];
  dio.dag_redund = baseConfig[4];
  dio.dag_min_hoprankinc = get16(baseConfig, 8);
  dio.dag_max_rankinc = get16(baseConfig, 10);
  dio.ocp = get16(baseConfig, 12);
  dio.default_lifetime = baseConfig[5];
  dio.lifetime_unit = get16(baseConfig, 14);

  unsigned char neighborConfig[32];
  xmem_pread(neighborConfig, 32,
      FIRST_NEIGHBOR_CONFIG_PAGE + NEIGHBOR_CONFIG_PAGE_SIZE * index[0]);

  for (j = 0; j < 8; j++) {
    uint16_t t = get16(neighborConfig, 0 + j * 2);
    from.u16[j] = t;
  }

  PRINTF("RPL: received dio from ");
  PRINT6ADDR(&from);
  PRINTF("\n");





  dio.instance_id = neighborUpdates[index[0]][0];
  dio.version = neighborUpdates[index[0]][1];
  dio.rank = get16(neighborUpdates[index[0]], 5);

  PRINTF("RPL: Incoming DIO (id, ver, rank) = (%u,%u,%u)\n",
      (unsigned)dio.instance_id,
      (unsigned)dio.version,
      (unsigned)dio.rank);

  dio.grounded = neighborUpdates[index[0]][3];
  dio.mop = baseConfig[1];
  dio.preference = neighborUpdates[index[0]][4];
  dio.dtsn = neighborUpdates[index[0]][2];

  dio.dag_id = dag_id;

  PRINTF("RPL: DAG conf:dbl=%d, min=%d red=%d maxinc=%d mininc=%d ocp=%d d_l=%u l_u=%u\n",
      dio.dag_intdoubl, dio.dag_intmin, dio.dag_redund,
      dio.dag_max_rankinc, dio.dag_min_hoprankinc, dio.ocp,
      dio.default_lifetime, dio.lifetime_unit);

  dio.prefix_info.length = baseConfig[6];
  dio.prefix_info.flags = baseConfig[7];
  dio.prefix_info.lifetime = 0;
  dio.prefix_info.prefix = prefix;

  PRINTF("RPL: Copying prefix information, length %u, flags %u, lifetime %u, prefix ", dio.prefix_info.length, dio.prefix_info.flags, dio.prefix_info.lifetime); PRINT6ADDR(&dio.prefix_info.prefix); PRINTF("\n");

  //let rpl do the rest
  rpl_process_dio(&from, &dio);

  //restore the (local) dio interval
  if (!intCurrentSet) {
    rpl_instance_t *instance;
    instance = rpl_get_instance(dio.instance_id);

    unsigned char intCurrent[256];
    xmem_pread(intCurrent, 256, DIO_INT_CURRENT_PAGE);

    i = 0;
    uint8_t current = RPL_DIO_INTERVAL_MIN;
    while (i < 256) {
      if (intCurrent[i] != 0) {
        current = intCurrent[i];
      } else {
        dioIntOffset = i;
        i = 256;
      }
      i++;
    }
    PRINTF("intCurrent %u\n", current);
    instance->dio_intcurrent = current;
    intCurrentSet = 1;
  }

}
#endif //RPL_RESTORE_SINGLE_ON_UID

/*
 * called by uid_request_input. outputs the requested uid
 */
void
uid_output(uip_ipaddr_t *to)
{
  unsigned char *buffer;
  uint8_t i;
  buffer = UIP_ICMP_PAYLOAD;


   PRINTF("RPL: sending uid to ");   PRINT6ADDR(to);   PRINTF("\n");

   PRINTF("RPL: uid[dag_id, instance_id, version, clock] ");
   PRINT6ADDR(&default_instance->current_dag->dag_id);
   PRINTF(", %u, %u, %u\n", default_instance->instance_id,
   default_instance->current_dag->version, clock);


  //dag_id
  for (i = 0; i < 8; i++) {
    set16(buffer, 2 * i, default_instance->current_dag->dag_id.u16[i]);
  }

  buffer[16] = default_instance->instance_id;

  buffer[17] = default_instance->current_dag->version;

  set16(buffer, 18, clock);

  uip_icmp6_send(to, ICMP6_RPL, RPL_CODE_UID, 20);
}

/*
 * icmp6 handler for incooming uid requests. calls uid_output to answer the request
 */
static void
uid_request_input(void)
{
  uip_ipaddr_t from;
  uip_ipaddr_copy(&from, &UIP_IP_BUF->srcipaddr);

  PRINTF("RPL: received uid_request from ");  PRINT6ADDR(&from);  PRINTF("\n");

  uid_output(&from);
}

#endif //RPL_RESTORE_USE_UIDS



/*
 * entry point for the restore before the rest of rpl gets initialized
 */
void
invoke_restore()
{
  uint16_t i = 0;

  //read the base configuration
  xmem_pread(baseConfig, 47, GENERAL_CONFIG_PAGE);

  //read how many neighbors were saved
  unsigned char countPage[1024];
  xmem_pread(countPage, 1024, NEIGHBOR_COUNT_PAGE);
  uint8_t count = 0;
  i = 0;
  while (i < 1024) {
    if (countPage[i] != 0) {
      count = countPage[i];
    } else {
      neighborCountOffset = i;
      i = 1024;
    }
    i++;
  }

  PRINTF("read parent count %u\n", count);

  for (i = 0; i < count; i++) { //for each neighbor that was stored
    PRINTF("reading %u of %u neighbors\n", (i + 1), count);

    // read the ip6 adresses for this neighbor
    uip_ipaddr_t from;
    uip_lladdr_t fromLL;
    uip_ds6_nbr_t *nbr;
    uint16_t j;

    unsigned char neighborConfig[32];
    xmem_pread(neighborConfig, 32,
    FIRST_NEIGHBOR_CONFIG_PAGE + NEIGHBOR_CONFIG_PAGE_SIZE * i);

    for (j = 0; j < 8; j++) {
      uint16_t t = get16(neighborConfig, 0 + j * 2);
      from.u16[j] = t;
    }
    for (j = 0; j < 8; j++) {
      uint16_t t = get16(neighborConfig, 16 + j * 2);
      fromLL.addr[j] = t;
    }
    getNeighborIndex(from);

    PRINTF("RPL: read data from neighbor ");
    PRINT6ADDR(&from);
    PRINTF("\n");

    //add this neighbor to the neighbor table
    if ((nbr = uip_ds6_nbr_lookup(&from)) == NULL) {
      if ((nbr = uip_ds6_nbr_add(&from, &fromLL, 0, NBR_REACHABLE,
                                 NBR_TABLE_REASON_UNDEFINED, NULL)) != NULL) {
        // already performed by nbr_add
        //stimer_set(&nbr->reachable, UIP_ND6_REACHABLE_TIME / 1000);
        PRINTF("RPL: Neighbor added to neighbor cache ");PRINT6ADDR(&from);PRINTF(", ");PRINT6ADDR(&fromLL);PRINTF("\n");
      } else {
        PRINTF("RPL: Out of memory, dropping DIO from ");PRINT6ADDR(&from);PRINTF(", ");PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));PRINTF("\n");
        return;
      }
    } else {
      PRINTF("RPL: Neighbor already in neighbor cache\n");
    }
  }

#ifdef RPL_RESTORE_USE_UIDS
  //if we should use uids, send them out to all rpl neighbors
  unsigned char *buffer;
  uip_ipaddr_t tmpaddr;
  buffer = UIP_ICMP_PAYLOAD;
  buffer[0] = buffer[1] = 0;
  PRINTF("sending uid request multicast\n");
  uip_create_linklocal_rplnodes_mcast(&tmpaddr);
  uip_icmp6_send(&tmpaddr, ICMP6_RPL, RPL_CODE_UID_REQUEST, 2);

#else
  // if not, just restore all neighbors
  restore_all_dios();
#endif
}

/*
 * save node specific data, if necessary
 */
static void
writeNodeUpdate(rpl_dio_t *dio, uint8_t index)
{
  PRINTF("save some specific variant informations\n");

  unsigned char toWrite[7] = { 1 };
  toWrite[0] = dio->instance_id;
  toWrite[1] = dio->version;
  toWrite[2] = dio->dtsn;
  toWrite[3] = dio->grounded;
  toWrite[4] = dio->preference;
  set16(toWrite, 5, dio->rank);

  PRINTF("(instance_id, version, dtsn, grounded, preference, rank): %u, %u, %u, %u, %u, %u\n", toWrite[0], toWrite[1], toWrite[2], toWrite[3], toWrite[4], dio->rank);

  //calculate the hash for this node update. the calculation could be replaced by any hash algorithm
  uint16_t hash = 0;
  hash += toWrite[0] + toWrite[1] + toWrite[3] + toWrite[4] + toWrite[5]
      + toWrite[6];

  if (neighborUpdateHashes[index] != hash) { //if the hashes are unequal (data has changed) write the update
    PRINTF("state has changed\n");
    neighborUpdateHashes[index] = hash;
    unsigned long pageOffset = FIRST_NEIGHBOR_UPDATE_PAGE
        + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE + NEIGHBOR_CLOCK_PAGE_SIZE
        + NEIGHBOR_UPDATE_CONTENT_SIZE * neighborUpdateOffsets[index]
        + NEIGHBOR_UPDATE_PAGE_SIZE * index;
    PRINTF("node update index %u, size*index %lu\n",index,pageOffset);
    xmem_pwrite(toWrite, 7, pageOffset);


    unsigned char offset[1] = { 1 };

    offset[0] = 1;    //pageOffsets[index]+1;

    pageOffset = FIRST_NEIGHBOR_UPDATE_PAGE + neighborUpdateOffsets[index]
        + NEIGHBOR_UPDATE_PAGE_SIZE * index;
    xmem_pwrite(offset, 1, pageOffset);

    neighborUpdateOffsets[index] += 1;

  }

  // independent from that, write the received clock value
  unsigned long offset = FIRST_NEIGHBOR_UPDATE_PAGE
      + NEIGHBOR_UPDATE_OFFSET_PAGE_SIZE
      + NEIGHBOR_UPDATE_PAGE_SIZE * index + neighborClockOffsets[index];
  unsigned char clockArray[2];
  set16(clockArray, 0, dio->in_clock);
  xmem_pwrite(clockArray, 2, offset);
  neighborClockOffsets[index] += 2;

}

/*
 * save the base config, invariant rpl-global informations
 */
static void
writeBaseConfig(rpl_dio_t *dio)
{

  PRINTF("saving base config\n");

  unsigned char toWrite[47] = { 1 };

  toWrite[1] = dio->mop;
  toWrite[2] = dio->dag_intdoubl;
  toWrite[3] = dio->dag_intmin;
  toWrite[4] = dio->dag_redund;
  toWrite[5] = dio->default_lifetime;
  toWrite[6] = dio->prefix_info.length;
  toWrite[7] = dio->prefix_info.flags;

  set16(toWrite, 8, dio->dag_max_rankinc);
  set16(toWrite, 10, dio->dag_min_hoprankinc);
  set16(toWrite, 12, dio->ocp);
  set16(toWrite, 14, dio->lifetime_unit);

  uint16_t i = 0;


  for (i = 0; i < 8; i++) {
    set16(toWrite, 16 + 2 * i, dio->dag_id.u16[i]);
  }


  for (i = 0; i < 8; i++) {
    set16(toWrite, 32 + 2 * i, dio->prefix_info.prefix.u16[i]);
  }

  xmem_pwrite(toWrite, 47, GENERAL_CONFIG_PAGE);

  baseConfigSaved = 1;

  PRINTF("done\n");
}

/*
 * save the ip6 adresses for a new neighbor
 */
static void
writeNodeConfig(uip_ipaddr_t *from, uip_ds6_nbr_t *nbr,
    uint8_t index)
{
  PRINTF("never seen this one, save some specific invariant informations\n");
  unsigned char nodeConfig[32] = { 1 };
  uint16_t i = 0;

  for (i = 0; i < 8; i++) {
    set16(nodeConfig, i * 2, from->u16[i]);
  }

  for (i = 0; i < 8; i++) {
    set16(nodeConfig, 16 + 2 * i, uip_ds6_nbr_get_ll(nbr)->addr[i]);
  }

  PRINT6ADDR(from);PRINTF(", ");PRINT6ADDR(uip_ds6_nbr_get_ll(nbr));PRINTF("\n");

  xmem_pwrite(nodeConfig, 32,
  FIRST_NEIGHBOR_CONFIG_PAGE + NEIGHBOR_CONFIG_PAGE_SIZE * index);

  unsigned char parentCount[1] = { 1 };
  parentCount[0] = getNeighborCount();

  xmem_pwrite(parentCount, 1, NEIGHBOR_COUNT_PAGE + neighborCountOffset);

  neighborCountOffset += 1;

}

#endif //RPL_RESTORE
/*---------------------------------------------------------------------------*/
static void
dis_input(void) {
  rpl_instance_t *instance;
  rpl_instance_t *end;

#ifdef RPL_RESTORE
  clock_tick(CLOCK_INCOMING_DIS, "CLOCK_INCOMING_DIS");
#endif

  /* DAG Information Solicitation */
  PRINTF("RPL: Received a DIS from ");PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  //uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");

  for(instance = &instance_table[0], end = instance + RPL_MAX_INSTANCES;
      instance < end; ++instance) {
    if(instance->used == 1) {
      if(uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
#if RPL_LEAF_ONLY
        PRINTF("RPL: LEAF ONLY Multicast DIS will NOT reset DIO timer\n");
#else /* !RPL_LEAF_ONLY */
        PRINTF("RPL: Multicast DIS => reset DIO timer\n");
        rpl_reset_dio_timer(instance);
#endif /* !RPL_LEAF_ONLY */
      } else {
        /* Check if this neighbor should be added according to the policy. */
        if(rpl_icmp6_update_nbr_table(&UIP_IP_BUF->srcipaddr,
                                      NBR_TABLE_REASON_RPL_DIS, NULL) == NULL) {
          PRINTF("RPL: Out of Memory, not sending unicast DIO, DIS from ");
          PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
          PRINTF(", ");
          PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
          PRINTF("\n");
        } else {
          PRINTF("RPL: Unicast DIS, reply to sender\n");
          dio_output(instance, &UIP_IP_BUF->srcipaddr);
        }
	/* } */
      }
    }
  }
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
dis_output(uip_ipaddr_t *addr)
{
  unsigned char *buffer;
  uip_ipaddr_t tmpaddr;

  /*
   * DAG Information Solicitation  - 2 bytes reserved
   *      0                   1                   2
   *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
   *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *     |     Flags     |   Reserved    |   Option(s)...
   *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

  buffer = UIP_ICMP_PAYLOAD;
  buffer[0] = buffer[1] = 0;

  if(addr == NULL) {
    uip_create_linklocal_rplnodes_mcast(&tmpaddr);
    addr = &tmpaddr;
  }

  PRINTF("RPL: Sending a DIS to ");PRINT6ADDR(addr);PRINTF("\n");

  uip_icmp6_send(addr, ICMP6_RPL, RPL_CODE_DIS, 2);
}
/*---------------------------------------------------------------------------*/
static void
dio_input(void)
{
  unsigned char *buffer;
  uint8_t buffer_length;
  rpl_dio_t dio;
  uint8_t subopt_type;
  int i;
  int len;
  uip_ipaddr_t from;

  memset(&dio, 0, sizeof(dio));

  /* Set default values in case the DIO configuration option is missing. */
  dio.dag_intdoubl = RPL_DIO_INTERVAL_DOUBLINGS;
  dio.dag_intmin = RPL_DIO_INTERVAL_MIN;
  dio.dag_redund = RPL_DIO_REDUNDANCY;
  dio.dag_min_hoprankinc = RPL_MIN_HOPRANKINC;
  dio.dag_max_rankinc = RPL_MAX_RANKINC;
  dio.ocp = RPL_OF_OCP;
  dio.default_lifetime = RPL_DEFAULT_LIFETIME;
  dio.lifetime_unit = RPL_DEFAULT_LIFETIME_UNIT;

  uip_ipaddr_copy(&from, &UIP_IP_BUF->srcipaddr);

  /* DAG Information Object */
  PRINTF("RPL: Received a DIO from ");
  PRINT6ADDR(&from);
  PRINTF("\n");

  buffer_length = uip_len - uip_l3_icmp_hdr_len;

  /* Process the DIO base option. */
  i = 0;
  buffer = UIP_ICMP_PAYLOAD;

  dio.instance_id = buffer[i++];
  dio.version = buffer[i++];
  dio.rank = get16(buffer, i);
  i += 2;

  PRINTF("RPL: Incoming DIO (id, ver, rank) = (%u,%u,%u)\n",
         (unsigned)dio.instance_id,
         (unsigned)dio.version,
         (unsigned)dio.rank);

  dio.grounded = buffer[i] & RPL_DIO_GROUNDED;
  dio.mop = (buffer[i]& RPL_DIO_MOP_MASK) >> RPL_DIO_MOP_SHIFT;
  dio.preference = buffer[i++] & RPL_DIO_PREFERENCE_MASK;

  dio.dtsn = buffer[i++];
  /* two reserved bytes */
  i += 2;

  memcpy(&dio.dag_id, buffer + i, sizeof(dio.dag_id));
  i += sizeof(dio.dag_id);

  PRINTF("RPL: Incoming DIO (dag_id, pref) = (");
  PRINT6ADDR(&dio.dag_id);
  PRINTF(", %u)\n", dio.preference);

  /* Check if there are any DIO suboptions. */
  for (; i < buffer_length; i += len) {

#ifdef RPL_RESTORE
    /* clock appears to be transmitted in last 2 bytes of DIO, not documented */
    if (i == buffer_length - 2) {
      dio.in_clock = (uint16_t) buffer[i];
      continue;
    }
#endif

    subopt_type = buffer[i];
    if(subopt_type == RPL_OPTION_PAD1) {
      len = 1;
    } else {
      /* Suboption with a two-byte header + payload */
      len = 2 + buffer[i + 1];
    }

    if(len + i > buffer_length) {
      PRINTF("RPL: Invalid DIO packet\n");
      RPL_STAT(rpl_stats.malformed_msgs++);
      goto discard;
    }

    PRINTF("RPL: DIO option %u, length: %u\n", subopt_type, len - 2);

    switch(subopt_type) {
    case RPL_OPTION_DAG_METRIC_CONTAINER:
      if(len < 6) {
        PRINTF("RPL: Invalid DAG MC, len = %d\n", len);
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }
      dio.mc.type = buffer[i + 2];
      dio.mc.flags = buffer[i + 3] << 1;
      dio.mc.flags |= buffer[i + 4] >> 7;
      dio.mc.aggr = (buffer[i + 4] >> 4) & 0x3;
      dio.mc.prec = buffer[i + 4] & 0xf;
      dio.mc.length = buffer[i + 5];

      if(dio.mc.type == RPL_DAG_MC_NONE) {
        /* No metric container: do nothing */
      } else if(dio.mc.type == RPL_DAG_MC_ETX) {
        dio.mc.obj.etx = get16(buffer, i + 6);

        PRINTF("RPL: DAG MC: type %u, flags %u, aggr %u, prec %u, length %u, ETX %u\n",
	       (unsigned)dio.mc.type,
	       (unsigned)dio.mc.flags,
	       (unsigned)dio.mc.aggr,
	       (unsigned)dio.mc.prec,
	       (unsigned)dio.mc.length,
	       (unsigned)dio.mc.obj.etx);
      } else if(dio.mc.type == RPL_DAG_MC_ENERGY) {
        dio.mc.obj.energy.flags = buffer[i + 6];
        dio.mc.obj.energy.energy_est = buffer[i + 7];
      } else {
       PRINTF("RPL: Unhandled DAG MC type: %u\n", (unsigned)dio.mc.type);
       goto discard;
      }
      break;
    case RPL_OPTION_ROUTE_INFO:
      if(len < 9) {
        PRINTF("RPL: Invalid destination prefix option, len = %d\n", len);
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      /* The flags field includes the preference value. */
      dio.destination_prefix.length = buffer[i + 2];
      dio.destination_prefix.flags = buffer[i + 3];
      dio.destination_prefix.lifetime = get32(buffer, i + 4);

      if(((dio.destination_prefix.length + 7) / 8) + 8 <= len &&
         dio.destination_prefix.length <= 128) {
        PRINTF("RPL: Copying destination prefix\n");
        memcpy(&dio.destination_prefix.prefix, &buffer[i + 8],
               (dio.destination_prefix.length + 7) / 8);
      } else {
        PRINTF("RPL: Invalid route info option, len = %d\n", len);
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      break;
    case RPL_OPTION_DAG_CONF:
      if(len != 16) {
        PRINTF("RPL: Invalid DAG configuration option, len = %d\n", len);
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }

      /* Path control field not yet implemented - at i + 2 */
      dio.dag_intdoubl = buffer[i + 3];
      dio.dag_intmin = buffer[i + 4];
      dio.dag_redund = buffer[i + 5];
      dio.dag_max_rankinc = get16(buffer, i + 6);
      dio.dag_min_hoprankinc = get16(buffer, i + 8);
      dio.ocp = get16(buffer, i + 10);
      /* buffer + 12 is reserved */
      dio.default_lifetime = buffer[i + 13];
      dio.lifetime_unit = get16(buffer, i + 14);
      PRINTF("RPL: DAG conf:dbl=%d, min=%d red=%d maxinc=%d mininc=%d ocp=%d d_l=%u l_u=%u\n",
             dio.dag_intdoubl, dio.dag_intmin, dio.dag_redund,
             dio.dag_max_rankinc, dio.dag_min_hoprankinc, dio.ocp,
             dio.default_lifetime, dio.lifetime_unit);
      break;
    case RPL_OPTION_PREFIX_INFO:
      if(len != 32) {
        PRINTF("RPL: Invalid DAG prefix info, len != 32\n");
	RPL_STAT(rpl_stats.malformed_msgs++);
        goto discard;
      }
      dio.prefix_info.length = buffer[i + 2];
      dio.prefix_info.flags = buffer[i + 3];
      /* valid lifetime is ingnored for now - at i + 4 */
      /* preferred lifetime stored in lifetime */
      dio.prefix_info.lifetime = get32(buffer, i + 8);
      /* 32-bit reserved at i + 12 */
      PRINTF("RPL: Copying prefix information\n");
      memcpy(&dio.prefix_info.prefix, &buffer[i + 16], 16);
      break;
    default:
      PRINTF("RPL: Unsupported suboption type in DIO: %u\n",
	(unsigned)subopt_type);
    }
  }

#ifdef RPL_DEBUG_DIO_INPUT
  RPL_DEBUG_DIO_INPUT(&from, &dio);
#endif

#ifdef RPL_RESTORE
  PRINTF("RPL: received clock %u\n", dio.in_clock);

  uip_ds6_nbr_t *nbr = rpl_icmp6_update_nbr_table(&from, NBR_TABLE_REASON_RPL_DIO, NULL);

  if (!baseConfigSaved) { //save the base config, if not done already
    writeBaseConfig(&dio);
  }

  uint8_t *index;

  index = getNeighborIndex(from);

  PRINTF("parent index: %u\n", index[0]);

  if (index[1]) { // received first dio of this neighbour, so save some basic informations about him
    writeNodeConfig(&from, nbr, index[0]);
  }

  writeNodeUpdate(&dio, index[0]); // write the node update
  PRINTF("done\n");
#endif

  rpl_process_dio(&from, &dio);

 discard:
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
dio_output(rpl_instance_t *instance, uip_ipaddr_t *uc_addr)
{
  unsigned char *buffer;
  int pos;
  int is_root;
  rpl_dag_t *dag = instance->current_dag;
#if !RPL_LEAF_ONLY
  uip_ipaddr_t addr;
#endif /* !RPL_LEAF_ONLY */

#if RPL_LEAF_ONLY
  /* In leaf mode, we only send DIO messages as unicasts in response to
     unicast DIS messages. */
  if(uc_addr == NULL) {
    PRINTF("RPL: LEAF ONLY have multicast addr: skip dio_output\n");
    return;
  }
#endif /* RPL_LEAF_ONLY */

  /* DAG Information Object */
  pos = 0;

  buffer = UIP_ICMP_PAYLOAD;
  buffer[pos++] = instance->instance_id;
  buffer[pos++] = dag->version;
  is_root = (dag->rank == ROOT_RANK(instance));

#ifdef RPL_RESTORE
  //save the new current dio interval
  unsigned char intCurrent[1] = { 1 };

  intCurrent[0] = instance->dio_intcurrent;
  intCurrent[0] += 1;
  xmem_pwrite(intCurrent, 1, DIO_INT_CURRENT_PAGE + dioIntOffset);

  dioIntOffset += 1;
#endif

#if RPL_LEAF_ONLY
  PRINTF("RPL: LEAF ONLY DIO rank set to INFINITE_RANK\n");
  set16(buffer, pos, INFINITE_RANK);
#else /* RPL_LEAF_ONLY */
  set16(buffer, pos, dag->rank);
#endif /* RPL_LEAF_ONLY */
  pos += 2;

  buffer[pos] = 0;
  if(dag->grounded) {
    buffer[pos] |= RPL_DIO_GROUNDED;
  }

  buffer[pos] |= instance->mop << RPL_DIO_MOP_SHIFT;
  buffer[pos] |= dag->preference & RPL_DIO_PREFERENCE_MASK;
  pos++;

  buffer[pos++] = instance->dtsn_out;

  if(RPL_DIO_REFRESH_DAO_ROUTES && is_root && uc_addr == NULL) {
    /* Request new DAO to refresh route. We do not do this for unicast DIO
     * in order to avoid DAO messages after a DIS-DIO update,
     * or upon unicast DIO probing. */
    RPL_LOLLIPOP_INCREMENT(instance->dtsn_out);
  }

  /* reserved 2 bytes */
  buffer[pos++] = 0; /* flags */
  buffer[pos++] = 0; /* reserved */

  memcpy(buffer + pos, &dag->dag_id, sizeof(dag->dag_id));
  pos += 16;

#if !RPL_LEAF_ONLY
  if(instance->mc.type != RPL_DAG_MC_NONE) {
    instance->of->update_metric_container(instance);

    buffer[pos++] = RPL_OPTION_DAG_METRIC_CONTAINER;
    buffer[pos++] = 6;
    buffer[pos++] = instance->mc.type;
    buffer[pos++] = instance->mc.flags >> 1;
    buffer[pos] = (instance->mc.flags & 1) << 7;
    buffer[pos++] |= (instance->mc.aggr << 4) | instance->mc.prec;
    if(instance->mc.type == RPL_DAG_MC_ETX) {
      buffer[pos++] = 2;
      set16(buffer, pos, instance->mc.obj.etx);
      pos += 2;
    } else if(instance->mc.type == RPL_DAG_MC_ENERGY) {
      buffer[pos++] = 2;
      buffer[pos++] = instance->mc.obj.energy.flags;
      buffer[pos++] = instance->mc.obj.energy.energy_est;
    } else {
      PRINTF("RPL: Unable to send DIO because of unhandled DAG MC type %u\n",
	(unsigned)instance->mc.type);
      return;
    }
  }
#endif /* !RPL_LEAF_ONLY */

  /* Always add a DAG configuration option. */
  buffer[pos++] = RPL_OPTION_DAG_CONF;
  buffer[pos++] = 14;
  buffer[pos++] = 0; /* No Auth, PCS = 0 */
  buffer[pos++] = instance->dio_intdoubl;
  buffer[pos++] = instance->dio_intmin;
  buffer[pos++] = instance->dio_redundancy;
  set16(buffer, pos, instance->max_rankinc);
  pos += 2;
  set16(buffer, pos, instance->min_hoprankinc);
  pos += 2;
  /* OCP is in the DAG_CONF option */
  set16(buffer, pos, instance->of->ocp);
  pos += 2;
  buffer[pos++] = 0; /* reserved */
  buffer[pos++] = instance->default_lifetime;
  set16(buffer, pos, instance->lifetime_unit);
  pos += 2;

  /* Check if we have a prefix to send also. */
  if(dag->prefix_info.length > 0) {
    buffer[pos++] = RPL_OPTION_PREFIX_INFO;
    buffer[pos++] = 30; /* always 30 bytes + 2 long */
    buffer[pos++] = dag->prefix_info.length;
    buffer[pos++] = dag->prefix_info.flags;
    set32(buffer, pos, dag->prefix_info.lifetime);
    pos += 4;
    set32(buffer, pos, dag->prefix_info.lifetime);
    pos += 4;
    memset(&buffer[pos], 0, 4);
    pos += 4;
    memcpy(&buffer[pos], &dag->prefix_info.prefix, 16);
    pos += 16;
    PRINTF("RPL: Sending prefix info in DIO for ");
    PRINT6ADDR(&dag->prefix_info.prefix);
    PRINTF("\n");
  } else {
    PRINTF("RPL: No prefix to announce (len %d)\n",
           dag->prefix_info.length);
  }

#ifdef RPL_RESTORE
  //add the current clock to the end of the dio message
  set16(buffer, pos, clock);
  pos += 2;
#endif

#if RPL_LEAF_ONLY
#if (DEBUG) & DEBUG_PRINT
  if(uc_addr == NULL) {
    PRINTF("RPL: LEAF ONLY sending unicast-DIO from multicast-DIO\n");
  }
#endif /* DEBUG_PRINT */
  PRINTF("RPL: Sending unicast-DIO with rank %u to ",
      (unsigned)dag->rank);
  PRINT6ADDR(uc_addr);
  PRINTF("\n");
  uip_icmp6_send(uc_addr, ICMP6_RPL, RPL_CODE_DIO, pos);
#else /* RPL_LEAF_ONLY */
  /* Unicast requests get unicast replies! */
  if (uc_addr == NULL) {
    PRINTF("RPL: Sending a multicast-DIO with rank %u\n",
        (unsigned)instance->current_dag->rank);
    uip_create_linklocal_rplnodes_mcast(&addr);
    uip_icmp6_send(&addr, ICMP6_RPL, RPL_CODE_DIO, pos);
  } else {
    PRINTF("RPL: Sending unicast-DIO with rank %u to ",
        (unsigned)instance->current_dag->rank);PRINT6ADDR(uc_addr);PRINTF("\n");
    uip_icmp6_send(uc_addr, ICMP6_RPL, RPL_CODE_DIO, pos);
  }
#endif /* RPL_LEAF_ONLY */
}
/*---------------------------------------------------------------------------*/
static void
dao_input_storing(void)
{
#if RPL_WITH_STORING
  uip_ipaddr_t dao_sender_addr;
  rpl_dag_t *dag;
  rpl_instance_t *instance;
  unsigned char *buffer;
  uint16_t sequence;
  uint8_t instance_id;
  uint8_t lifetime;
  uint8_t prefixlen;
  uint8_t flags;
  uint8_t subopt_type;
  /*
  uint8_t pathcontrol;
  uint8_t pathsequence;
  */
  uip_ipaddr_t prefix;
  uip_ds6_route_t *rep;
  uint8_t buffer_length;
  int pos;
  int len;
  int i;
  int learned_from;
  rpl_parent_t *parent;
  uip_ds6_nbr_t *nbr;
  int is_root;

#ifdef RPL_RESTORE
  //increase the local clock
  clock_tick(CLOCK_INCOMING_DAO, "CLOCK_INCOMING_DAO");
#endif

  prefixlen = 0;
  parent = NULL;

  uip_ipaddr_copy(&dao_sender_addr, &UIP_IP_BUF->srcipaddr);

  buffer = UIP_ICMP_PAYLOAD;
  buffer_length = uip_len - uip_l3_icmp_hdr_len;

  pos = 0;
  instance_id = buffer[pos++];

  instance = rpl_get_instance(instance_id);

  lifetime = instance->default_lifetime;

  flags = buffer[pos++];
  /* reserved */
  pos++;
  sequence = buffer[pos++];

  dag = instance->current_dag;
  is_root = (dag->rank == ROOT_RANK(instance));

  /* Is the DAG ID present? */
  if(flags & RPL_DAO_D_FLAG) {
    if(memcmp(&dag->dag_id, &buffer[pos], sizeof(dag->dag_id))) {
      PRINTF("RPL: Ignoring a DAO for a DAG different from ours\n");
      return;
    }
    pos += 16;
  }

  learned_from = uip_is_addr_mcast(&dao_sender_addr) ?
                 RPL_ROUTE_FROM_MULTICAST_DAO : RPL_ROUTE_FROM_UNICAST_DAO;

  /* Destination Advertisement Object */
  PRINTF("RPL: Received a (%s) DAO with sequence number %u from ",
      learned_from == RPL_ROUTE_FROM_UNICAST_DAO? "unicast": "multicast", sequence);
  PRINT6ADDR(&dao_sender_addr);
  PRINTF("\n");

  if(learned_from == RPL_ROUTE_FROM_UNICAST_DAO) {
    /* Check whether this is a DAO forwarding loop. */
    parent = rpl_find_parent(dag, &dao_sender_addr);
    /* check if this is a new DAO registration with an "illegal" rank */
    /* if we already route to this node it is likely */
    if(parent != NULL &&
       DAG_RANK(parent->rank, instance) < DAG_RANK(dag->rank, instance)) {
      PRINTF("RPL: Loop detected when receiving a unicast DAO from a node with a lower rank! (%u < %u)\n",
          DAG_RANK(parent->rank, instance), DAG_RANK(dag->rank, instance));
      parent->rank = INFINITE_RANK;
      parent->flags |= RPL_PARENT_FLAG_UPDATED;
      return;
    }

    /* If we get the DAO from our parent, we also have a loop. */
    if(parent != NULL && parent == dag->preferred_parent) {
      PRINTF("RPL: Loop detected when receiving a unicast DAO from our parent\n");
      parent->rank = INFINITE_RANK;
      parent->flags |= RPL_PARENT_FLAG_UPDATED;
      return;
    }
  }

  /* Check if there are any RPL options present. */
  for(i = pos; i < buffer_length; i += len) {
    subopt_type = buffer[i];
    if(subopt_type == RPL_OPTION_PAD1) {
      len = 1;
    } else {
      /* The option consists of a two-byte header and a payload. */
      len = 2 + buffer[i + 1];
    }

    switch(subopt_type) {
    case RPL_OPTION_TARGET:
      /* Handle the target option. */
      prefixlen = buffer[i + 3];
      memset(&prefix, 0, sizeof(prefix));
      memcpy(&prefix, buffer + i + 4, (prefixlen + 7) / CHAR_BIT);
      break;
    case RPL_OPTION_TRANSIT:
      /* The path sequence and control are ignored. */
      /*      pathcontrol = buffer[i + 3];
              pathsequence = buffer[i + 4];*/
      lifetime = buffer[i + 5];
      /* The parent address is also ignored. */
      break;
    }
  }

  PRINTF("RPL: DAO lifetime: %u, prefix length: %u prefix: ",
          (unsigned)lifetime, (unsigned)prefixlen);
  PRINT6ADDR(&prefix);
  PRINTF("\n");

#if RPL_WITH_MULTICAST
  if(uip_is_addr_mcast_global(&prefix)) {
    mcast_group = uip_mcast6_route_add(&prefix);
    if(mcast_group) {
      mcast_group->dag = dag;
      mcast_group->lifetime = RPL_LIFETIME(instance, lifetime);
    }
    goto fwd_dao;
  }
#endif

  rep = uip_ds6_route_lookup(&prefix);

  if(lifetime == RPL_ZERO_LIFETIME) {
    PRINTF("RPL: No-Path DAO received\n");
    /* No-Path DAO received; invoke the route purging routine. */
    if(rep != NULL &&
       !RPL_ROUTE_IS_NOPATH_RECEIVED(rep) &&
       rep->length == prefixlen &&
       uip_ds6_route_nexthop(rep) != NULL &&
       uip_ipaddr_cmp(uip_ds6_route_nexthop(rep), &dao_sender_addr)) {
      PRINTF("RPL: Setting expiration timer for prefix ");
      PRINT6ADDR(&prefix);
      PRINTF("\n");
      RPL_ROUTE_SET_NOPATH_RECEIVED(rep);
      rep->state.lifetime = RPL_NOPATH_REMOVAL_DELAY;

      /* We forward the incoming No-Path DAO to our parent, if we have
         one. */
      if(dag->preferred_parent != NULL &&
         rpl_get_parent_ipaddr(dag->preferred_parent) != NULL) {
        uint8_t out_seq;
        out_seq = prepare_for_dao_fwd(sequence, rep);

        PRINTF("RPL: Forwarding No-path DAO to parent - out_seq:%d",
	       out_seq);
        PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent));
        PRINTF("\n");

        buffer = UIP_ICMP_PAYLOAD;
        buffer[3] = out_seq; /* add an outgoing seq no before fwd */
        uip_icmp6_send(rpl_get_parent_ipaddr(dag->preferred_parent),
                       ICMP6_RPL, RPL_CODE_DAO, buffer_length);
      }
    }
    /* independent if we remove or not - ACK the request */
    if(flags & RPL_DAO_K_FLAG) {
      /* indicate that we accepted the no-path DAO */
      uip_clear_buf();
      dao_ack_output(instance, &dao_sender_addr, sequence,
                     RPL_DAO_ACK_UNCONDITIONAL_ACCEPT);
    }
    return;
  }

  PRINTF("RPL: Adding DAO route\n");

  /* Update and add neighbor - if no room - fail. */
  if((nbr = rpl_icmp6_update_nbr_table(&dao_sender_addr, NBR_TABLE_REASON_RPL_DAO, instance)) == NULL) {
    PRINTF("RPL: Out of Memory, dropping DAO from ");
    PRINT6ADDR(&dao_sender_addr);
    PRINTF(", ");
    PRINTLLADDR((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
    PRINTF("\n");
    if(flags & RPL_DAO_K_FLAG) {
      /* signal the failure to add the node */
      dao_ack_output(instance, &dao_sender_addr, sequence,
		     is_root ? RPL_DAO_ACK_UNABLE_TO_ADD_ROUTE_AT_ROOT :
		     RPL_DAO_ACK_UNABLE_TO_ACCEPT);
    }
    return;
  }

  rep = rpl_add_route(dag, &prefix, prefixlen, &dao_sender_addr);
  if(rep == NULL) {
    RPL_STAT(rpl_stats.mem_overflows++);
    PRINTF("RPL: Could not add a route after receiving a DAO\n");
    if(flags & RPL_DAO_K_FLAG) {
      /* signal the failure to add the node */
      dao_ack_output(instance, &dao_sender_addr, sequence,
		     is_root ? RPL_DAO_ACK_UNABLE_TO_ADD_ROUTE_AT_ROOT :
		     RPL_DAO_ACK_UNABLE_TO_ACCEPT);
    }
    return;
  }

  /* set lifetime and clear NOPATH bit */
  rep->state.lifetime = RPL_LIFETIME(instance, lifetime);
  RPL_ROUTE_CLEAR_NOPATH_RECEIVED(rep);

#if RPL_WITH_MULTICAST
fwd_dao:
#endif

  if(learned_from == RPL_ROUTE_FROM_UNICAST_DAO) {
    int should_ack = 0;

    if(flags & RPL_DAO_K_FLAG) {
      /*
       * check if this route is already installed and we can ack now!
       * not pending - and same seq-no means that we can ack.
       * (e.g. the route is installed already so it will not take any
       * more room that it already takes - so should be ok!)
       */
      if((!RPL_ROUTE_IS_DAO_PENDING(rep) &&
          rep->state.dao_seqno_in == sequence) ||
          dag->rank == ROOT_RANK(instance)) {
        should_ack = 1;
      }
    }

    if(dag->preferred_parent != NULL &&
       rpl_get_parent_ipaddr(dag->preferred_parent) != NULL) {
      uint8_t out_seq = 0;
      /* if this is pending and we get the same seq no it is a retrans */
      if(RPL_ROUTE_IS_DAO_PENDING(rep) &&
         rep->state.dao_seqno_in == sequence) {
        /* keep the same seq-no as before for parent also */
        out_seq = rep->state.dao_seqno_out;
      } else {
        out_seq = prepare_for_dao_fwd(sequence, rep);
      }

      PRINTF("RPL: Forwarding DAO to parent ");
      PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent));
      PRINTF(" in seq: %d out seq: %d\n", sequence, out_seq);

      buffer = UIP_ICMP_PAYLOAD;
      buffer[3] = out_seq; /* add an outgoing seq no before fwd */
      uip_icmp6_send(rpl_get_parent_ipaddr(dag->preferred_parent),
                     ICMP6_RPL, RPL_CODE_DAO, buffer_length);
    }
    if(should_ack) {
      PRINTF("RPL: Sending DAO ACK\n");
      uip_clear_buf();
      dao_ack_output(instance, &dao_sender_addr, sequence,
                     RPL_DAO_ACK_UNCONDITIONAL_ACCEPT);
    }
  }
#endif /* RPL_WITH_STORING */
}
/*---------------------------------------------------------------------------*/
static void
dao_input_nonstoring(void)
{
#if RPL_WITH_NON_STORING
  uip_ipaddr_t dao_sender_addr;
  uip_ipaddr_t dao_parent_addr;
  rpl_dag_t *dag;
  rpl_instance_t *instance;
  unsigned char *buffer;
  uint16_t sequence;
  uint8_t instance_id;
  uint8_t lifetime;
  uint8_t prefixlen;
  uint8_t flags;
  uint8_t subopt_type;
  uip_ipaddr_t prefix;
  uint8_t buffer_length;
  int pos;
  int len;
  int i;

  prefixlen = 0;

  uip_ipaddr_copy(&dao_sender_addr, &UIP_IP_BUF->srcipaddr);
  memset(&dao_parent_addr, 0, 16);

  buffer = UIP_ICMP_PAYLOAD;
  buffer_length = uip_len - uip_l3_icmp_hdr_len;

  pos = 0;
  instance_id = buffer[pos++];
  instance = rpl_get_instance(instance_id);
  lifetime = instance->default_lifetime;

  flags = buffer[pos++];
  /* reserved */
  pos++;
  sequence = buffer[pos++];

  dag = instance->current_dag;
  /* Is the DAG ID present? */
  if(flags & RPL_DAO_D_FLAG) {
    if(memcmp(&dag->dag_id, &buffer[pos], sizeof(dag->dag_id))) {
      PRINTF("RPL: Ignoring a DAO for a DAG different from ours\n");
      return;
    }
    pos += 16;
  }

  /* Check if there are any RPL options present. */
  for(i = pos; i < buffer_length; i += len) {
    subopt_type = buffer[i];
    if(subopt_type == RPL_OPTION_PAD1) {
      len = 1;
    } else {
      /* The option consists of a two-byte header and a payload. */
      len = 2 + buffer[i + 1];
    }

    switch(subopt_type) {
    case RPL_OPTION_TARGET:
      /* Handle the target option. */
      prefixlen = buffer[i + 3];
      memset(&prefix, 0, sizeof(prefix));
      memcpy(&prefix, buffer + i + 4, (prefixlen + 7) / CHAR_BIT);
      break;
    case RPL_OPTION_TRANSIT:
      /* The path sequence and control are ignored. */
      /*      pathcontrol = buffer[i + 3];
              pathsequence = buffer[i + 4];*/
      lifetime = buffer[i + 5];
      if(len >= 20) {
        memcpy(&dao_parent_addr, buffer + i + 6, 16);
      }
      break;
    }
  }

  PRINTF("RPL: DAO lifetime: %u, prefix length: %u prefix: ",
          (unsigned)lifetime, (unsigned)prefixlen);
  PRINT6ADDR(&prefix);
  PRINTF(", parent: ");
  PRINT6ADDR(&dao_parent_addr);
  PRINTF(" \n");

  if(lifetime == RPL_ZERO_LIFETIME) {
    PRINTF("RPL: No-Path DAO received\n");
    rpl_ns_expire_parent(dag, &prefix, &dao_parent_addr);
  } else {
    if(rpl_ns_update_node(dag, &prefix, &dao_parent_addr, RPL_LIFETIME(instance, lifetime)) == NULL) {
      PRINTF("RPL: failed to add link\n");
      return;
    }
  }

  if(flags & RPL_DAO_K_FLAG) {
    PRINTF("RPL: Sending DAO ACK\n");
    uip_clear_buf();
    dao_ack_output(instance, &dao_sender_addr, sequence,
        RPL_DAO_ACK_UNCONDITIONAL_ACCEPT);
  }
#endif /* RPL_WITH_NON_STORING */
}
/*---------------------------------------------------------------------------*/
static void
dao_input(void)
{
  rpl_instance_t *instance;
  uint8_t instance_id;

  /* Destination Advertisement Object */
  PRINTF("RPL: Received a DAO from ");
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");

  instance_id = UIP_ICMP_PAYLOAD[0];
  instance = rpl_get_instance(instance_id);
  if(instance == NULL) {
    PRINTF("RPL: Ignoring a DAO for an unknown RPL instance(%u)\n",
           instance_id);
    goto discard;
  }

  if(RPL_IS_STORING(instance)) {
    dao_input_storing();
  } else if(RPL_IS_NON_STORING(instance)) {
    dao_input_nonstoring();
  }

 discard:
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
#if RPL_WITH_DAO_ACK
static void
handle_dao_retransmission(void *ptr)
{
  rpl_parent_t *parent;
  uip_ipaddr_t prefix;
  rpl_instance_t *instance;

  parent = ptr;
  if(parent == NULL || parent->dag == NULL || parent->dag->instance == NULL) {
    return;
  }
  instance = parent->dag->instance;

  if(instance->my_dao_transmissions >= RPL_DAO_MAX_RETRANSMISSIONS) {
    /* No more retransmissions - give up. */
    if(instance->lifetime_unit == 0xffff && instance->default_lifetime == 0xff) {
      /*
       * ContikiRPL was previously using infinite lifetime for routes
       * and no DAO_ACK configured. This probably means that the root
       * and possibly other nodes might be running an old version that
       * does not support DAO ack. Assume that everything is ok for
       * now and let the normal repair mechanisms detect any problems.
       */
      return;
    }

    if(RPL_IS_STORING(instance) && instance->of->dao_ack_callback) {
      /* Inform the objective function about the timeout. */
      instance->of->dao_ack_callback(parent, RPL_DAO_ACK_TIMEOUT);
    }

    /* Perform local repair and hope to find another parent. */
    rpl_local_repair(instance);
    return;
  }

  PRINTF("RPL: will retransmit DAO - seq:%d trans:%d\n", instance->my_dao_seqno,
	 instance->my_dao_transmissions);

  if(get_global_addr(&prefix) == 0) {
    return;
  }

  ctimer_set(&instance->dao_retransmit_timer,
             RPL_DAO_RETRANSMISSION_TIMEOUT / 2 +
             (random_rand() % (RPL_DAO_RETRANSMISSION_TIMEOUT / 2)),
	     handle_dao_retransmission, parent);

  instance->my_dao_transmissions++;
  dao_output_target_seq(parent, &prefix,
			instance->default_lifetime, instance->my_dao_seqno);
}
#endif /* RPL_WITH_DAO_ACK */
/*---------------------------------------------------------------------------*/
void
dao_output(rpl_parent_t *parent, uint8_t lifetime)
{
  /* Destination Advertisement Object */
  uip_ipaddr_t prefix;

  if(get_global_addr(&prefix) == 0) {
    PRINTF("RPL: No global address set for this node - suppressing DAO\n");
    return;
  }

  if(parent == NULL || parent->dag == NULL || parent->dag->instance == NULL) {
    return;
  }

  RPL_LOLLIPOP_INCREMENT(dao_sequence);
#if RPL_WITH_DAO_ACK
  /* set up the state since this will be the first transmission of DAO */
  /* retransmissions will call directly to dao_output_target_seq */
  /* keep track of my own sending of DAO for handling ack and loss of ack */
  if(lifetime != RPL_ZERO_LIFETIME) {
    rpl_instance_t *instance;
    instance = parent->dag->instance;

    instance->my_dao_seqno = dao_sequence;
    instance->my_dao_transmissions = 1;
    ctimer_set(&instance->dao_retransmit_timer, RPL_DAO_RETRANSMISSION_TIMEOUT,
 	       handle_dao_retransmission, parent);
  }
#else
   /* We know that we have tried to register so now we are assuming
      that we have a down-link - unless this is a zero lifetime one */
  parent->dag->instance->has_downward_route = lifetime != RPL_ZERO_LIFETIME;
#endif /* RPL_WITH_DAO_ACK */

  /* Sending a DAO with own prefix as target */
  dao_output_target(parent, &prefix, lifetime);
}
/*---------------------------------------------------------------------------*/
void
dao_output_target(rpl_parent_t *parent, uip_ipaddr_t *prefix, uint8_t lifetime)
{
  dao_output_target_seq(parent, prefix, lifetime, dao_sequence);
}
/*---------------------------------------------------------------------------*/
static void
dao_output_target_seq(rpl_parent_t *parent, uip_ipaddr_t *prefix,
		      uint8_t lifetime, uint8_t seq_no)
{
  rpl_dag_t *dag;
  rpl_instance_t *instance;
  unsigned char *buffer;
  uint8_t prefixlen;
  int pos;
  uip_ipaddr_t *parent_ipaddr = NULL;
  uip_ipaddr_t *dest_ipaddr = NULL;

  /* Destination Advertisement Object */

  /* If we are in feather mode, we should not send any DAOs */
  if(rpl_get_mode() == RPL_MODE_FEATHER) {
    return;
  }

  if(parent == NULL) {
    PRINTF("RPL dao_output_target error parent NULL\n");
    return;
  }

  parent_ipaddr = rpl_get_parent_ipaddr(parent);
  if(parent_ipaddr == NULL) {
    PRINTF("RPL dao_output_target error parent IP address NULL\n");
    return;
  }

  dag = parent->dag;
  if(dag == NULL) {
    PRINTF("RPL dao_output_target error dag NULL\n");
    return;
  }

  instance = dag->instance;

  if(instance == NULL) {
    PRINTF("RPL dao_output_target error instance NULL\n");
    return;
  }
  if(prefix == NULL) {
    PRINTF("RPL dao_output_target error prefix NULL\n");
    return;
  }
#ifdef RPL_DEBUG_DAO_OUTPUT
  RPL_DEBUG_DAO_OUTPUT(parent);
#endif

  buffer = UIP_ICMP_PAYLOAD;
  pos = 0;

  buffer[pos++] = instance->instance_id;
  buffer[pos] = 0;
#if RPL_DAO_SPECIFY_DAG
  buffer[pos] |= RPL_DAO_D_FLAG;
#endif /* RPL_DAO_SPECIFY_DAG */
#if RPL_WITH_DAO_ACK
  if(lifetime != RPL_ZERO_LIFETIME) {
    buffer[pos] |= RPL_DAO_K_FLAG;
  }
#endif /* RPL_WITH_DAO_ACK */
  ++pos;
  buffer[pos++] = 0; /* reserved */
  buffer[pos++] = seq_no;
#if RPL_DAO_SPECIFY_DAG
  memcpy(buffer + pos, &dag->dag_id, sizeof(dag->dag_id));
  pos+=sizeof(dag->dag_id);
#endif /* RPL_DAO_SPECIFY_DAG */

  /* create target subopt */
  prefixlen = sizeof(*prefix) * CHAR_BIT;
  buffer[pos++] = RPL_OPTION_TARGET;
  buffer[pos++] = 2 + ((prefixlen + 7) / CHAR_BIT);
  buffer[pos++] = 0; /* reserved */
  buffer[pos++] = prefixlen;
  memcpy(buffer + pos, prefix, (prefixlen + 7) / CHAR_BIT);
  pos += ((prefixlen + 7) / CHAR_BIT);

  /* Create a transit information sub-option. */
  buffer[pos++] = RPL_OPTION_TRANSIT;
  buffer[pos++] = (instance->mop != RPL_MOP_NON_STORING) ? 4 : 20;
  buffer[pos++] = 0; /* flags - ignored */
  buffer[pos++] = 0; /* path control - ignored */
  buffer[pos++] = 0; /* path seq - ignored */
  buffer[pos++] = lifetime;

  if(instance->mop != RPL_MOP_NON_STORING) {
    /* Send DAO to parent */
    dest_ipaddr = parent_ipaddr;
  } else {
    /* Include parent global IP address */
    memcpy(buffer + pos, &parent->dag->dag_id, 8); /* Prefix */
    pos += 8;
    memcpy(buffer + pos, ((const unsigned char *)parent_ipaddr) + 8, 8); /* Interface identifier */
    pos += 8;
    /* Send DAO to root */
    dest_ipaddr = &parent->dag->dag_id;
  }

  PRINTF("RPL: Sending a %sDAO with sequence number %u, lifetime %u, prefix ",
      lifetime == RPL_ZERO_LIFETIME ? "No-Path " : "", seq_no, lifetime);

  PRINT6ADDR(prefix);
  PRINTF(" to ");
  PRINT6ADDR(dest_ipaddr);
  PRINTF(" , parent ");
  PRINT6ADDR(parent_ipaddr);
  PRINTF("\n");

  if(dest_ipaddr != NULL) {
    uip_icmp6_send(dest_ipaddr, ICMP6_RPL, RPL_CODE_DAO, pos);
  }
}
/*---------------------------------------------------------------------------*/
static void
dao_ack_input(void)
{
#if RPL_WITH_DAO_ACK

  uint8_t *buffer;
  uint8_t instance_id;
  uint8_t sequence;
  uint8_t status;
  rpl_instance_t *instance;
  rpl_parent_t *parent;

  buffer = UIP_ICMP_PAYLOAD;

  instance_id = buffer[0];
  sequence = buffer[2];
  status = buffer[3];

  instance = rpl_get_instance(instance_id);
  if(instance == NULL) {
    uip_clear_buf();
    return;
  }

  if(RPL_IS_STORING(instance)) {
    parent = rpl_find_parent(instance->current_dag, &UIP_IP_BUF->srcipaddr);
    if(parent == NULL) {
      /* not a known instance - drop the packet and ignore */
      uip_clear_buf();
      return;
    }
  } else {
    parent = NULL;
  }

  PRINTF("RPL: Received a DAO %s with sequence number %d (%d) and status %d from ",
   status < 128 ? "ACK" : "NACK",
	 sequence, instance->my_dao_seqno, status);
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");

  if(sequence == instance->my_dao_seqno) {
    instance->has_downward_route = status < 128;

    /* always stop the retransmit timer when the ACK arrived */
    ctimer_stop(&instance->dao_retransmit_timer);

    /* Inform objective function on status of the DAO ACK */
    if(RPL_IS_STORING(instance) && instance->of->dao_ack_callback) {
      instance->of->dao_ack_callback(parent, status);
    }

#if RPL_REPAIR_ON_DAO_NACK
    if(status >= RPL_DAO_ACK_UNABLE_TO_ACCEPT) {
      /*
       * Failed the DAO transmission - need to remove the default route.
       * Trigger a local repair since we can not get our DAO in.
       */
      rpl_local_repair(instance);
    }
#endif

  } else if(RPL_IS_STORING(instance)) {
    /* this DAO ACK should be forwarded to another recently registered route */
    uip_ds6_route_t *re;
    uip_ipaddr_t *nexthop;
    if((re = find_route_entry_by_dao_ack(sequence)) != NULL) {
      /* pick the recorded seq no from that node and forward DAO ACK - and
         clear the pending flag*/
      RPL_ROUTE_CLEAR_DAO_PENDING(re);

      nexthop = uip_ds6_route_nexthop(re);
      if(nexthop == NULL) {
        PRINTF("RPL: No next hop to fwd DAO ACK to\n");
      } else {
        PRINTF("RPL: Fwd DAO ACK to:");
        PRINT6ADDR(nexthop);
        PRINTF("\n");
        buffer[2] = re->state.dao_seqno_in;
        uip_icmp6_send(nexthop, ICMP6_RPL, RPL_CODE_DAO_ACK, 4);
      }

      if(status >= RPL_DAO_ACK_UNABLE_TO_ACCEPT) {
        /* this node did not get in to the routing tables above... - remove */
        uip_ds6_route_rm(re);
      }
    } else {
      PRINTF("RPL: No route entry found to forward DAO ACK (seqno %u)\n", sequence);
    }
  }
#endif /* RPL_WITH_DAO_ACK */
  uip_clear_buf();
}
/*---------------------------------------------------------------------------*/
void
dao_ack_output(rpl_instance_t *instance, uip_ipaddr_t *dest, uint8_t sequence,
	       uint8_t status)
{
#if RPL_WITH_DAO_ACK
  unsigned char *buffer;

  PRINTF("RPL: Sending a DAO %s with sequence number %d to ", status < 128 ? "ACK" : "NACK", sequence);
  PRINT6ADDR(dest);
  PRINTF(" with status %d\n", status);

  buffer = UIP_ICMP_PAYLOAD;

  buffer[0] = instance->instance_id;
  buffer[1] = 0;
  buffer[2] = sequence;
  buffer[3] = status;

  uip_icmp6_send(dest, ICMP6_RPL, RPL_CODE_DAO_ACK, 4);
#endif /* RPL_WITH_DAO_ACK */
}
/*---------------------------------------------------------------------------*/
void
rpl_icmp6_register_handlers()
{
  uip_icmp6_register_input_handler(&dis_handler);
  uip_icmp6_register_input_handler(&dio_handler);
  uip_icmp6_register_input_handler(&dao_handler);
  uip_icmp6_register_input_handler(&dao_ack_handler);

#ifdef RPL_RESTORE_USE_UIDS
  //register icmp6 uid handlers
  uip_icmp6_register_input_handler(&uid_handler);
  uip_icmp6_register_input_handler(&uid_request_handler);
#endif
}
/*---------------------------------------------------------------------------*/

/** @}*/
