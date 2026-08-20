/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include "wifi_data.h"
#include "os_mem.h"
#include "os_queue.h"
#include "os_sched.h"
#include "wifi_sdio.h"
#include "wifi_types.h"
#include "wifi_task.h"
#include "wifi_desc.h"

#define WIFI_DATA_MAX_HANDLERS  4
#define TX_RETRY_DELAY_MS       10
#define TX_MAX_RETRY            50   /* 50 x 10ms = wait up to 500ms for chip to free TX BD */

typedef struct
{
    uint32_t          ip_addr;
    uint16_t          port;
    bool              used;
    T_WIFI_DATA_RX_CB cb;
} T_DATA_HANDLER;

static T_DATA_HANDLER s_rx_handlers[WIFI_DATA_MAX_HANDLERS];
static T_OS_QUEUE     s_tx_queue;
static bool           s_init_done  = false;
static uint8_t        s_tx_seq     = 0;

static void lazy_init(void)
{
    if (!s_init_done)
    {
        os_queue_init(&s_tx_queue);
        s_init_done = true;
    }
}

static T_WIFI_DATA_RX_CB find_rx_cb(uint32_t ip, uint16_t port)
{
    T_WIFI_DATA_RX_CB wildcard_cb = NULL;
    for (uint8_t i = 0; i < WIFI_DATA_MAX_HANDLERS; i++)
    {
        if (!s_rx_handlers[i].used) { continue; }
        if (s_rx_handlers[i].ip_addr == ip && s_rx_handlers[i].port == port)
        {
            return s_rx_handlers[i].cb; /* exact match preferred */
        }
        if (s_rx_handlers[i].ip_addr == 0 && s_rx_handlers[i].port == 0)
        {
            wildcard_cb = s_rx_handlers[i].cb; /* wildcard fallback */
        }
    }
    return wildcard_cb;
}

static void send_tx_drain_event(void)
{
    T_WIFI_MSG msg = {.event = EVENT_SDIO_TX_DRAIN};
    if (wifi_task_send_msg(&msg) == false)
    {
        printf("[data] tx drain msg fail\n");
    }
}

/* ---- Public API ---- */

bool wifi_data_rx_register(uint32_t ip_addr, uint16_t port, T_WIFI_DATA_RX_CB cb)
{
    if (!cb)
    {
        return false;
    }
    /* Dedup: if same ip/port exists, update cb to avoid slot waste */
    for (uint8_t i = 0; i < WIFI_DATA_MAX_HANDLERS; i++)
    {
        if (s_rx_handlers[i].used
            && s_rx_handlers[i].ip_addr == ip_addr
            && s_rx_handlers[i].port    == port)
        {
            s_rx_handlers[i].cb = cb;
            return true;
        }
    }
    for (uint8_t i = 0; i < WIFI_DATA_MAX_HANDLERS; i++)
    {
        if (!s_rx_handlers[i].used)
        {
            s_rx_handlers[i].ip_addr = ip_addr;
            s_rx_handlers[i].port    = port;
            s_rx_handlers[i].cb      = cb;
            s_rx_handlers[i].used    = true;
            return true;
        }
    }
    printf("[data] rx handler table full\n");
    return false;
}

bool wifi_data_rx_unregister(uint32_t ip_addr, uint16_t port)
{
    for (uint8_t i = 0; i < WIFI_DATA_MAX_HANDLERS; i++)
    {
        if (s_rx_handlers[i].used
            && s_rx_handlers[i].ip_addr == ip_addr
            && s_rx_handlers[i].port    == port)
        {
            memset(&s_rx_handlers[i], 0, sizeof(s_rx_handlers[i]));
            return true;
        }
    }
    return false;
}

bool wifi_data_tx(uint32_t ip_addr, uint16_t port, const uint8_t *data, uint16_t len)
{
    lazy_init();

    /* Entire frame written to SDIO aligned to 512 from tx_desc, so buffer must
     * hold [p_next | aligned(TXDESC + payload)]. Use offsetof for p_next so
     * we don't assume no padding between p_next and tx_desc. */
    uint32_t alloc_size = offsetof(T_WIFI_SDIO_WRITE_QUEUE, tx_desc)
                          + ((sizeof(TXDESC) + len + 511u) & ~511u);

    T_WIFI_SDIO_WRITE_QUEUE *pkt = os_mem_aligned_alloc(OS_MEM_TYPE_DATA, alloc_size, 4);
    if (!pkt)
    {
        printf("[data] tx alloc fail size=%u\n", alloc_size);
        return false;
    }

    pkt->tx_desc.offset           = SIZE_TX_DESC;
    pkt->tx_desc.bus_agg_num      = 1;
    pkt->tx_desc.type             = TX_PACKET_USER;
    pkt->tx_desc.txpktsize        = len;
    pkt->tx_desc.seq              = s_tx_seq;
    pkt->tx_desc.ext_desc.ip_addr = ip_addr;
    pkt->tx_desc.ext_desc.port    = port;
    pkt->tx_desc.ext_desc.seq     = s_tx_seq;
    s_tx_seq++;

    memcpy(pkt->data, data, len);
    os_queue_in(&s_tx_queue, pkt);

    send_tx_drain_event();
    return true;
}

#include "gui_stream.h"   /* stp_* transport API (stp_transport_t, stp_acquire_free, ...) */

/* The transport is owned by the app and created on the consumer side
 * (example_gui_stream.c).  The wifi RX path is the producer; fetch the shared
 * handle here.  Returns NULL until the stream app has initialised. */
extern stp_transport_t *app_stream_transport_get(void);
extern stp_transport_t *gui_stream_transport_get(void);
#pragma pack(push,1)
typedef struct
{
    uint16_t        mark;
    uint8_t         seq;
    uint16_t        len;
    uint8_t         check_sum;
} str_frame_t;
#pragma pack(pop)

void wifi_stream_cb(uint8_t *payload, uint16_t pkt_len)
{
    /* The transport is owned by the app (created in example_gui_stream.c). */
    stp_transport_t *tp = gui_stream_transport_get();
    if (!tp)
    {
        return;   /* transport not ready yet -> drop */
    }

    // static uint32_t id = 0;
    static stp_frame_t f = {0};
    static uint8_t get_buff = false;
    static uint32_t cur_addr = 0;
    static uint32_t cur_len = 0;
    static uint32_t s_xfer_total = 0;
    static uint8_t skip = 0;
    static str_frame_t hdr;
    static uint8_t cur_hdr = 0;



    uint32_t remain_len = pkt_len;
    uint32_t cp_len = 0;

    while (remain_len)
    {
        uint8_t *prec = payload + pkt_len - remain_len;
        uint8_t *pdata = NULL;

        if (!get_buff && !skip)
        {
            uint32_t remain_hdr = sizeof(str_frame_t) - cur_hdr;
            if (!cur_hdr)
            {
                // printf("clear hdr\n");
                memset((void *)&hdr, 0, sizeof(str_frame_t));
            }
            if (remain_hdr)
            {
                // extract header first
                // if(remain_hdr != sizeof(str_frame_t))
                // {
                //     printf("remain_len %d, remain_hdr %d\n", remain_len, remain_hdr);
                // }
                if (remain_len <= remain_hdr)
                {
                    // printf("remain_len %d, remain_hdr %d\n", remain_len, remain_hdr);
                    memcpy((void *)((uint8_t *)&hdr + cur_hdr), prec, remain_len);
                    cur_hdr += remain_len;
                    break;
                }
                else
                {
                    memcpy((void *)((uint8_t *)&hdr + cur_hdr), prec, remain_hdr);
                    remain_len -= remain_hdr;
                }
            }


            // header check
            if (hdr.mark != 0xA9A5)
            {
                printf("not stream frame 0x%x\n", hdr.mark);
                cur_hdr = 0;
                break;
            }

            uint8_t cnt = hdr.seq;
            uint8_t ck = hdr.check_sum;
            uint8_t ck_cal = hdr.seq + (hdr.len & 0xff) + (hdr.len >> 8);
            if (ck != ck_cal)
            {
                printf("0x%x 0x%x 0x%x  \n", cnt, ck_cal, ck);
            }
        }


        pdata = payload + pkt_len - remain_len;

        if (!get_buff && !s_xfer_total && !skip)
        {
            s_xfer_total = hdr.len;
            // printf("s_xfer_total %d\n", s_xfer_total);

            if (stp_acquire_free(tp, s_xfer_total, &f))
            {
                cur_addr = (uint32_t)f.addr;
                get_buff = true;
                // id++;
                // printf("buffer get: 0x%x, s_xfer_total %d\n", cur_addr, s_xfer_total);
            }
            else
            {
                printf("no transf buff avi %u\n", s_xfer_total);
                // printf("[data] 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,0x%x,0x%x,0x%x,\n", prec[0], prec[1], prec[2], prec[3], prec[4], prec[5], prec[6], prec[7]);
                skip = 1;
            }
        }

        if (get_buff || skip)
        {
            cp_len = ((s_xfer_total - cur_len) <= remain_len) ? (s_xfer_total - cur_len) : remain_len;
            if (!skip)
            {
                memcpy((void *)cur_addr, pdata, cp_len);
                cur_addr += cp_len;
            }

            cur_len += cp_len;
            remain_len -= cp_len;
            // printf("buffer memcpy: 0x%x %d\n", prec, cp_len);
        }

        if ((get_buff || skip) && (cur_len == s_xfer_total))
        {
            if (!skip)
            {
                int rc = 0;
                rc = stp_commit(tp, &f, s_xfer_total, false);
                printf("commit %u %d \n", s_xfer_total, rc);

                extern void ui_jump_streaming(void);
                ui_jump_streaming();

                extern int cache_flush_by_addr(uint32_t *addr, uint32_t length);
                cache_flush_by_addr((uint32_t *)(cur_addr - s_xfer_total), s_xfer_total);
                // printf("buffer flush 0x%x-0x%x\n", cur_addr - s_xfer_total, cur_addr);
            }
            else
            {
                printf("skip %u\n", s_xfer_total);
            }

            skip = 0;
            get_buff = false;
            cur_addr = 0;
            s_xfer_total = 0;
            cur_len = 0;
            cur_hdr = 0;
        }
    }

}


void wifi_data_sdio_rx_handler(void)
{
    uint16_t buf_size = wifi_sdio_get_read_buf_size();
    uint8_t *buf      = wifi_sdio_get_read_buf();

    if (!wifi_sdio_read_frame(buf, &buf_size))
    {
        return;
    }

    PRXDESC  rxdesc  = (PRXDESC)buf;
    uint16_t pkt_len = rxdesc->pkt_len;
    uint16_t offset  = rxdesc->offset; /* payload offset relative to frame header, filled by firmware */

    if (pkt_len == 0)
    {
        return;
    }

    /* RX frame carries no EXTDESC: payload follows right after the offset-byte
     * frame header, so SIZE_RX_DESC (which includes ext_desc) cannot be used
     * as offset; no ip/port to route on either, unified dispatch to wildcard
     * handler (ip=0,port=0). */
    uint8_t *payload = buf + offset;

    T_WIFI_DATA_RX_CB cb = find_rx_cb(0, 0);
    if (cb)
    {
        cb(0, 0, payload, pkt_len);
    }
    else
    {
        // return;
        printf("[data] pkt_len=%d\n", pkt_len);
        // printf("[data] no handler, pkt_len=%d\n", pkt_len);
        // printf("[data] 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,\n", payload[0], payload[1], payload[2], payload[3], payload[4]);
        wifi_stream_cb(payload, pkt_len);
    }
}

void wifi_data_sdio_tx_handler(void)
{
    lazy_init();

    /* Synchronously drain the entire TX queue (see watch implementation): if
     * BD is full, retry with os_delay in place, capped to avoid watchdog
     * reset. Drain within one event, no longer relying on async timer — the
     * latter would stall echo and block the single-threaded wifi task. */
    while (s_tx_queue.count > 0)
    {
        T_WIFI_SDIO_WRITE_QUEUE *pkt = os_queue_peek(&s_tx_queue, 0);
        if (!pkt)
        {
            break;
        }

        int ret = wifi_sdio_write_frame(pkt);
        if (ret == 0)
        {
            os_mem_aligned_free(os_queue_out(&s_tx_queue));
            continue;
        }

        if (ret == -EAGAIN)
        {
            /* TX BD temporarily exhausted, wait for chip to free up then retry */
            uint16_t retry = 0;
            while (wifi_sdio_tx_bd_avail() == 0 && retry < TX_MAX_RETRY)
            {
                os_delay(TX_RETRY_DELAY_MS);
                retry++;
            }
            if (retry >= TX_MAX_RETRY)
            {
                printf("[data] tx bd starved, drop 1 pkt\n");
                os_mem_aligned_free(os_queue_out(&s_tx_queue));
            }
            /* otherwise loop back to retransmit the current packet */
            continue;
        }

        /* other errors: drop the current packet */
        printf("[data] write frame error=%d, drop\n", ret);
        os_mem_aligned_free(os_queue_out(&s_tx_queue));
    }
}
