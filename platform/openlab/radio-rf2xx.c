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
#define NO_DEBUG_COLOR
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

static phy_packet_t          tx_pkt;
static void                  rf2xx_tx_done(phy_status_t status);
static volatile int          tx_pkt_pending;
static volatile phy_status_t tx_pkt_status;

enum rx_state
{
    RX_IDLE,
    RX_LISTEN,
    RX_PENDING,
    RX_ERROR,
    RX_READ,
};

static phy_packet_t           rx_pkt;
static void                   rf2xx_rx_done(phy_status_t status);
static volatile enum rx_state rx_state;
static volatile phy_status_t  rx_pkt_status;

static volatile int rf2xx_on;

PROCESS(rf2xx_process, "rf2xx driver");

static int rf2xx_wr_on(void);
static int rf2xx_wr_off(void);
static void rf2xx_rx_start(void);
static void rf2xx_reset(void);

/*---------------------------------------------------------------------------*/

static int
rf2xx_wr_init(void)
{
    log_error("rf2xx_wr_init (channel %u)", RF2XX_CHANNEL);

    rf2xx_on = 0;
    rx_state = RX_IDLE;
    tx_pkt_pending = 0;

    rf2xx_reset();
    process_start(&rf2xx_process, NULL);

    return 1;
}

/*---------------------------------------------------------------------------*/

/** Prepare the radio with a packet to be sent. */
static int
rf2xx_wr_prepare(const void *payload, unsigned short payload_len)
{
    log_debug("rf2xx_wr_prepare %d :: %d",payload_len,soft_timer_time());

    if (payload_len > PHY_MAX_TX_LENGTH)
    {
        log_error("payload is too big");
        tx_pkt.length = 0;
        return 1;
    }

    tx_pkt.data   = tx_pkt.raw_data;
    tx_pkt.length = payload_len;
    memcpy(tx_pkt.data, payload, tx_pkt.length);

    return 0;
}

/*---------------------------------------------------------------------------*/

/** Send the packet that has previously been prepared. */
static int
rf2xx_wr_transmit(unsigned short transmit_len)
{
    int ret;
    log_debug("rf2xx_wr_transmit %d :: %d\n", transmit_len, soft_timer_time());

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

    /* if we received a packet, abort sending */
    switch (rx_state)
    {
        case RX_LISTEN:
        case RX_IDLE:
            break;
        default:
            log_error("Tx colision");
            return RADIO_TX_COLLISION;
    }

    /* send... */
    phy_idle(phy);
    rx_state = RX_IDLE;
    tx_pkt_pending = 1;
    switch (phy_tx_now(phy, &tx_pkt, rf2xx_tx_done))
    {
        case PHY_SUCCESS:
            //log_printf("TX\n");
            while (tx_pkt_pending)
            {
                //clock_delay_usec(100);
            }
            ret = (tx_pkt_status == PHY_SUCCESS) ? RADIO_TX_OK : RADIO_TX_ERR;
            break;
        case PHY_ERR_INVALID_STATE:
        case PHY_ERR_INTERNAL:
            rf2xx_reset();
        default:
            log_error("Tx failed");
            tx_pkt_pending = 0;
            ret = RADIO_TX_ERR;
    }

    /* Eventually restart listening */
    if (rf2xx_on)
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
    if (rf2xx_wr_prepare(payload, payload_len))
    {
        return RADIO_TX_ERR;
    }
    return rf2xx_wr_transmit(payload_len);
}

/*---------------------------------------------------------------------------*/

/** Read a received packet into a buffer. */
static int
rf2xx_wr_read(void *buf, unsigned short buf_len)
{
    int len = 0;
    log_info("rf2xx_wr_read %d :: %d", buf_len, soft_timer_time());

    /* handle packet */
    if (rx_state == RX_PENDING)
    {
        rx_state = RX_READ;
        len = rx_pkt.length;
        if (buf_len < len)
        {
            len = buf_len;
        }
        memcpy(buf, rx_pkt.data, len);

        /* eventually restart listening */
        rx_state = RX_IDLE;
        if (rf2xx_on)
        {
            rf2xx_rx_start();
        }
    }

    return len;
}

/*---------------------------------------------------------------------------*/

/** Perform a Clear-Channel Assessment (CCA) to find out if there is
    a packet in the air or not. */
static int
rf2xx_wr_channel_clear(void)
{
    int32_t flag;
    phy_status_t result;

    log_info("rf2xx_wr_channel_clear :: %d", soft_timer_time());

    switch (rx_state)
    {
        case RX_LISTEN:
            flag = 0;
            // radio must be in idle before a cca
            result = phy_rx_busy(phy, &flag);
            //log_warning("bsy%d",flag);
            //if (flag)
            //{
            //    log_warning("bsy");
            //}
            flag = !flag;
            break;

        case RX_IDLE:
            flag = 1;
            result = phy_cca(phy,&flag);
            //if (!flag)
            //{
            //    log_warning("cca");
            //}
            //log_warning("cca%d",flag);
            break;

        default:
            // it seems we can't do cca too soon after rx
            // without breaking the radio
            return 1;
    }

    switch (result)
    {
        case PHY_SUCCESS:
            break;
        case PHY_ERR_INTERNAL:
            log_error("fatal error\n");
            rf2xx_reset();
        default:
            return 1;
    }

    flag = flag ? 1 : 0;
    return flag;
}

/*---------------------------------------------------------------------------*/

/** Check if the radio driver is currently receiving a packet */
static int
rf2xx_wr_receiving_packet(void)
{
    phy_status_t result;
    int32_t bsy = 0;
    log_debug("rf2xx_wr_receiving_packet : %d :: %d",rx_pkt_receiving,soft_timer_time());

    result = phy_rx_busy(phy, &bsy);
    return (result == PHY_SUCCESS) && bsy;
}

/*---------------------------------------------------------------------------*/

/** Check if the radio driver has just received a packet */
static int
rf2xx_wr_pending_packet(void)
{
    log_debug("rf2xx_wr_pending_packet : %d :: %d",rx_pkt_pending,soft_timer_time());
    return (rx_state == RX_PENDING);
}

/*---------------------------------------------------------------------------*/

/** Turn the radio on. */
static int
rf2xx_wr_on(void)
{
    log_info("rf2xx_wr_on :: %d",soft_timer_time());
    //log_printf("on\n");

    if (!rf2xx_on)
    {
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
    int stop = 0;
    log_info("rf2xx_wr_off :: %d",soft_timer_time());
    //log_printf("off\n");

    rf2xx_on = 0;
    platform_enter_critical();
    if (rx_state == RX_LISTEN)
    {
        rx_state = RX_IDLE;
        stop = 1;
    }
    platform_exit_critical();

    if (stop)
    {
        phy_idle(phy);
    }

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

static void rf2xx_reset(void)
{
    phy_reset(phy);
    phy_set_channel(phy, RF2XX_CHANNEL);
    phy_idle(phy);
}
static void rf2xx_tx_done(phy_status_t status)
{
    /*
     * Called in irq handler
     */
    //log_error("tx done");
    if (tx_pkt_pending)
    {
        tx_pkt_status = status;
        tx_pkt_pending = 0;
    }
}

/*---------------------------------------------------------------------------*/

static void rf2xx_rx_done(phy_status_t status)
{
    /*
     * Called in irq handler
     */
    //log_error("rx_done");
    if (rx_state != RX_LISTEN)
    {
        log_debug("rx_done but not listening");
        return;
    }

    // Check status
    rx_pkt_status = status;
    switch (status)
    {
        case PHY_SUCCESS:
            rx_state = RX_PENDING;
            //log_printf("RX\n");
            break;
        case PHY_RX_TIMEOUT_ERROR:
        case PHY_RX_CRC_ERROR:
        case PHY_RX_LENGTH_ERROR:
        default:
            log_debug("PHY RX error %x\n", status);
            rx_state = RX_ERROR;
            break;
    }
    //log_error("rx_done done");
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
    int start = 0;
    log_info("rf2xx_rx_start");

    platform_enter_critical();
    if (rf2xx_on && rx_state == RX_IDLE)
    {
        rx_state = RX_LISTEN;
        rx_pkt.data     = rx_pkt.raw_data;
        rx_pkt.length   = 0;
        start = 1;
    }
    platform_exit_critical();

    if (start)
    {
        phy_status_t status = phy_rx_now(phy, &rx_pkt, rf2xx_rx_done);
        switch (status)
        {
            case PHY_SUCCESS:
                break;
            default:
                rx_pkt_status = status;
                rx_state = RX_ERROR;
                log_error("Failed to start start listenning");
                process_poll(&rf2xx_process);
                break;
        }
    }
}

/*---------------------------------------------------------------------------*/

PROCESS_THREAD(rf2xx_process, ev, data)
{
    PROCESS_BEGIN();

    while(1)
    {
        PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);

        /*
         * at this point, we may be in any state
         *
         * this process can be concurrent to rtimer tasks
         * such as contikimac rdc listening task
         */

        switch (rx_state)
        {
            int len;
            case RX_PENDING:
                /* read packet */
                packetbuf_clear();
                len = rf2xx_wr_read(packetbuf_dataptr(), PACKETBUF_SIZE);

                /* give packet to netstack */
                if (len > 0)
                {
                    packetbuf_set_datalen(len);
                    /* this callback can do anything */
                    NETSTACK_RDC.input();
                }
                break;

            case RX_ERROR:

                /* need reset ? */
                switch (rx_pkt_status)
                {
                        case PHY_ERR_INTERNAL:
                        case PHY_ERR_INVALID_STATE:
                            rf2xx_reset();
                        default:
                            break;
                }

                /* eventually start listening */
                rx_state = RX_IDLE;
                if (rf2xx_on)
                {
                    rf2xx_rx_start();
                }
                break;

            default:
                break;
        }
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
