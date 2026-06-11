/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdio.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "wifi_sdio.h"
#include "wifi_types.h"
#include "wifi_task.h"

/* HISR 报了 RX_REQUEST 但 RX0_REQ_LEN 仍为 0 时的最大轮询次数，
 * 防止固件异常导致 wifi task 在此死循环、长时间不喂狗触发看门狗复位 */
#define SDIO_RX_LEN_POLL_MAX  10

static const struct device *sdhc_dev = DEVICE_DT_GET(DT_ALIAS(sdhc0));
static const struct gpio_dt_spec wifi_int = GPIO_DT_SPEC_GET(DT_ALIAS(wifi_int), gpios);
static struct sdio_func     wifi_sdio_func;
static struct gpio_callback wifi_int_cb_data;
static struct sd_card       card;

static uint32_t s_read_buf[4096 / 4];
static uint16_t s_tx_bd_num = 0;

/* ---- 内部工具 ---- */

static uint16_t sdio_tx_bd_read(void)
{
    uint8_t  bd[2] = {0};
    uint32_t reg;
    reg = (SDIO_LOCAL_DEVICE_ID << 13) | (SDIO_REG_FREE_TXBD_NUM + 1);
    sdio_read_byte(&wifi_sdio_func, reg, &bd[1]);
    reg = (SDIO_LOCAL_DEVICE_ID << 13) | SDIO_REG_FREE_TXBD_NUM;
    sdio_read_byte(&wifi_sdio_func, reg, &bd[0]);
    return (uint16_t)((bd[1] << 8) | bd[0]);
}

/* ---- 公开接口 ---- */

int wifi_sdio_init(void)
{
    if (!device_is_ready(sdhc_dev))
    {
        printf("[sdio] device not ready\n");
        return -ENODEV;
    }

    int ret = sd_init(sdhc_dev, &card);
    if (ret)
    {
        printf("[sdio] sd_init ret=%d\n", ret);
        return ret;
    }

    ret = sdio_init_func(&card, &wifi_sdio_func, 1);
    if (ret)
    {
        printf("[sdio] sdio_init_func ret=%d\n", ret);
        return ret;
    }

    ret = sdio_set_block_size(&wifi_sdio_func, wifi_sdio_func.cis.max_blk_size);
    if (ret)
    {
        printf("[sdio] set_block_size failed\n");
        return ret;
    }

    uint8_t  rv  = 0;
    uint32_t rr  = 0;
    uint32_t wd  = 0xFFFFFFFF;

    sdio_write_byte(&card.func0, 0x110, 0x00);
    sdio_write_byte(&card.func0, 0x111, 0x02);
    sdio_read_byte(&card.func0, SDIO_REG_STATIS_RECOVERY_TIMOUT, &rv);
    sdio_write_byte(&card.func0, SDIO_REG_STATIS_RECOVERY_TIMOUT, 0x02);
    sdio_read_byte(&card.func0, 0x03, &rv);
    sdio_write_byte(&card.func0, 0x110, 0x00);
    sdio_write_byte(&card.func0, 0x111, 0x02);
    sdio_read_byte(&wifi_sdio_func, SDIO_REG_CPU_IND, &rv);

    sdio_read_addr(&wifi_sdio_func, SDIO_REG_FREE_TXBD_NUM, (uint8_t *)&rr, 4);

    sdio_write_byte(&wifi_sdio_func, SDIO_REG_AVAI_BD_NUM_TH_L, 0x16);
    sdio_write_byte(&wifi_sdio_func, 0xD1, 0x00);
    sdio_write_byte(&wifi_sdio_func, SDIO_REG_AVAI_BD_NUM_TH_H, 0x0b);
    sdio_write_byte(&wifi_sdio_func, 0xD5, 0x00);

    sdio_write_addr(&wifi_sdio_func, SDIO_REG_HISR, (uint8_t *)&wd, 4);
    wd = 0;
    sdio_write_addr(&wifi_sdio_func, SDIO_REG_HIMR, (uint8_t *)&wd, 4);

    sdio_read_byte(&wifi_sdio_func, SDIO_REG_FREE_TXBD_NUM, &rv);
    sdio_read_byte(&wifi_sdio_func, SDIO_REG_TXBUF_UNIT_SZ, &rv);

    sdio_read_byte(&card.func0, SDIO_REG_32K_TRANS_IDLE_TIME, &rv);
    sdio_write_byte(&card.func0, SDIO_REG_32K_TRANS_IDLE_TIME, 0x03);

    wd = 0x40001;
    sdio_write_addr(&wifi_sdio_func, SDIO_REG_HIMR, (uint8_t *)&wd, 4);

    s_tx_bd_num = sdio_tx_bd_read();
    printf("[sdio] init ok tx_bd_num=%d\n", s_tx_bd_num);

    wifi_sdio_int_init();
    return 0;
}

void wifi_sdio_int_init(void)
{
    if (!device_is_ready(wifi_int.port))
    {
        printf("[sdio] int pin not ready\n");
        return;
    }
    if (gpio_pin_configure_dt(&wifi_int, GPIO_INPUT | GPIO_PULL_UP))
    {
        printf("[sdio] configure int pin failed\n");
        return;
    }
    if (gpio_pin_interrupt_configure_dt(&wifi_int, GPIO_INT_EDGE_TO_ACTIVE))
    {
        printf("[sdio] configure int irq failed\n");
        return;
    }
    gpio_init_callback(&wifi_int_cb_data, wifi_sdio_isr, BIT(wifi_int.pin));
    gpio_add_callback(wifi_int.port, &wifi_int_cb_data);
}

void wifi_sdio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    T_WIFI_MSG msg = {.event = EVENT_SDIO_INT};
    if (wifi_task_send_msg(&msg) == false)
    {
        printf("[sdio] int msg send fail\n");
    }
}

bool wifi_sdio_read_frame(uint8_t *buf, uint16_t *size)
{
    bool     ret       = false;
    static uint32_t fifo_cnt = 0;
    uint32_t sdio_hisr = 0;
    uint32_t rx_len    = 0;

    sdio_read_addr(&wifi_sdio_func, SDIO_REG_HISR, (uint8_t *)&sdio_hisr, 4);
    sdio_read_addr(&wifi_sdio_func, SDIO_REG_RX0_REQ_LEN, (uint8_t *)&rx_len, 4);

    rx_len &= 0xFFFF;
    rx_len = ((rx_len >> 2) + ((rx_len & 3) ? 1 : 0)) << 2; /* 4-byte align */

    if (rx_len > *size)
    {
        printf("[sdio] rx_len=%u > buf=%u, skip\n", (unsigned int)rx_len, (unsigned int)*size);
        return ret;
    }

    uint32_t himr = SDIO_HIMR_RX_REQUEST_MSK | SDIO_HIMR_AVAL_MSK | SDIO_HIMR_CPWM1_MSK;
    if (sdio_hisr & himr)
    {
        uint32_t v32 = sdio_hisr & himr & MASK_SDIO_HISR_CLEAR;
        if (v32)
        {
            sdio_write_addr(&wifi_sdio_func, SDIO_REG_HISR, (uint8_t *)&v32, 4);
        }
    }

    if (sdio_hisr & SDIO_HISR_AVAL_INT)
    {
        uint32_t fp;
        sdio_read_addr(&wifi_sdio_func, SDIO_REG_FREE_TXBD_NUM, (uint8_t *)&fp, 4);
        T_WIFI_MSG msg = {.event = EVENT_SDIO_TX_DRAIN};
        wifi_task_send_msg(&msg);
    }

    if (sdio_hisr & SDIO_HISR_RX_REQUEST)
    {
        uint16_t poll = 0;
        do
        {
            if (rx_len == 0)
            {
                /* 长度尚未就绪：有限次轮询，超出上限放弃，避免死循环卡死 task */
                if (++poll > SDIO_RX_LEN_POLL_MAX)
                {
                    printf("[sdio] rx_len stays 0, give up\n");
                    break;
                }
                sdio_read_addr(&wifi_sdio_func, SDIO_REG_RX0_REQ_LEN, (uint8_t *)&rx_len, 4);
                rx_len &= 0xFFFF;
                rx_len = ((rx_len >> 2) + ((rx_len & 3) ? 1 : 0)) << 2; /* 4-byte align */
                continue;
            }

            /* 重新读出的长度也必须校验，防止越界写 buf */
            if (rx_len > *size)
            {
                printf("[sdio] rx_len=%u > buf=%u, skip\n", (unsigned int)rx_len, (unsigned int)*size);
                break;
            }

            uint32_t reg = (WLAN_RX_FIFO_DEVICE_ID << 13) | (fifo_cnt & WLAN_RX_FIFO_MSK);
            fifo_cnt++;
            sdio_read_addr(&wifi_sdio_func, reg, buf, rx_len);
            *size = rx_len;
            ret   = true;
            break;
        }
        while (1);
    }

    if (sdio_hisr & SDIO_HISR_CPWM1)
    {
        uint8_t cpwm;
        sdio_read_byte(&wifi_sdio_func, SDIO_REG_HCPWM, &cpwm);
    }

    return ret;
}

int wifi_sdio_write_frame(T_WIFI_SDIO_WRITE_QUEUE *pkt)
{
    if (!pkt)
    {
        return -EINVAL;
    }

    s_tx_bd_num = sdio_tx_bd_read();
    if (s_tx_bd_num == 0)
    {
        return -EAGAIN;
    }

    /* Bug 1 修复：正确的 512-byte 对齐（括号确保 & 先于 +）*/
    uint16_t size = (uint16_t)((sizeof(TXDESC) + pkt->tx_desc.txpktsize + 511u) & ~511u);
    uint32_t reg  = (WLAN_TX_FIFO_DEVICE_ID << 13) | ((size >> 2) & WLAN_TX_FIFO_MSK);
    sdio_write_addr(&wifi_sdio_func, reg, (uint8_t *)&pkt->tx_desc, size);
    s_tx_bd_num = sdio_tx_bd_read();

    return 0;
}

uint16_t wifi_sdio_tx_bd_avail(void)
{
    s_tx_bd_num = sdio_tx_bd_read();
    return s_tx_bd_num;
}

uint8_t  *wifi_sdio_get_read_buf(void)      { return (uint8_t *)s_read_buf; }
uint16_t  wifi_sdio_get_read_buf_size(void) { return (uint16_t)sizeof(s_read_buf); }
