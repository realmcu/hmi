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

/* ---- SDIO 设备 ID ---- */
#define SDIO_LOCAL_DEVICE_ID        0
#define WLAN_TX_FIFO_DEVICE_ID      4
#define WLAN_RX_FIFO_DEVICE_ID      7
#define SDIO_LOCAL_MSK              0xFFF
#define WLAN_TX_FIFO_MSK            0x1FFF
#define WLAN_RX_FIFO_MSK            0x3

/* ---- SDIO 本地寄存器 ---- */
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

/* ---- 写队列节点 ---- */
typedef struct t_sdio_write_queue
{
    struct t_sdio_write_queue *p_next; /* 必须在首位（os_queue 要求）*/
    TXDESC                     tx_desc;
    uint8_t                    data[0];
} T_WIFI_SDIO_WRITE_QUEUE;

/* 初始化 SDIO 控制器、func、block size 及中断寄存器 */
int wifi_sdio_init(void);

/* 初始化 GPIO 中断 */
void wifi_sdio_int_init(void);

/* GPIO ISR（由 gpio_add_callback 注册）*/
void wifi_sdio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

/* 读取一帧 RX 数据
 * size 入参为 buf 最大容量，出参为实际读出长度
 * 返回 true 表示有数据 */
bool wifi_sdio_read_frame(uint8_t *buf, uint16_t *size);

/* 发送一帧 TX 数据
 * 返回  0      : 成功
 *       -EAGAIN : TX BD 耗尽（可重试）
 *       <0      : 错误 */
int wifi_sdio_write_frame(T_WIFI_SDIO_WRITE_QUEUE *pkt);

/* 查询当前 TX BD 空闲数 */
uint16_t wifi_sdio_tx_bd_avail(void);

/* 获取内部 4KB RX 读缓冲区（供 wifi_data 使用）*/
uint8_t  *wifi_sdio_get_read_buf(void);
uint16_t  wifi_sdio_get_read_buf_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TRANSPORT_SDIO_H_ */
