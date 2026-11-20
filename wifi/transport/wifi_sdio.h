/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _WIFI_TRANSPORT_SDIO_H_
#define _WIFI_TRANSPORT_SDIO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include "wifi_desc.h"

/* ---- SDIO device ID ---- */
#define SDIO_LOCAL_DEVICE_ID        0
#define WLAN_TX_FIFO_DEVICE_ID      4
#define WLAN_RX_FIFO_DEVICE_ID      7
#define SDIO_LOCAL_MSK              0xFFF
#define WLAN_TX_FIFO_MSK            0x1FFF
#define WLAN_RX_FIFO_MSK            0x3

/* ---- SDIO local registers ---- */
#define SDIO_REG_TX_CTRL                0x00
#define SDIO_REG_STATIS_RECOVERY_TIMOUT 0x02
#define SDIO_REG_32K_TRANS_IDLE_TIME    0x04
#define SDIO_REG_HIMR                   0x14
#define SDIO_REG_HISR                   0x18
#define SDIO_REG_RX0_REQ_LEN            0x1c
#define SDIO_REG_FREE_TXBD_NUM          0x20
#define SDIO_REG_HCPWM                  0x38
#define SDIO_REG_HCPWM2                 0x3a
#define SDIO_REG_HRPWM                  0x80
#define SDIO_REG_CPU_IND                0x87
#define SDIO_REG_AVAI_BD_NUM_TH_L       0xD0
#define SDIO_REG_AVAI_BD_NUM_TH_H       0xD4
#define SDIO_REG_RX_AGG_CFG             0xD8
#define SDIO_REG_TXBUF_UNIT_SZ          0x1D9
#define SDIO_REG_FREE_RXBD_CNT          0x1DA

/* ---- HISR / HIMR bit map ---- */
#define SDIO_HISR_RX_REQUEST            (BIT0)
#define SDIO_HISR_AVAL_INT              (BIT1)
#define SDIO_HISR_CPWM1                 (BIT18)
#define SDIO_HIMR_RX_REQUEST_MSK        (BIT0)
#define SDIO_HIMR_AVAL_MSK              (BIT1)
#define SDIO_HIMR_CPWM1_MSK             (BIT18)
#define MASK_SDIO_HISR_CLEAR            (BIT2|BIT3|BIT4|BIT17|BIT18|BIT19|BIT20|BIT22)

/* ---- Write queue node ---- */
typedef struct t_sdio_write_queue
{
    struct t_sdio_write_queue *p_next; /* must be first (os_queue requirement) */
    TXDESC                     tx_desc;
    uint8_t                    data[0];
} T_WIFI_SDIO_WRITE_QUEUE;

/* Initialize the SDIO controller, func, block size and interrupt registers */
int wifi_sdio_init(void);

/* Initialize the GPIO interrupt */
void wifi_sdio_int_init(void);

/* GPIO ISR (registered by gpio_add_callback) */
void wifi_sdio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

/* Read one RX frame
 * On input size is the capacity of buf; on output the number of bytes read
 * Returns true when data was available */
bool wifi_sdio_read_frame(uint8_t *buf, uint16_t *size);

/* Send one TX frame
 * Returns  0      : success
 *          -EAGAIN : TX BD exhausted (retry later)
 *          <0      : error */
int wifi_sdio_write_frame(T_WIFI_SDIO_WRITE_QUEUE *pkt);

/* Query the current number of free TX BDs */
uint16_t wifi_sdio_tx_bd_avail(void);

/* Get the internal 4KB RX read buffer (used by wifi_data) */
uint8_t  *wifi_sdio_get_read_buf(void);
uint16_t  wifi_sdio_get_read_buf_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TRANSPORT_SDIO_H_ */
