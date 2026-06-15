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
#define TX_MAX_RETRY            50   /* 50 × 10ms = 最多等 500ms 让芯片腾出 TX BD */

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
            return s_rx_handlers[i].cb; /* 精确匹配优先 */
        }
        if (s_rx_handlers[i].ip_addr == 0 && s_rx_handlers[i].port == 0)
        {
            wildcard_cb = s_rx_handlers[i].cb; /* 通配符备用 */
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

/* ---- 公开接口 ---- */

bool wifi_data_rx_register(uint32_t ip_addr, uint16_t port, T_WIFI_DATA_RX_CB cb)
{
    if (!cb)
    {
        return false;
    }
    /* 去重：相同 ip/port 已存在则更新 cb，避免重复占槽 */
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

    /* 整帧从 tx_desc 起按 512 对齐写入 SDIO，故缓冲需容纳
     * [p_next | 对齐后的(TXDESC + payload)]。用 offsetof 表达 p_next 占位，
     * 避免依赖“p_next 与 tx_desc 之间无 padding”的隐含假设。*/
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

#include "gui_stream.h"
typedef struct
{
    stp_transport_t *tp;
    uint8_t         *pool;
    uint32_t         pool_size;
    uint32_t         interval_ms;
    const char      *label;
    volatile bool    running;
} demo_stream_t;
extern demo_stream_t s_stream_bt;
void wifi_stream_cb(uint8_t *payload, uint16_t pkt_len)
{
    static uint32_t id = 0;
    static stp_frame_t f = {0};
    static uint8_t get_buff = false;
    static uint32_t cur_addr = 0;
    static uint32_t cur_len = 0;
    static uint32_t s_xfer_total = 0;
    static uint8_t skip = 0;



    uint32_t remain_len = pkt_len;
    uint32_t cp_len = 0;

    while (remain_len)
    {
        uint8_t *prec = payload + pkt_len - remain_len;
        if (!get_buff && !skip && prec[0] != 0xA5 && prec[1] != 0xA9)
        {
            printf("not stream frame %d 0x%x 0x%x\n", get_buff, prec[0], prec[1]);
            return;
        }

        if (!get_buff && !s_xfer_total && !skip)
        {
            s_xfer_total = *(uint32_t *)(&prec[2]);
            // printf("s_xfer_total %d\n", s_xfer_total);
            if (stp_acquire_free(s_stream_bt.tp, s_xfer_total, &f))
            {
                cur_addr = (uint32_t)f.addr;
                get_buff = true;
                id++;
                // printf("buffer get: 0x%x, s_xfer_total %d\n", cur_addr, s_xfer_total);
                prec += 6;
                remain_len -= 6;
            }
            else
            {
                printf("no transf buff avi\n");
                skip = 1;
                remain_len -= 6;
            }
        }

        if (get_buff || skip)
        {
            cp_len = ((s_xfer_total - cur_len) <= remain_len) ? (s_xfer_total - cur_len) : remain_len;
            if (!skip)
            {
                memcpy((void *)cur_addr, prec, cp_len);
            }
            cur_addr += cp_len;
            cur_len += cp_len;
            remain_len -= cp_len;
            // printf("buffer memcpy: 0x%x %d\n", prec, cp_len);
        }

        if ((get_buff || skip) && (cur_len == s_xfer_total))
        {
            if (!skip)
            {
                int rc = 0;
                rc = stp_commit(s_stream_bt.tp, &f, s_xfer_total, false);
                printf("buffer commit %u %d \n", s_xfer_total, rc);
                extern int cache_flush_by_addr(uint32_t *addr, uint32_t length);
                cache_flush_by_addr((uint32_t *)(cur_addr - s_xfer_total), s_xfer_total);
                // printf("buffer flush 0x%x-0x%x\n", cur_addr - s_xfer_total, cur_addr);
            }
            else
            {
                printf("skip\n");
            }

            skip = 0;
            get_buff = false;
            cur_addr = 0;
            s_xfer_total = 0;
            cur_len = 0;
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
    uint16_t offset  = rxdesc->offset; /* payload 相对帧头的偏移，由固件填写 */

    if (pkt_len == 0)
    {
        return;
    }

    /* RX 帧不携带 EXTDESC：payload 紧跟在 offset 字节的帧头之后，
     * 不能用 SIZE_RX_DESC(含 ext_desc) 作偏移，也无 ip/port 可路由，
     * 统一交给通配 handler(ip=0,port=0)。*/
    uint8_t *payload = buf + offset;

    T_WIFI_DATA_RX_CB cb = find_rx_cb(0, 0);
    if (cb)
    {
        cb(0, 0, payload, pkt_len);
    }
    else
    {
        printf("[data] no handler, pkt_len=%d\n", pkt_len);
        // printf("[data] 0x%x, 0x%x, 0x%x, 0x%x, 0x%x,\n", payload[0], payload[1], payload[2], payload[3], payload[4]);
        wifi_stream_cb(payload, pkt_len);
    }
}

void wifi_data_sdio_tx_handler(void)
{
    lazy_init();

    /* 同步把整条 TX 队列发完(参考 watch 实现)：BD 满则原地 os_delay 重试，
     * 带次数上限，避免长时间不喂狗触发看门狗复位。一次事件内排空，
     * 不再依赖异步 timer——后者会让 echo 滞留、堵塞单线程 wifi task。*/
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
            /* TX BD 暂时耗尽，等芯片腾出后重发当前包 */
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
            /* 否则继续 while，重发当前包 */
            continue;
        }

        /* 其他错误：丢弃当前包 */
        printf("[data] write frame error=%d, drop\n", ret);
        os_mem_aligned_free(os_queue_out(&s_tx_queue));
    }
}
