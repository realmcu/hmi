#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdio.h>
#include <zephyr/sys/crc.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtl876x_pinmux.h"
#include "wdg.h"

LOG_MODULE_REGISTER(z2plus_tcpudp, LOG_LEVEL_INF);

#define WIFI_EN_PIN             P6_4
#define RF_SWITCH_V2            P6_2
#define RF_SWITCH_V1            P6_0

#define UART_RX_BUF_LENGTH      128
#define UART_LINE_MAX           160
#define SDIO_RX_BUF_SIZE        4096
#define TRANSFER_HEADER_MAX     192
#define TRANSFER_NAME_MAX       64
#define TRANSFER_PORT           5001
#define Z2PLUS_WIFI_TIMEOUT_S   30
#define Z2PLUS_WIFI_SSID        "wifi"
#define Z2PLUS_WIFI_PASSWORD    "12345678"

#define SDIO_LOCAL_DEVICE_ID                    0
#define WLAN_RX_FIFO_DEVICE_ID                  7
#define WLAN_RX_FIFO_MSK                        0x3

#define SDIO_REG_STATIS_RECOVERY_TIMOUT         0x02
#define SDIO_REG_32K_TRANS_IDLE_TIME            0x04
#define SDIO_REG_HIMR                           0x14
#define SDIO_REG_HISR                           0x18
#define SDIO_REG_RX0_REQ_LEN                    0x1c
#define SDIO_REG_FREE_TXBD_NUM                  0x20
#define SDIO_REG_HCPWM                          0x38
#define SDIO_REG_CPU_IND                        0x87
#define SDIO_REG_AVAI_BD_NUM_TH_L               0xD0
#define SDIO_REG_AVAI_BD_NUM_TH_H               0xD4
#define SDIO_REG_FREE_RXBD_CNT                  0x1DA

#define SDIO_HISR_RX_REQUEST                    BIT(0)
#define SDIO_HISR_AVAL_INT                      BIT(1)
#define SDIO_HISR_TXPKT_OVER_BUFF               BIT(2)
#define SDIO_HISR_TX_AGG_SIZE_MISMATCH          BIT(3)
#define SDIO_HISR_TXBD_OVF                      BIT(4)
#define SDIO_HISR_C2H_MSG_INT                   BIT(17)
#define SDIO_HISR_CPWM1                         BIT(18)
#define SDIO_HISR_CPWM2                         BIT(19)
#define SDIO_HISR_H2C_BUS_FAIL                  BIT(20)
#define SDIO_HISR_CPU_NOT_RDY                   BIT(22)

#define MASK_SDIO_HISR_CLEAR (SDIO_HISR_TXPKT_OVER_BUFF | \
                              SDIO_HISR_TX_AGG_SIZE_MISMATCH | \
                              SDIO_HISR_TXBD_OVF | \
                              SDIO_HISR_C2H_MSG_INT | \
                              SDIO_HISR_CPWM1 | \
                              SDIO_HISR_CPWM2 | \
                              SDIO_HISR_H2C_BUS_FAIL | \
                              SDIO_HISR_CPU_NOT_RDY)

#define SDIO_HIMR_RX_REQUEST_MSK                BIT(0)
#define SDIO_HIMR_AVAL_MSK                      BIT(1)
#define SDIO_HIMR_CPWM1_MSK                     BIT(18)

typedef struct
{
    uint32_t ip_addr;
    uint16_t port;
    uint8_t seq;
    uint8_t rsvd1;
    uint32_t rsvd2;
    uint32_t rsvd3;
} extdesc_t;

typedef struct
{
    uint32_t pkt_len: 16;
    uint32_t offset: 8;
    uint32_t rsvd0: 6;
    uint32_t icv: 1;
    uint32_t crc: 1;
    uint32_t type: 8;
    uint32_t rsvd1: 24;
    uint32_t rsvd2;
    uint32_t rsvd3;
    uint32_t rsvd4;
    uint32_t seq: 8;
    uint32_t rsvd5: 24;
    extdesc_t ext_desc;
} rxdesc_t;

enum transfer_state
{
    TRANSFER_WAIT_HEADER,
    TRANSFER_RECV_BODY,
};

struct uart_state
{
    uint8_t rx_buf[UART_RX_BUF_LENGTH * 2];
    uint8_t buf_index;
    char line_buf[UART_LINE_MAX];
    size_t line_len;
    bool tx_busy;
};

struct transfer_state_ctx
{
    enum transfer_state state;
    char header_buf[TRANSFER_HEADER_MAX];
    size_t header_len;
    char file_name[TRANSFER_NAME_MAX];
    uint32_t expected_size;
    uint32_t expected_crc;
    uint32_t received_size;
    uint32_t running_crc;
    uint32_t packet_count;
    bool header_error;
};

static const struct device *const sdhc_dev = DEVICE_DT_GET(DT_ALIAS(sdhc0));
static const struct device *const uart_dev = DEVICE_DT_GET(DT_ALIAS(uart3));
static const struct gpio_dt_spec wifi_int = GPIO_DT_SPEC_GET(DT_ALIAS(wifi_int), gpios);

static struct sd_card wifi_card;
static struct sdio_func wifi_sdio_func;
static struct gpio_callback wifi_int_cb_data;
static struct k_sem sdio_int_sem;
static struct k_sem uart_tx_done_sem;
static struct k_sem uart_wait_sem;

static struct uart_state uart_state;
static struct transfer_state_ctx transfer_ctx;

struct uart_wait_state
{
    const char *success_token;
    const char *error_token;
    bool active;
    bool success;
};

static struct uart_wait_state uart_wait_state;

static uint32_t sdio_read_buf[SDIO_RX_BUF_SIZE / sizeof(uint32_t)];

static void wifi_enable(bool enable)
{
    if (enable)
    {
        Pad_Config(WIFI_EN_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE,
                   PAD_OUT_LOW);
        k_msleep(200);
        Pad_Config(WIFI_EN_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE,
                   PAD_OUT_HIGH);
        k_msleep(2000);

        Pad_Config(RF_SWITCH_V1, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
                   PAD_OUT_DISABLE, PAD_OUT_HIGH);
        Pinmux_Config(RF_SWITCH_V1, EN_EXLNA);

        Pad_Config(RF_SWITCH_V2, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
                   PAD_OUT_DISABLE, PAD_OUT_HIGH);
        Pinmux_Config(RF_SWITCH_V2, EN_EXPA);
        LOG_INF("z2plus power enabled");
    }
    else
    {
        Pad_Config(WIFI_EN_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE,
                   PAD_OUT_LOW);
        LOG_INF("z2plus power disabled");
    }
}

static void transfer_reset(void)
{
    memset(&transfer_ctx, 0, sizeof(transfer_ctx));
    transfer_ctx.state = TRANSFER_WAIT_HEADER;
}

static void transfer_complete(void)
{
    bool crc_ok = (transfer_ctx.running_crc == transfer_ctx.expected_crc);

    LOG_INF("transfer done: name=%s bytes=%u crc32=%08x expected=%08x packets=%u status=%s",
            transfer_ctx.file_name,
            transfer_ctx.received_size,
            transfer_ctx.running_crc,
            transfer_ctx.expected_crc,
            transfer_ctx.packet_count,
            crc_ok ? "ok" : "crc-mismatch");

    transfer_reset();
}

static bool transfer_parse_header(void)
{
    unsigned int parsed_size = 0;
    unsigned int parsed_crc = 0;
    char file_name[TRANSFER_NAME_MAX] = {0};
    int matched = sscanf(transfer_ctx.header_buf, "Z2FT1 TCP %63s %u %x",
                         file_name, &parsed_size, &parsed_crc);

    if (matched != 3)
    {
        LOG_ERR("invalid transfer header: %s", transfer_ctx.header_buf);
        transfer_ctx.header_error = true;
        transfer_reset();
        return false;
    }

    memcpy(transfer_ctx.file_name, file_name, sizeof(transfer_ctx.file_name) - 1);
    transfer_ctx.expected_size = parsed_size;
    transfer_ctx.expected_crc = parsed_crc;
    transfer_ctx.received_size = 0;
    transfer_ctx.running_crc = 0;
    transfer_ctx.packet_count = 0;
    transfer_ctx.state = TRANSFER_RECV_BODY;

    LOG_INF("transfer start: name=%s bytes=%u crc32=%08x",
            transfer_ctx.file_name,
            transfer_ctx.expected_size,
            transfer_ctx.expected_crc);
    return true;
}

static void transfer_feed_stream(const uint8_t *data, size_t len)
{
    size_t offset = 0;

    while (offset < len)
    {
        if (transfer_ctx.state == TRANSFER_WAIT_HEADER)
        {
            uint8_t ch = data[offset++];

            if (ch == '\r')
            {
                continue;
            }

            if (ch == '\n')
            {
                transfer_ctx.header_buf[transfer_ctx.header_len] = '\0';
                transfer_ctx.header_len = 0;
                if (!transfer_parse_header())
                {
                    continue;
                }
                continue;
            }

            if (transfer_ctx.header_len + 1 >= sizeof(transfer_ctx.header_buf))
            {
                LOG_ERR("transfer header too long, resetting parser");
                transfer_reset();
                continue;
            }

            transfer_ctx.header_buf[transfer_ctx.header_len++] = (char)ch;
            continue;
        }

        uint32_t remaining = transfer_ctx.expected_size - transfer_ctx.received_size;
        size_t chunk = MIN((size_t)remaining, len - offset);

        transfer_ctx.running_crc = crc32_ieee_update(transfer_ctx.running_crc, data + offset, chunk);
        transfer_ctx.received_size += (uint32_t)chunk;
        offset += chunk;

        if (transfer_ctx.received_size == transfer_ctx.expected_size)
        {
            transfer_complete();
        }
    }
}

static void uart_wait_begin(const char *success_token, const char *error_token)
{
    uart_wait_state.success_token = success_token;
    uart_wait_state.error_token = error_token;
    uart_wait_state.active = true;
    uart_wait_state.success = false;
    k_sem_reset(&uart_wait_sem);
}

static void uart_wait_end(void)
{
    uart_wait_state.success_token = NULL;
    uart_wait_state.error_token = NULL;
    uart_wait_state.active = false;
    uart_wait_state.success = false;
}

static void uart_match_response_line(const char *line)
{
    if (!uart_wait_state.active || line == NULL)
    {
        return;
    }

    if (uart_wait_state.error_token != NULL && strstr(line, uart_wait_state.error_token) != NULL)
    {
        uart_wait_state.success = false;
        uart_wait_state.active = false;
        k_sem_give(&uart_wait_sem);
        return;
    }

    if (uart_wait_state.success_token != NULL && strstr(line, uart_wait_state.success_token) != NULL)
    {
        uart_wait_state.success = true;
        uart_wait_state.active = false;
        k_sem_give(&uart_wait_sem);
    }
}

static void uart_consume_rx(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        char ch = (char)data[i];

        if (ch == '\r')
        {
            continue;
        }

        if (ch == '\n')
        {
            if (uart_state.line_len > 0)
            {
                uart_state.line_buf[uart_state.line_len] = '\0';
                LOG_INF("uart rsp: %s", uart_state.line_buf);
                uart_match_response_line(uart_state.line_buf);
                uart_state.line_len = 0;
            }
            continue;
        }

        if (uart_state.line_len + 1 < sizeof(uart_state.line_buf))
        {
            uart_state.line_buf[uart_state.line_len++] = ch;
        }
        else
        {
            uart_state.line_len = 0;
        }
    }
}

static void uart_async_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    switch (evt->type)
    {
    case UART_TX_DONE:
        uart_state.tx_busy = false;
        k_sem_give(&uart_tx_done_sem);
        break;

    case UART_RX_RDY:
        uart_consume_rx(&evt->data.rx.buf[evt->data.rx.offset], evt->data.rx.len);
        break;

    case UART_RX_BUF_REQUEST:
        uart_state.buf_index ^= 1U;
        uart_rx_buf_rsp(uart_dev,
                        &uart_state.rx_buf[uart_state.buf_index * UART_RX_BUF_LENGTH],
                        UART_RX_BUF_LENGTH);
        break;

    default:
        break;
    }
}

static int uart_start(void)
{
    int ret;

    if (!device_is_ready(uart_dev))
    {
        LOG_ERR("uart3 not ready");
        return -ENODEV;
    }

    uart_callback_set(uart_dev, uart_async_callback, NULL);
    uart_state.buf_index = 0;
    ret = uart_rx_enable(uart_dev, uart_state.rx_buf, UART_RX_BUF_LENGTH, 0);
    if (ret != 0)
    {
        LOG_ERR("uart_rx_enable failed: %d", ret);
        return ret;
    }

    return 0;
}

static int uart_send_line(const char *line)
{
    size_t len = strlen(line);
    int ret;

    if (uart_state.tx_busy)
    {
        return -EBUSY;
    }

    k_sem_reset(&uart_tx_done_sem);
    uart_state.tx_busy = true;
    ret = uart_tx(uart_dev, line, len, SYS_FOREVER_US);
    if (ret != 0)
    {
        uart_state.tx_busy = false;
    }

    return ret;
}

static int uart_send_command_and_wait(const char *line,
                                      const char *success_token,
                                      const char *error_token,
                                      k_timeout_t timeout)
{
    int ret;

    uart_wait_begin(success_token, error_token);

    ret = uart_send_line(line);
    if (ret != 0)
    {
        uart_wait_end();
        return ret;
    }

    ret = k_sem_take(&uart_wait_sem, timeout);
    if (ret != 0)
    {
        uart_wait_end();
        return ret;
    }

    ret = uart_wait_state.success ? 0 : -EIO;
    uart_wait_end();
    return ret;
}

static int z2plus_connect_wifi(void)
{
    char cmd[128];

    if (Z2PLUS_WIFI_SSID[0] == '\0')
    {
        LOG_ERR("wifi ssid is empty");
        return -EINVAL;
    }

    if (strchr(Z2PLUS_WIFI_SSID, ',') != NULL || strchr(Z2PLUS_WIFI_PASSWORD, ',') != NULL)
    {
        LOG_ERR("wifi ssid/password must not contain commas for ATPN");
        return -EINVAL;
    }

    if (Z2PLUS_WIFI_PASSWORD[0] != '\0')
    {
        snprintk(cmd, sizeof(cmd), "ATPN=%s,%s\r\n", Z2PLUS_WIFI_SSID, Z2PLUS_WIFI_PASSWORD);
    }
    else
    {
        snprintk(cmd, sizeof(cmd), "ATPN=%s\r\n", Z2PLUS_WIFI_SSID);
    }

    LOG_INF("connecting z2plus wifi ssid=%s", Z2PLUS_WIFI_SSID);
    return uart_send_command_and_wait(cmd, "[ATPN] OK", "[ATPN] ERROR:",
                                      K_SECONDS(Z2PLUS_WIFI_TIMEOUT_S));
}

static int z2plus_start_tcp_listener(void)
{
    char cmd[32];

    snprintk(cmd, sizeof(cmd), "ATTF=tcp,%d\r\n", TRANSFER_PORT);
    LOG_INF("starting z2plus tcp listener on %d", TRANSFER_PORT);
    return uart_send_command_and_wait(cmd, "[ATTF] OK:tcp", "[ATTF] ERROR:", K_SECONDS(5));
}

static bool sdio_read_packet(uint8_t *read_data, uint16_t *size)
{
    static uint32_t fifo_cnt;
    uint32_t sdio_hisr = 0;
    uint32_t rx_len = 0;
    uint32_t hisr_clear = 0;

    sdio_read_addr(&wifi_sdio_func, SDIO_REG_HISR, (uint8_t *)&sdio_hisr, 4);

    if (sdio_hisr == 0)
    {
        return false;
    }

    if (sdio_hisr & SDIO_HISR_AVAL_INT)
    {
        hisr_clear |= SDIO_HISR_AVAL_INT;
    }

    if (sdio_hisr & MASK_SDIO_HISR_CLEAR)
    {
        LOG_WRN("sdio hisr=0x%08x", (unsigned int)(sdio_hisr & MASK_SDIO_HISR_CLEAR));
        hisr_clear |= (sdio_hisr & MASK_SDIO_HISR_CLEAR);
    }

    if (sdio_hisr & SDIO_HISR_RX_REQUEST)
    {
        sdio_read_addr(&wifi_sdio_func, SDIO_REG_RX0_REQ_LEN, (uint8_t *)&rx_len, 4);
        rx_len &= 0xFFFF;
        rx_len = ((rx_len >> 2) + ((rx_len & 3U) ? 1U : 0U)) << 2;

        if (rx_len > 0 && rx_len <= *size)
        {
            uint32_t reg = (WLAN_RX_FIFO_DEVICE_ID << 13) | (fifo_cnt & WLAN_RX_FIFO_MSK);
            uint16_t free_cnt = 1;

            fifo_cnt++;
            sdio_read_addr(&wifi_sdio_func, reg, read_data, rx_len);
            sdio_write_addr(&wifi_sdio_func, SDIO_REG_FREE_RXBD_CNT, (uint8_t *)&free_cnt, 2);
            *size = (uint16_t)rx_len;
            hisr_clear |= SDIO_HISR_RX_REQUEST;
        }
        else
        {
            LOG_WRN("sdio rx length invalid: %u", rx_len);
            hisr_clear |= SDIO_HISR_RX_REQUEST;
        }
    }

    if (sdio_hisr & SDIO_HISR_CPWM1)
    {
        uint8_t cpwm;

        sdio_read_byte(&wifi_sdio_func, SDIO_REG_HCPWM, &cpwm);
        hisr_clear |= SDIO_HISR_CPWM1;
    }

    if (hisr_clear != 0U)
    {
        sdio_write_addr(&wifi_sdio_func, SDIO_REG_HISR, (uint8_t *)&hisr_clear, 4);
    }

    return (sdio_hisr & SDIO_HISR_RX_REQUEST) != 0U;
}

static void wifi_sdio_int_handler(const struct device *port,
                                  struct gpio_callback *cb,
                                  uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    k_sem_give(&sdio_int_sem);
}

static int sdio_init_channel(void)
{
    int ret;
    uint8_t read_val = 0;
    uint32_t read_reg = 0;
    uint32_t write_data = 0xFFFFFFFF;

    if (!device_is_ready(sdhc_dev))
    {
        LOG_ERR("sdhc0 not ready");
        return -ENODEV;
    }

    ret = sd_init(sdhc_dev, &wifi_card);
    if (ret != 0)
    {
        LOG_ERR("sd_init failed: %d", ret);
        return ret;
    }

    ret = sdio_init_func(&wifi_card, &wifi_sdio_func, 1);
    if (ret != 0)
    {
        LOG_ERR("sdio_init_func failed: %d", ret);
        return ret;
    }

    ret = sdio_set_block_size(&wifi_sdio_func, wifi_sdio_func.cis.max_blk_size);
    if (ret != 0)
    {
        LOG_ERR("sdio_set_block_size failed: %d", ret);
        return ret;
    }

    sdio_write_byte(&wifi_card.func0, 0x110, 0x00);
    sdio_write_byte(&wifi_card.func0, 0x111, 0x02);
    sdio_read_byte(&wifi_card.func0, SDIO_REG_STATIS_RECOVERY_TIMOUT, &read_val);
    sdio_write_byte(&wifi_card.func0, SDIO_REG_STATIS_RECOVERY_TIMOUT, 0x02);
    sdio_read_byte(&wifi_card.func0, 0x03, &read_val);
    sdio_write_byte(&wifi_card.func0, 0x110, 0x00);
    sdio_write_byte(&wifi_card.func0, 0x111, 0x02);
    sdio_read_byte(&wifi_sdio_func, SDIO_REG_CPU_IND, &read_val);
    sdio_read_addr(&wifi_sdio_func, SDIO_REG_FREE_TXBD_NUM, (uint8_t *)&read_reg, 4);

    sdio_write_byte(&wifi_sdio_func, SDIO_REG_AVAI_BD_NUM_TH_L, 0x16);
    sdio_write_byte(&wifi_sdio_func, 0xD1, 0x00);
    sdio_write_byte(&wifi_sdio_func, SDIO_REG_AVAI_BD_NUM_TH_H, 0x0b);
    sdio_write_byte(&wifi_sdio_func, 0xD5, 0x00);

    sdio_write_addr(&wifi_sdio_func, SDIO_REG_HISR, (uint8_t *)&write_data, 4);
    write_data = 0;
    sdio_write_addr(&wifi_sdio_func, SDIO_REG_HIMR, (uint8_t *)&write_data, 4);
    sdio_read_byte(&wifi_sdio_func, 0x21, &read_val);
    sdio_read_byte(&wifi_sdio_func, SDIO_REG_FREE_TXBD_NUM, &read_val);
    sdio_read_byte(&wifi_sdio_func, SDIO_REG_HCPWM, &read_val);

    sdio_read_byte(&wifi_card.func0, SDIO_REG_32K_TRANS_IDLE_TIME, &read_val);
    sdio_write_byte(&wifi_card.func0, SDIO_REG_32K_TRANS_IDLE_TIME, 0x03);

    write_data = SDIO_HIMR_RX_REQUEST_MSK | SDIO_HIMR_AVAL_MSK | SDIO_HIMR_CPWM1_MSK;
    sdio_write_addr(&wifi_sdio_func, SDIO_REG_HIMR, (uint8_t *)&write_data, 4);

    if (!device_is_ready(wifi_int.port))
    {
        LOG_ERR("wifi_int gpio not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&wifi_int, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0)
    {
        LOG_ERR("wifi_int configure failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&wifi_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0)
    {
        LOG_ERR("wifi_int irq configure failed: %d", ret);
        return ret;
    }

    gpio_init_callback(&wifi_int_cb_data, wifi_sdio_int_handler, BIT(wifi_int.pin));
    gpio_add_callback(wifi_int.port, &wifi_int_cb_data);

    LOG_INF("sdio ready");
    return 0;
}

static void sdio_drain_packets(void)
{
    uint32_t drained = 0;

    for (;;)
    {
        uint16_t read_size = sizeof(sdio_read_buf);
        rxdesc_t *rx_desc = (rxdesc_t *)sdio_read_buf;

        if (!sdio_read_packet((uint8_t *)sdio_read_buf, &read_size))
        {
            break;
        }

        if (read_size < sizeof(*rx_desc) || rx_desc->pkt_len == 0U)
        {
            continue;
        }

        if (rx_desc->offset >= read_size)
        {
            LOG_WRN("invalid rx offset=%u size=%u", rx_desc->offset, read_size);
            continue;
        }

        transfer_ctx.packet_count++;
        transfer_feed_stream(((uint8_t *)sdio_read_buf) + rx_desc->offset, rx_desc->pkt_len);
        drained++;
    }

    if (drained > 1U)
    {
        LOG_INF("sdio drained %u packets", drained);
    }
}

static void sdio_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true)
    {
        wdg_kick();
        k_sem_take(&sdio_int_sem, K_FOREVER);
        wdg_kick();
        sdio_drain_packets();
    }
}

K_THREAD_DEFINE(sdio_thread_id, 2048, sdio_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    int ret;

    k_sem_init(&sdio_int_sem, 0, UINT_MAX);
    k_sem_init(&uart_tx_done_sem, 0, 1);
    k_sem_init(&uart_wait_sem, 0, 1);
    uart_wait_end();
    transfer_reset();

    LOG_INF("z2plus tcp receiver start");

    wifi_enable(true);

    ret = sdio_init_channel();
    if (ret != 0)
    {
        LOG_ERR("sdio init failed: %d", ret);
        return ret;
    }

    ret = uart_start();
    if (ret != 0)
    {
        LOG_ERR("uart init failed: %d", ret);
        return ret;
    }

    ret = z2plus_connect_wifi();
    if (ret != 0)
    {
        LOG_ERR("ATPN failed: %d", ret);
        return ret;
    }

    LOG_INF("z2plus wifi connected");

    ret = z2plus_start_tcp_listener();
    if (ret != 0)
    {
        LOG_ERR("ATTF failed: %d", ret);
        return ret;
    }

    while (true)
    {
        wdg_kick();
        k_sleep(K_SECONDS(1));
    }

    return 0;
}