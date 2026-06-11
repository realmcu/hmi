/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>
#include "os_mem.h"
#include "os_sync.h"
#include "wifi_uart.h"
#include "wifi_types.h"
#include "wifi_task.h"

#define RX_DMA_BUF_LEN  256   /* DMA 单次搬运量，~22ms@115200，ring buffer 始终有足够余量 */
#define RX_BUF_LEN      2048  /* ring buffer，足够容纳一次完整扫描响应 (~2KB) */
#define TX_BUF_LEN      128

static const struct device *const uart_dev = DEVICE_DT_GET(DT_ALIAS(uart3));

typedef struct
{
    uint16_t read_idx;
    uint16_t write_idx;
    uint16_t rx_cnt;
    uint8_t  buf[RX_BUF_LEN];
    bool     tx_lock;
    uint8_t  tx_buf[TX_BUF_LEN];
} T_RING_BUF;

typedef struct
{
    T_WIFI_UART_MODE mode;
    uint32_t         baud;
    void            *mp_sem_handle;
    uint8_t          buf_index;
    uint8_t          rx_dma_buf[RX_DMA_BUF_LEN * 2];
} T_WIFI_UART_SET;

static T_RING_BUF      s_ring;
static T_WIFI_UART_SET s_uart = {.mode = WIFI_UART_MODE_AT, .baud = 115200};

/* ---- ring buffer helpers ---- */

static void ring_buf_write(const uint8_t *data, uint16_t len)
{
    if (s_ring.rx_cnt + len > RX_BUF_LEN)
    {
        printf("[uart] ring buf overflow cnt=%d len=%d\n", s_ring.rx_cnt, len);
        return;
    }

    if (s_ring.write_idx + len <= RX_BUF_LEN)
    {
        memcpy(&s_ring.buf[s_ring.write_idx], data, len);
        s_ring.write_idx += len;
        if (s_ring.write_idx == RX_BUF_LEN)
        {
            s_ring.write_idx = 0;
        }
    }
    else
    {
        uint16_t tail = RX_BUF_LEN - s_ring.write_idx;
        memcpy(&s_ring.buf[s_ring.write_idx], data, tail);
        memcpy(&s_ring.buf[0], data + tail, len - tail);
        s_ring.write_idx = len - tail;
    }
    s_ring.rx_cnt += len;
}

static uint16_t ring_buf_read(uint8_t *buf, uint16_t max_len)
{
    unsigned int key = irq_lock();
    uint16_t read_len = s_ring.rx_cnt;
    irq_unlock(key);

    if (read_len == 0)
    {
        return 0;
    }
    if (read_len > max_len)
    {
        read_len = max_len;
    }

    if (s_ring.read_idx + read_len <= RX_BUF_LEN)
    {
        memcpy(buf, &s_ring.buf[s_ring.read_idx], read_len);
        s_ring.read_idx += read_len;
        if (s_ring.read_idx == RX_BUF_LEN)
        {
            s_ring.read_idx = 0;
        }
    }
    else
    {
        uint16_t tail = RX_BUF_LEN - s_ring.read_idx;
        memcpy(buf, &s_ring.buf[s_ring.read_idx], tail);
        memcpy(buf + tail, &s_ring.buf[0], read_len - tail);
        s_ring.read_idx = read_len - tail;
    }

    key = irq_lock();
    s_ring.rx_cnt -= read_len;
    irq_unlock(key);

    return read_len;
}

/* ---- async callback (参考 watch wifi_uart.c) ---- */

static void uart_async_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    switch (evt->type)
    {
    case UART_TX_DONE:
        s_ring.tx_lock = false;
        break;

    case UART_RX_RDY:
        ring_buf_write(&evt->data.rx.buf[evt->data.rx.offset], evt->data.rx.len);

        if (s_uart.mode == WIFI_UART_MODE_AT)
        {
            T_WIFI_MSG msg = {.event = EVENT_UART_RX};
            if (wifi_task_send_msg(&msg) == false)
            {
                printf("[uart] EVENT_UART_RX send fail\n");
            }
        }
        else if (s_uart.mode == WIFI_UART_MODE_MP)
        {
            if (os_sem_give(s_uart.mp_sem_handle) == false)
            {
                printf("[uart] mp sem_give fail\n");
            }
        }
        break;

    case UART_RX_BUF_REQUEST:
        s_uart.buf_index ^= 1;
        uart_rx_buf_rsp(uart_dev, &s_uart.rx_dma_buf[s_uart.buf_index * RX_DMA_BUF_LEN], RX_DMA_BUF_LEN);
        break;

    default:
        break;
    }
}

/* ---- 公开接口 ---- */

int wifi_uart_init(void)
{
    if (!device_is_ready(uart_dev))
    {
        printf("[uart] device not ready\n");
        return -ENODEV;
    }

    memset(&s_ring, 0, sizeof(s_ring));

    if (os_sem_create(&s_uart.mp_sem_handle, "mp_sem", 0, 1) == false)
    {
        printf("[uart] mp sem create fail\n");
        return -ENOMEM;
    }

    uart_callback_set(uart_dev, uart_async_cb, NULL);
    s_uart.buf_index = 0;
    int ret = uart_rx_enable(uart_dev, s_uart.rx_dma_buf, RX_DMA_BUF_LEN, 0);
    if (ret != 0)
    {
        printf("[uart] uart_rx_enable fail ret=%d\n", ret);
        return ret;
    }
    printf("[uart] init OK, DMA RX enabled\n");
    return 0;
}

int wifi_uart_set_mode(T_WIFI_UART_MODE mode)
{
    s_uart.mode = mode;
    uart_rx_disable(uart_dev);
    s_uart.buf_index = 0;
    memset(&s_ring, 0, sizeof(s_ring));
    uart_rx_enable(uart_dev, s_uart.rx_dma_buf, RX_DMA_BUF_LEN, 0);
    printf("[uart] set_mode %d\n", mode);
    return 0;
}

int wifi_uart_set_baudrate(uint32_t baud)
{
    struct uart_config cfg;
    uart_config_get(uart_dev, &cfg);
    cfg.baudrate = baud;
    int ret = uart_configure(uart_dev, &cfg);
    if (ret == 0)
    {
        s_uart.baud = baud;
    }
    uart_rx_disable(uart_dev);
    s_uart.buf_index = 0;
    uart_rx_enable(uart_dev, s_uart.rx_dma_buf, RX_DMA_BUF_LEN, 0);
    return ret;
}

bool wifi_uart_tx(const uint8_t *data, uint16_t len)
{
    if (len > TX_BUF_LEN)
    {
        printf("[uart] tx too large len=%d max=%d\n", len, TX_BUF_LEN);
        return false;
    }

    /* 忙等上一笔发送完成再抢锁(原子 test-and-set)。
     * tx_lock 由 UART_TX_DONE 中断异步清除；connect 的 ATW0/ATW1/ATWC 三条
     * 在 wifi task 内几乎无间隔连发，若不等待则后两条会被直接拒发、丢命令。
     * 最多等 ~100ms 兜底，避免 TX 卡死时长时间阻塞 task。*/
    uint16_t wait_ms = 0;
    for (;;)
    {
        unsigned int key = irq_lock();
        if (!s_ring.tx_lock)
        {
            s_ring.tx_lock = true;
            irq_unlock(key);
            break;
        }
        irq_unlock(key);
        if (++wait_ms > 100)
        {
            printf("[uart] tx busy, give up\n");
            return false;
        }
        k_msleep(1);
    }

    memcpy(s_ring.tx_buf, data, len);

    int ret = uart_tx(uart_dev, s_ring.tx_buf, len, SYS_FOREVER_US);
    if (ret != 0)
    {
        /* 发送启动失败不会有 UART_TX_DONE，立即释放锁，否则通道永久死锁 */
        printf("[uart] uart_tx fail ret=%d\n", ret);
        s_ring.tx_lock = false;
        return false;
    }
    return true;
}

uint16_t wifi_uart_rx_read(uint8_t *buf, uint16_t max_len)
{
    return ring_buf_read(buf, max_len);
}

uint16_t wifi_uart_rx_avail(void)
{
    unsigned int key = irq_lock();
    uint16_t cnt = s_ring.rx_cnt;
    irq_unlock(key);
    return cnt;
}

uint16_t wifi_uart_mp_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    memset(buf, 0, max_len);
    if (os_sem_take(s_uart.mp_sem_handle, timeout_ms) == false)
    {
        return 0;
    }
    return ring_buf_read(buf, max_len);
}
