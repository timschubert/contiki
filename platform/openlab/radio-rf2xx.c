/*
 * This file is part of HiKoB Openlab.
 *
 * HiKoB Openlab is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, version 3.
 *
 * HiKoB Openlab is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with HiKoB Openlab. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2011,2012 HiKoB.
 */

/**
 * \file radio-rf2xx.c
 *         Configuration for HiKoB OpenLab platforms
 *         This file contains wrappers around the OpenLab phy layer
 *
 * \author
 *         Antoine Fraboulet <antoine.fraboulet.at.hikob.com>
 *         
 */

#include <stdlib.h>
#include <string.h>

#include "platform.h"
#define NO_DEBUG_HEADER
#define LOG_LEVEL LOG_LEVEL_WARNING
#include "debug.h"
#include "event.h"
#include "phy.h"

#include "contiki.h"
#include "contiki-net.h"

/*---------------------------------------------------------------------------*/

#ifndef RF2XX_CHANNEL
#define RF2XX_CHANNEL   11
#endif
#ifndef RX2XX_TX_POWER
#define RF2XX_TX_POWER  PHY_POWER_0dBm
#endif

static phy_packet_t     tx_pkt;
static void             rf2xx_tx_done(phy_status_t status);
static volatile int     tx_pkt_pending;

static phy_packet_t     rx_pkt;
static void             rf2xx_rx_done(phy_status_t status);
static volatile int     rx_pkt_pending;

static volatile int     rf2xx_on;
static volatile int     rf2xx_listening;

PROCESS(rf2xx_process, "rf2xx driver");

static int rf2xx_wr_on(void);
static int rf2xx_wr_off(void);
static void rf2xx_rx_start(void);

/*---------------------------------------------------------------------------*/

static int 
rf2xx_wr_init(void)
{
    log_debug("rf2xx_wr_init (channel %u)", RF2XX_CHANNEL);

    tx_pkt_pending   = 0;
    rx_pkt_pending   = 0;

    rf2xx_on = 0;
    rf2xx_listening  = 0;

    phy_reset(phy);
    phy_set_channel(phy, RF2XX_CHANNEL);
    process_start(&rf2xx_process, NULL);

    return 1;
}
  
/*---------------------------------------------------------------------------*/

/** Prepare the radio with a packet to be sent. */
static int 
rf2xx_wr_prepare(const void *payload, unsigned short payload_len)
{
    log_debug("rf2xx_wr_prepare %d :: %d",payload_len,soft_timer_time());

    tx_pkt.data   = tx_pkt.raw_data;
    tx_pkt.length = payload_len;
    memcpy(tx_pkt.data, payload, tx_pkt.length);

    return 1;
}

/*---------------------------------------------------------------------------*/

/** Send the packet that has previously been prepared. */
static int 
rf2xx_wr_transmit(unsigned short transmit_len)
{
    int ret;
    log_info("rf2xx_wr_transmit %d :: %d", transmit_len, soft_timer_time());

    if (tx_pkt_pending == 1)
    {
	log_error("    Tx too early, another tx still active");
	return RADIO_TX_ERR;
    }

    if (tx_pkt.length != transmit_len)
    {
	log_error("    pkt.length (%d) != transmit_len (%d) !", tx_pkt.length, transmit_len);
    }

// TODO:  if(packetbuf_attr(PACKETBUF_ATTR_RADIO_TXPOWER) > 0) {

    phy_idle(phy);

    tx_pkt_pending = 1;
    ret = RADIO_TX_OK;
    if (phy_tx_now(phy, &tx_pkt, rf2xx_tx_done) != PHY_SUCCESS)
    {
        tx_pkt_pending = 0;
        ret = RADIO_TX_ERR;
    }
    
    while (tx_pkt_pending)
    {
	clock_delay_usec(100);
    }

    /*
     *  Restart listening only if no packet is pending
     */
    if (rf2xx_listening && !rx_pkt_pending)
    {
        rf2xx_rx_start();
    }

    return ret;
}

/*---------------------------------------------------------------------------*/

/** Prepare & transmit a packet. */
static int 
rf2xx_wr_send(const void *payload, unsigned short payload_len)
{
    log_debug("rf2xx_wr_send %d :: %d", payload_len,soft_timer_time());
    rf2xx_wr_prepare(payload, payload_len);
    return rf2xx_wr_transmit(payload_len);
}

/*---------------------------------------------------------------------------*/

/** Read a received packet into a buffer. */
static int 
rf2xx_wr_read(void *buf, unsigned short buf_len)
{
    int len = rx_pkt.length;

    log_info("rf2xx_wr_read %d (avail %d) :: %d", buf_len, len, soft_timer_time());

    if (buf_len < len)
    {
	len = buf_len;
    }

    memcpy(packetbuf_dataptr(), rx_pkt.data, len);
    return len;
}

/*---------------------------------------------------------------------------*/

/** Perform a Clear-Channel Assessment (CCA) to find out if there is
    a packet in the air or not. */
static int 
rf2xx_wr_channel_clear(void)
{
    int32_t cca;
    phy_status_t result;

    log_debug("rf2xx_wr_channel_clear :: %d",soft_timer_time());

    // radio must be in idle before a cca for phy_cca(phy)
    // TODO: implement a different method that handles radio in Rx state.
    phy_idle(phy);
    result = phy_cca(phy,&cca);
    return (result == PHY_SUCCESS) && (cca == 1);
}

/*---------------------------------------------------------------------------*/

/** Check if the radio driver is currently receiving a packet */
static int 
rf2xx_wr_receiving_packet(void)
{
    log_debug("rf2xx_wr_receiving_packet : %d :: %d",rx_pkt_receiving,soft_timer_time());
    /*
     * actually we don't know if we'are receiving
     */
    return 0;
}

/*---------------------------------------------------------------------------*/

/** Check if the radio driver has just received a packet */
static int
rf2xx_wr_pending_packet(void)
{
    log_debug("rf2xx_wr_pending_packet : %d :: %d",rx_pkt_pending,soft_timer_time());
    return (rx_pkt_pending > 0) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/

/** Turn the radio on. */
static int 
rf2xx_wr_on(void)
{
    log_info("rf2xx_wr_on :: %d",soft_timer_time());

    if (!rf2xx_on)
    {
        // Start RX now
        rf2xx_on = 1;
        rf2xx_rx_start();
    }

    return 1;
}

/*---------------------------------------------------------------------------*/

/** Turn the radio off. */
static int
rf2xx_wr_off(void)
{
    log_info("rf2xx_wr_off :: %d",soft_timer_time());

    rf2xx_on = 0;
    rf2xx_listening = 0;
    rf2xx_dig2_disable(phy);
    phy_idle(phy);

    return 1;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

const struct radio_driver rf2xx_driver =
  {
    .init             = rf2xx_wr_init,
    .prepare          = rf2xx_wr_prepare,
    .transmit         = rf2xx_wr_transmit,
    .send             = rf2xx_wr_send,
    .read             = rf2xx_wr_read,
    .channel_clear    = rf2xx_wr_channel_clear,
    .receiving_packet = rf2xx_wr_receiving_packet,
    .pending_packet   = rf2xx_wr_pending_packet,
    .on               = rf2xx_wr_on,
    .off              = rf2xx_wr_off,
 };

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

static void rf2xx_tx_done(phy_status_t status)
{
    /*
     * Called in irq handler
     */
    tx_pkt_pending = 0;
    if (status == PHY_SUCCESS)
    {
        log_debug("Frame sent at %u, length: %u", tx_pkt.timestamp, tx_pkt.length);
    }
    else
    {
        log_debug("Error while sending %x", status);
    }
}

/*---------------------------------------------------------------------------*/

static void rf2xx_rx_done(phy_status_t status)
{
    /*
     * Called in irq handler
     */
    if (!rf2xx_on)
    {
        log_debug("rf2xx_rx_done but not on");
        return;
    }

    // Check status
    switch (status)
    {
        case PHY_SUCCESS:
	    rx_pkt_pending = 1;
            log_debug("PHY Rx ok");
            break;
        case PHY_RX_TIMEOUT_ERROR:
        case PHY_RX_CRC_ERROR:
        case PHY_RX_LENGTH_ERROR:
        default:
	    rx_pkt_pending = -1;
            log_debug("PHY RX error %x\n", status);
            break;
    }
    process_poll(&rf2xx_process);
}

/*---------------------------------------------------------------------------*/

event_status_t event_post_from_isr(event_queue_t queue, handler_t event, handler_arg_t arg)
{
    event(arg);
    return EVENT_OK;
}

/*---------------------------------------------------------------------------*/

static void rf2xx_rx_start(void)
{
    log_info("rf2xx_rx_start");
    rx_pkt.data     = rx_pkt.raw_data;
    rx_pkt.length   = 0;
    rf2xx_listening = 1;

    if (phy_rx_now(phy, &rx_pkt, rf2xx_rx_done) != PHY_SUCCESS)
    {
        rf2xx_listening = 0;
        log_error("Failed to start start listenning");
    }
}

/*---------------------------------------------------------------------------*/

PROCESS_THREAD(rf2xx_process, ev, data)
{
    int len;

    PROCESS_BEGIN();

    while(1) 
    {
	PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);

        /*
         * indicate we are not listening anymore
         */
        rf2xx_listening = 0;
        log_info("read done");
	if (rx_pkt_pending > 0)
	{
	    rx_pkt_pending = 0;
	    packetbuf_clear();

           /* give packet to netstack */
	    if (rx_pkt.length > 0)
	    {
		len = rf2xx_wr_read(packetbuf_dataptr(), PACKETBUF_SIZE);
		packetbuf_set_datalen(len);
		NETSTACK_RDC.input();
	    }
	}
        else
        {
            rx_pkt_pending = 0;
        }

        /*
         * resume listening here if still on
         * check also if listening because
         * RDC callback could have restarted it
         */
        if (rf2xx_on && !rf2xx_listening)
        {
            rf2xx_rx_start();
        }
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
