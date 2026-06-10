/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include <stdio.h>
#include "wifi_download.h"
#include "rtl876x_pinmux.h"
#include "platform_utils.h"
#include "os_sched.h"
#include "os_mem.h"
#include "fmc_api.h"
#include "flash_map.h"
#include "patch_header_check.h"
#include "wifi_xmodem.h"
#include "wifi_uart.h"

#define DUT_POWER_PIN     P2_7
#define Z2_DOWNLOAD_CTRL0 P2_6
#define Z2_DOWNLOAD_CTRL1 P2_5

#define PROGRAM_BUF_SIZE   (16 * 1024)
#define HANDSHAKE_RECV_MS  1200

static uint16_t xmodem_rx_adapter(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    return wifi_uart_mp_recv(buf, max_len, timeout_ms);
}

static bool xmodem_tx_adapter(const uint8_t *data, uint16_t len)
{
    return wifi_uart_tx(data, len);
}

static void z2_pin_enter_download(void)
{
    Pad_Config(Z2_DOWNLOAD_CTRL0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE,
               PAD_OUT_HIGH);
    Pad_Config(Z2_DOWNLOAD_CTRL1, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE,
               PAD_OUT_HIGH);
    Pad_Config(DUT_POWER_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE, PAD_OUT_LOW);
    platform_delay_ms(500);
    Pad_Config(DUT_POWER_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
    platform_delay_ms(1000);
}

static bool z2_handshake(void)
{
    static const char ping_cmd[] = "ping\n";
    static const char ew_cmd[]   = "EW 40002800 7EFFFFF\n";
    static const char ucfg1m[]   = "ucfg 1000000 0 0\n";
    static const char floader[]  = "floader\r\n";
    static const char fwd[]      = "fwd 0 1 \n";

    uint8_t  rev[50]  = {0};
    uint16_t recv_len = 0;

    wifi_uart_tx((const uint8_t *)ping_cmd, strlen(ping_cmd));
    recv_len = wifi_uart_mp_recv(rev, sizeof(rev), HANDSHAKE_RECV_MS);
    rev[sizeof(rev) - 1] = '\0'; /* 防止响应填满 buffer 时 strstr 越界 */
    /* 用子串匹配：模组回显可能带 CR/LF 或前缀，strcmp 精确比较过于脆弱 */
    if (strstr((char *)rev, "ping") == NULL)
    {
        printf("[dl] ping fail recv=%d rsp=%s\n", recv_len, rev);
        return false;
    }

    wifi_uart_tx((const uint8_t *)ew_cmd, strlen(ew_cmd));
    recv_len = wifi_uart_mp_recv(rev, sizeof(rev), HANDSHAKE_RECV_MS);
    printf("[dl] EW rsp recv=%d rsp=%s\n", recv_len, rev);

    wifi_uart_tx((const uint8_t *)ucfg1m, strlen(ucfg1m) - 1);
    os_delay(400);
    wifi_uart_set_baudrate(1000000);
    recv_len = wifi_uart_mp_recv(rev, sizeof(rev), HANDSHAKE_RECV_MS);
    rev[sizeof(rev) - 1] = '\0'; /* 防止响应填满 buffer 时 strstr 越界 */
    if (strstr((char *)rev, "OK") == NULL)
    {
        printf("[dl] 1M baud fail recv=%d rsp=%s\n", recv_len, rev);
        return false;
    }
    os_delay(500);

    wifi_uart_tx((const uint8_t *)floader, strlen(floader) - 1);
    os_delay(50);
    wifi_uart_tx((const uint8_t *)floader, strlen(floader) - 1);
    os_delay(80);
    wifi_uart_tx((const uint8_t *)fwd, strlen(fwd) - 1);
    os_delay(800);

    uint8_t nak_byte = 0;
    recv_len = wifi_uart_mp_recv(&nak_byte, 1, HANDSHAKE_RECV_MS);
    if (recv_len == 0 || nak_byte != 0x15)
    {
        printf("[dl] floader NAK fail byte=0x%x\n", nak_byte);
        return false;
    }

    return true;
}

T_XMODEM_STATUS wifi_download_firmware(void)
{
    z2_pin_enter_download();

    if (wifi_uart_set_mode(WIFI_UART_MODE_MP) != 0)
    {
        printf("[dl] set MP mode fail\n");
        return XMODEM_ERR_PROGRAM_FAIL;
    }

    wifi_uart_set_baudrate(115200);

    if (!z2_handshake())
    {
        printf("[dl] handshake fail\n");
        wifi_uart_set_mode(WIFI_UART_MODE_AT);
        return XMODEM_ERR_PROGRAM_FAIL;
    }

    wifi_xmodem_init(xmodem_tx_adapter, xmodem_rx_adapter);

    T_IMG_HEADER_FORMAT *img_hdr = (T_IMG_HEADER_FORMAT *)USER_DATA2_ADDR;
    uint32_t fw_size    = img_hdr->ctrl_header.payload_len;
    uint32_t flash_addr = USER_DATA2_ADDR + 0x400;

    printf("[dl] firmware size=%u\n", fw_size);

    uint8_t *buf = os_mem_alloc(OS_MEM_TYPE_DATA, PROGRAM_BUF_SIZE);
    if (!buf)
    {
        printf("[dl] buf alloc fail\n");
        wifi_uart_set_mode(WIFI_UART_MODE_AT);
        return XMODEM_ERR_PROGRAM_FAIL;
    }

    T_XMODEM_STATUS ret = XMODEM_OK;
    uint32_t remain  = fw_size;
    uint32_t wr_addr = 0;  /* Bug 6 修复：初始化为 0 */

    while (remain > 0)
    {
        uint32_t chunk = (remain > PROGRAM_BUF_SIZE) ? PROGRAM_BUF_SIZE : remain;

        if (!fmc_flash_nor_read(flash_addr, buf, chunk))
        {
            printf("[dl] flash read fail addr=0x%x\n", flash_addr);
            ret = XMODEM_ERR_PROGRAM_FAIL;
            break;
        }

        printf("[dl] chunk flash=0x%x wr=0x%x len=%u\n", flash_addr, wr_addr, chunk);

        /* Bug 4 修复：xmodem_send 不发 EOT，全部 chunk 传完后由 finish 统一发 */
        ret = wifi_xmodem_send(buf, chunk);
        if (ret != XMODEM_OK)
        {
            printf("[dl] xmodem_send fail status=%d\n", ret);
            break;
        }

        flash_addr += chunk;
        wr_addr    += chunk;
        remain     -= chunk;
    }

    os_mem_free(buf);

    if (ret == XMODEM_OK)
    {
        /* Bug 4 修复：整包传完后唯一发 EOT */
        ret = wifi_xmodem_finish();
    }

    wifi_uart_set_baudrate(115200);
    wifi_uart_set_mode(WIFI_UART_MODE_AT);

    printf("[dl] done status=%d\n", ret);
    return ret;
}
