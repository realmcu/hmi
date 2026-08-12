/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 *
 * Navigation projection BLE GATT service.
 *
 * Implements the FFD0 service for JPEG frame transfer over BLE.
 * FFD1/FFD2 carry CRC-protected L1 control frames; each FFD3 Write Command carries one complete
 * protocol-v2 data packet; FFD4 carries CREDIT and REPORT notifications.
 */

#include "navi_projection_service.h"
#include <string.h>
#include <stdio.h>
#include "trace.h"
#include "gap_conn_le.h"
#include "bt_gatt_svc.h"
#include "def_file.h" /* gui_rgb_data_head_t, gui_jpeg_file_head_t */
#include "gui_server.h"
#include "gui_message.h"
#include "rtl876x.h"

#if F_APP_GATT_SERVER_EXT_API_SUPPORT
#include <profile_server_ext.h>
#else
#error "Navi projection requires EXT_API support (F_APP_GATT_SERVER_EXT_API_SUPPORT=1)"
#endif

/* Reserve fixed PSRAM regions so frame reception never allocates memory in a BLE callback. */
#define NAVI_PSRAM_BASE        SPIC1_MEM_BASE
#define NAVI_PSRAM_POOL_SIZE   (512 * 1024)
#define NAVI_JPG_HEADER_SIZE   16 /* gui_rgb_data_head_t(8) + size(4) + dummy(4) */
#define NAVI_COVERAGE_SIZE     ((NAVI_MAX_JPEG_SIZE + 7) / 8)

#define NAVI_CONN_INTERVAL_MIN 0x06 /* 7.5 ms */
#define NAVI_CONN_INTERVAL_MAX 0x0C /* 15 ms */
#define NAVI_CONN_LATENCY      0
#define NAVI_CONN_TIMEOUT      0x01F4 /* 5 seconds */
#define NAVI_CE_LENGTH_MIN     (2 * (NAVI_CONN_INTERVAL_MIN - 1))
#define NAVI_CE_LENGTH_MAX     (2 * (NAVI_CONN_INTERVAL_MAX - 1))
#define NAVI_DLE_TX_OCTETS     251
#define NAVI_DLE_TX_TIME       2120

static bool navi_psram_init(void);
static void navi_reset_frame_slots(void);

#define NAVI_ATTR_SERVICE_DECL      0
#define NAVI_ATTR_CTRL_TX_CHAR_DECL 1
#define NAVI_ATTR_CTRL_TX_VALUE     2

#define NAVI_ATTR_CTRL_RX_CHAR_DECL 3
#define NAVI_ATTR_CTRL_RX_VALUE     4
#define NAVI_ATTR_CTRL_RX_CCCD      5

#define NAVI_ATTR_DATA_TX_CHAR_DECL 6
#define NAVI_ATTR_DATA_TX_VALUE     7

#define NAVI_ATTR_DATA_RX_CHAR_DECL 8
#define NAVI_ATTR_DATA_RX_VALUE     9
#define NAVI_ATTR_DATA_RX_CCCD      10

#define NAVI_ATTR_COUNT             11

/* CRC-16/ARC: polynomial 0x8005, initial value 0x0000, reflected. */
static const uint16_t crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, 0xC601, 0x06C0, 0x0780, 0xC741,
    0x0500, 0xC5C1, 0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841, 0xD801, 0x18C0, 0x1980, 0xD941,
    0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641, 0xD201, 0x12C0, 0x1380, 0xD341,
    0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441, 0x3C00, 0xFCC1, 0xFD81, 0x3D40,
    0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41, 0xEE01, 0x2EC0, 0x2F80, 0xEF41,
    0x2D00, 0xEDC1, 0xEC81, 0x2C40, 0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041, 0xA001, 0x60C0, 0x6180, 0xA141,
    0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41, 0xAA01, 0x6AC0, 0x6B80, 0xAB41,
    0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40, 0xB401, 0x74C0, 0x7580, 0xB541,
    0x7700, 0xB7C1, 0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241, 0x9601, 0x56C0, 0x5780, 0x9741,
    0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841, 0x8801, 0x48C0, 0x4980, 0x8941,
    0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641, 0x8201, 0x42C0, 0x4380, 0x8341,
    0x4100, 0x81C1, 0x8081, 0x4040
};

static uint16_t navi_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000;
    while (len--)
    {
        crc = (crc >> 8) ^ crc16_table[(crc ^ *data++) & 0xFF];
    }
    return crc;
}

static uint32_t navi_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    while (len--)
    {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* Exercise the CRC implementation with the standard CRC-16/ARC test vector. */
#define NAVI_CRC_SELF_TEST()                                                                       \
    do                                                                                             \
    {                                                                                              \
        (void)navi_crc16((const uint8_t *)"123456789", 9);                                         \
    } while (0)

#define BIG16(a, b)    (((uint16_t)(a) << 8) | (uint16_t)(b))
#define BIG24(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))
#define BIG32(a, b, c, d)                                                                          \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define HI_24(v)  ((uint8_t)(((v) >> 16) & 0xFF))
#define MID_24(v) ((uint8_t)(((v) >> 8) & 0xFF))
#define LO_24(v)  ((uint8_t)((v) & 0xFF))
#define HI_16(v)  ((uint8_t)(((v) >> 8) & 0xFF))
#define LO_16(v)  ((uint8_t)((v) & 0xFF))

static P_FUN_EXT_SERVER_GENERAL_CB pfn_navi_general_cb = NULL;
static navi_frame_ready_cb_t pfn_frame_ready_cb = NULL;
static navi_gui_frame_cb_t pfn_gui_frame_cb = NULL;
static T_SERVER_ID navi_service_id = 0xFF;

const T_ATTRIB_APPL navi_projection_tbl[] = {
    /* [0] <<Primary Service>>, FFD0 */
    { (ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_LE),
      { LO_WORD(GATT_UUID_PRIMARY_SERVICE),
        HI_WORD(GATT_UUID_PRIMARY_SERVICE),
        LO_WORD(NAVI_SERVICE_UUID),
        HI_WORD(NAVI_SERVICE_UUID) },
      UUID_16BIT_SIZE,
      NULL,
      GATT_PERM_READ },

    /* [1] <<Characteristic>> FFD1 CTRL_TX (Write) */
    { ATTRIB_FLAG_VALUE_INCL,
      {
          LO_WORD(GATT_UUID_CHARACTERISTIC),
          HI_WORD(GATT_UUID_CHARACTERISTIC),
          GATT_CHAR_PROP_WRITE /* Write only */
      },
      1,
      NULL,
      GATT_PERM_READ },

    /* [2] FFD1 CTRL_TX value */
    { ATTRIB_FLAG_VALUE_APPL,
      { LO_WORD(NAVI_CHAR_CTRL_TX_UUID), HI_WORD(NAVI_CHAR_CTRL_TX_UUID) },
      0,
      NULL,
      GATT_PERM_WRITE },

    /* [3] <<Characteristic>> FFD2 CTRL_RX (Notify) */
    { ATTRIB_FLAG_VALUE_INCL,
      { LO_WORD(GATT_UUID_CHARACTERISTIC),
        HI_WORD(GATT_UUID_CHARACTERISTIC),
        GATT_CHAR_PROP_NOTIFY },
      1,
      NULL,
      GATT_PERM_READ },

    /* [4] FFD2 CTRL_RX value */
    { ATTRIB_FLAG_VALUE_APPL,
      { LO_WORD(NAVI_CHAR_CTRL_RX_UUID), HI_WORD(NAVI_CHAR_CTRL_RX_UUID) },
      0,
      NULL,
      GATT_PERM_NONE },

    /* [5] FFD2 CTRL_RX CCCD */
    { ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL,
      { LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
        HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
        LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
        HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT) },
      2,
      NULL,
      (GATT_PERM_READ | GATT_PERM_WRITE) },

    /* [6] <<Characteristic>> FFD3 DATA_TX (WriteWithoutResponse) */
    { ATTRIB_FLAG_VALUE_INCL,
      {
          LO_WORD(GATT_UUID_CHARACTERISTIC),
          HI_WORD(GATT_UUID_CHARACTERISTIC),
          GATT_CHAR_PROP_WRITE_NO_RSP /* WriteWithoutResponse */
      },
      1,
      NULL,
      GATT_PERM_READ },

    /* [7] FFD3 DATA_TX value */
    { ATTRIB_FLAG_VALUE_APPL,
      { LO_WORD(NAVI_CHAR_DATA_TX_UUID), HI_WORD(NAVI_CHAR_DATA_TX_UUID) },
      0,
      NULL,
      GATT_PERM_WRITE },

    /* [8] <<Characteristic>> FFD4 DATA_RX (Notify) */
    { ATTRIB_FLAG_VALUE_INCL,
      { LO_WORD(GATT_UUID_CHARACTERISTIC),
        HI_WORD(GATT_UUID_CHARACTERISTIC),
        GATT_CHAR_PROP_NOTIFY },
      1,
      NULL,
      GATT_PERM_READ },

    /* [9] FFD4 DATA_RX value */
    { ATTRIB_FLAG_VALUE_APPL,
      { LO_WORD(NAVI_CHAR_DATA_RX_UUID), HI_WORD(NAVI_CHAR_DATA_RX_UUID) },
      0,
      NULL,
      GATT_PERM_NONE },

    /* [10] FFD4 DATA_RX CCCD */
    { ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL,
      { LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
        HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
        LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT),
        HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT) },
      2,
      NULL,
      (GATT_PERM_READ | GATT_PERM_WRITE) },
};

static navi_state_t s_state = NAVI_STATE_IDLE;
static navi_open_params_t s_open_params;
static uint16_t s_credit = 0;
static uint16_t s_conn_handle = 0xFFFF;
static uint16_t s_cid = 0xFFFF;
static uint8_t s_conn_id = 0xFF;
static bool s_first_frame_link_info_pending;

static uint8_t s_l1_rx_buf[512];
static uint16_t s_l1_rx_len;
static bool s_l1_synced;

static uint16_t s_l1_tx_seq;
static uint16_t s_l1_rx_seq;

/* Triple buffer shared by BLE reception and GUI consumption. */
static navi_frame_slot_t s_frame_slots[NAVI_FRAME_SLOTS];
static int s_current_slot;

static bool navi_psram_init(void)
{
    const uint32_t slot_jpeg_size = NAVI_MAX_JPEG_SIZE + NAVI_JPG_HEADER_SIZE;
    const uint32_t slot_bitmap_size = NAVI_COVERAGE_SIZE;
    const uint32_t required_size = NAVI_FRAME_SLOTS * (slot_jpeg_size + slot_bitmap_size);

    if (required_size > NAVI_PSRAM_POOL_SIZE)
    {
        APP_PRINT_ERROR2(
            "Navi PSRAM pool too small: need=%u available=%u", required_size, NAVI_PSRAM_POOL_SIZE);
        return false;
    }

    for (int i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        s_frame_slots[i].jpeg_buffer = (uint8_t *)(NAVI_PSRAM_BASE + i * slot_jpeg_size);
        s_frame_slots[i].chunk_bitmap =
            (uint8_t *)(NAVI_PSRAM_BASE + NAVI_FRAME_SLOTS * slot_jpeg_size + i * slot_bitmap_size);
    }

    APP_PRINT_INFO2("Navi PSRAM initialized: base=0x%08X size=%u", NAVI_PSRAM_BASE, required_size);
    return true;
}

static void navi_reset_frame_slots(void)
{
    for (int i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        s_frame_slots[i].frame_seq = 0;
        s_frame_slots[i].total_len = 0;
        s_frame_slots[i].received_bytes = 0;
        s_frame_slots[i].expected_crc32 = 0;
        s_frame_slots[i].crc32_received = false;
        s_frame_slots[i].credits_consumed = 0;
        s_frame_slots[i].pending_retx_credits = 0;
        s_frame_slots[i].complete = false;
        s_frame_slots[i].terminal_seen = false;
        s_frame_slots[i].gui_claimed = false;
        s_frame_slots[i].state = NAVI_SLOT_FREE;
    }
}

static uint16_t s_report_frame_seq;
static uint32_t s_last_data_time_ms;

/* READY may be replaced by a newer frame; USE remains owned by the GUI. */
static volatile int8_t s_ready_slot = -1;
static volatile int8_t s_use_slot = -1;
static volatile bool s_gui_msg_pending;

static uint32_t s_perf_start_ms;
static volatile uint32_t s_perf_rx_frames;
static volatile uint32_t s_perf_gui_frames;
static volatile uint32_t s_perf_rx_bytes;
static volatile uint32_t s_perf_ready_drops;
static volatile uint32_t s_perf_crc_errors;

static void navi_perf_reset(void)
{
    extern uint32_t sys_timestamp_get_us(void);
    s_perf_start_ms = sys_timestamp_get_us() / 1000;
    s_perf_rx_frames = 0;
    s_perf_gui_frames = 0;
    s_perf_rx_bytes = 0;
    s_perf_ready_drops = 0;
    s_perf_crc_errors = 0;
}

static void navi_perf_report(void)
{
    extern uint32_t sys_timestamp_get_us(void);
    uint32_t now_ms = sys_timestamp_get_us() / 1000;
    uint32_t elapsed_ms = now_ms - s_perf_start_ms;

    if (elapsed_ms < 1000)
    {
        return;
    }

    uint32_t rx_frames = s_perf_rx_frames;
    uint32_t gui_frames = s_perf_gui_frames;
    uint32_t rx_bytes = s_perf_rx_bytes;
    uint32_t ready_drops = s_perf_ready_drops;
    uint32_t crc_errors = s_perf_crc_errors;
    uint32_t rx_fps_x100 = rx_frames * 100000U / elapsed_ms;
    uint32_t gui_fps_x100 = gui_frames * 100000U / elapsed_ms;
    uint32_t bytes_per_sec = rx_bytes * 1000U / elapsed_ms;

    DBG_DIRECT("Navi FPS: rx=%u.%02u gui=%u.%02u throughput=%u B/s ready_drop=%u crc_error=%u",
               rx_fps_x100 / 100,
               rx_fps_x100 % 100,
               gui_fps_x100 / 100,
               gui_fps_x100 % 100,
               bytes_per_sec,
               ready_drops,
               crc_errors);

    s_perf_start_ms = now_ms;
    s_perf_rx_frames = 0;
    s_perf_gui_frames = 0;
    s_perf_rx_bytes = 0;
    s_perf_ready_drops = 0;
    s_perf_crc_errors = 0;
}

static void navi_reset_slot_runtime(navi_frame_slot_t *slot)
{
    slot->frame_seq = 0;
    slot->total_len = 0;
    slot->received_bytes = 0;
    slot->expected_crc32 = 0;
    slot->crc32_received = false;
    slot->credits_consumed = 0;
    slot->pending_retx_credits = 0;
    slot->complete = false;
    slot->terminal_seen = false;
    slot->gui_claimed = false;
    slot->state = NAVI_SLOT_FREE;
}

static void navi_gui_consume_ready(void *p);

static void navi_queue_gui_wakeup_if_needed(void)
{
    if (s_ready_slot < 0 || s_gui_msg_pending)
    {
        return;
    }

    gui_msg_t msg = { .event = GUI_EVENT_USER_DEFINE, .cb = navi_gui_consume_ready };
    s_gui_msg_pending = gui_send_msg_to_server(&msg);
    if (!s_gui_msg_pending)
    {
        APP_PRINT_ERROR0("Navi GUI wakeup queue failed");
    }
}

static void navi_gui_consume_ready(void *p)
{
    (void)p;

    int ready_idx = s_ready_slot;
    if (ready_idx < 0 || ready_idx >= NAVI_FRAME_SLOTS)
    {
        s_gui_msg_pending = false;
        return;
    }

    navi_frame_slot_t *ready = &s_frame_slots[ready_idx];
    if (ready->state != NAVI_SLOT_READY)
    {
        if (s_ready_slot == ready_idx)
        {
            s_ready_slot = -1;
        }
        s_gui_msg_pending = false;
        navi_queue_gui_wakeup_if_needed();
        return;
    }

    uint16_t claimed_seq = ready->frame_seq;
    ready->gui_claimed = true;
    bool accepted = pfn_gui_frame_cb != NULL &&
                    pfn_gui_frame_cb(ready->jpeg_buffer, ready->total_len, claimed_seq);

    /* Do not recycle READY while the GUI callback is deciding whether to accept it. */
    if (ready->state != NAVI_SLOT_READY || ready->frame_seq != claimed_seq)
    {
        ready->gui_claimed = false;
        s_gui_msg_pending = false;
        navi_queue_gui_wakeup_if_needed();
        return;
    }

    if (!accepted)
    {
        ready->gui_claimed = false;
        if (s_ready_slot == ready_idx)
        {
            s_ready_slot = -1;
        }
        navi_reset_slot_runtime(ready);
        s_gui_msg_pending = false;
        navi_queue_gui_wakeup_if_needed();
        return;
    }

    int old_use_idx = s_use_slot;
    ready->gui_claimed = false;
    ready->state = NAVI_SLOT_USE;
    s_use_slot = ready_idx;
    if (s_ready_slot == ready_idx)
    {
        s_ready_slot = -1;
    }
    s_gui_msg_pending = false;

    if (old_use_idx >= 0 && old_use_idx < NAVI_FRAME_SLOTS && old_use_idx != ready_idx)
    {
        navi_reset_slot_runtime(&s_frame_slots[old_use_idx]);
    }

    s_perf_gui_frames++;
    navi_perf_report();
    navi_queue_gui_wakeup_if_needed();
}

static bool navi_publish_ready(navi_frame_slot_t *slot)
{
    int slot_idx = (int)(slot - s_frame_slots);
    if (slot_idx < 0 || slot_idx >= NAVI_FRAME_SLOTS)
    {
        return false;
    }

    int old_ready_idx = s_ready_slot;
    if (old_ready_idx >= 0 && old_ready_idx < NAVI_FRAME_SLOTS && old_ready_idx != slot_idx)
    {
        navi_frame_slot_t *old_ready = &s_frame_slots[old_ready_idx];
        if (!old_ready->gui_claimed)
        {
            s_perf_ready_drops++;
            navi_reset_slot_runtime(old_ready);
        }
    }

    slot->state = NAVI_SLOT_READY;
    s_ready_slot = slot_idx;

    if (s_gui_msg_pending)
    {
        return true;
    }

    gui_msg_t msg = { .event = GUI_EVENT_USER_DEFINE, .cb = navi_gui_consume_ready };
    if (!gui_send_msg_to_server(&msg))
    {
        APP_PRINT_ERROR1("Navi GUI queue failed: seq=%u", slot->frame_seq);
        if (s_ready_slot == slot_idx)
        {
            s_ready_slot = -1;
        }
        slot->state = NAVI_SLOT_RECEIVING;
        return false;
    }

    s_gui_msg_pending = true;
    return true;
}

static void navi_send_l2(uint8_t key, const uint8_t *value, uint16_t vlen);
static void navi_send_l1_ack(uint16_t seq);
static void navi_send_l1_error(uint16_t seq);
static void navi_handle_l2_frame(const uint8_t *data, uint16_t len);
static void navi_handle_open(const uint8_t *value, uint16_t vlen);
static void navi_handle_close(uint8_t reason);
static void navi_handle_frame_packet(const uint8_t *data, uint16_t len);
static void navi_detect_gaps_and_report(navi_frame_slot_t *slot);
static void navi_check_frame_complete(navi_frame_slot_t *slot);
static void navi_reset_session(void);

static void navi_log_link_info_before_frame(void)
{
    uint8_t conn_id;
    uint16_t conn_interval = 0;
    uint16_t conn_latency = 0;
    uint16_t conn_timeout = 0;
    uint16_t mtu_size = 0;
    uint8_t tx_phy = 0;
    uint8_t rx_phy = 0;

    if (!s_first_frame_link_info_pending || s_conn_handle == 0xFFFF ||
        !le_get_conn_id_by_handle(s_conn_handle, &conn_id))
    {
        return;
    }

    le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_latency, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_timeout, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu_size, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_TX_PHY_TYPE, &tx_phy, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_RX_PHY_TYPE, &rx_phy, conn_id);

    DBG_DIRECT("Navi BLE first frame: conn=%u interval=%u(%u us) "
               "latency=%u timeout=%u ms mtu=%u tx_phy=%u rx_phy=%u "
               "max_ll_pdu=251 max_att_value=%u",
               conn_id,
               conn_interval,
               conn_interval * 1250U,
               conn_latency,
               conn_timeout * 10U,
               mtu_size,
               tx_phy,
               rx_phy,
               mtu_size >= 3 ? mtu_size - 3 : 0);
    s_first_frame_link_info_pending = false;
}

static void navi_request_high_throughput(void)
{
    uint8_t conn_id;

    if (s_conn_handle == 0xFFFF || !le_get_conn_id_by_handle(s_conn_handle, &conn_id))
    {
        DBG_DIRECT("Navi BLE high throughput: cannot resolve conn_id");
        return;
    }

    s_conn_id = conn_id;

    uint16_t conn_interval = 0;
    uint16_t conn_latency = 0;
    uint16_t conn_timeout = 0;
    uint16_t mtu_size = 0;
    uint8_t tx_phy = 0;
    uint8_t rx_phy = 0;

    le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &conn_interval, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_LATENCY, &conn_latency, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_TIMEOUT, &conn_timeout, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu_size, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_TX_PHY_TYPE, &tx_phy, conn_id);
    le_get_conn_param(GAP_PARAM_CONN_RX_PHY_TYPE, &rx_phy, conn_id);

    DBG_DIRECT("Navi BLE before transfer: conn=%u interval=%u(%u us) "
               "latency=%u timeout=%u ms mtu=%u tx_phy=%u rx_phy=%u",
               conn_id,
               conn_interval,
               conn_interval * 1250U,
               conn_latency,
               conn_timeout * 10U,
               mtu_size,
               tx_phy,
               rx_phy);
    s_first_frame_link_info_pending = true;

    T_GAP_CAUSE conn_cause = GAP_CAUSE_SUCCESS;
    if (conn_interval < NAVI_CONN_INTERVAL_MIN || conn_interval > NAVI_CONN_INTERVAL_MAX ||
        conn_latency != NAVI_CONN_LATENCY)
    {
        conn_cause = le_update_conn_param(conn_id,
                                          NAVI_CONN_INTERVAL_MIN,
                                          NAVI_CONN_INTERVAL_MAX,
                                          NAVI_CONN_LATENCY,
                                          NAVI_CONN_TIMEOUT,
                                          NAVI_CE_LENGTH_MIN,
                                          NAVI_CE_LENGTH_MAX);
    }

    T_GAP_CAUSE dle_cause = le_set_data_len(conn_id, NAVI_DLE_TX_OCTETS, NAVI_DLE_TX_TIME);
    T_GAP_CAUSE phy_cause = GAP_CAUSE_SUCCESS;
    if (tx_phy != GAP_PHYS_2M || rx_phy != GAP_PHYS_2M)
    {
        phy_cause = le_set_phy(conn_id,
                               0,
                               GAP_PHYS_PREFER_2M_BIT,
                               GAP_PHYS_PREFER_2M_BIT,
                               GAP_PHYS_OPTIONS_CODED_PREFER_NO);
    }

    DBG_DIRECT("Navi BLE request: conn=%u target_interval=7.5ms interval_update=%u "
               "conn_cause=0x%X dle_cause=0x%X phy_update=%u phy_cause=0x%X",
               conn_id,
               conn_interval < NAVI_CONN_INTERVAL_MIN || conn_interval > NAVI_CONN_INTERVAL_MAX ||
                   conn_latency != NAVI_CONN_LATENCY,
               conn_cause,
               dle_cause,
               tx_phy != GAP_PHYS_2M || rx_phy != GAP_PHYS_2M,
               phy_cause);
}

/**
 * L1 format: 0xAB | ctrl | len(BE) | crc(BE) | seq(BE) | payload
 */
static void navi_send_l1_frame(uint8_t ctrl, uint16_t seq, const uint8_t *payload, uint16_t pay_len)
{
    if (s_conn_handle == 0xFFFF)
    {
        return;
    }

    uint8_t buf[NAVI_L1_HEADER_LEN + NAVI_L1_MAX_PAYLOAD];
    uint16_t crc = navi_crc16(payload, pay_len);

    buf[0] = NAVI_L1_SYNC;
    buf[1] = ctrl;
    buf[2] = HI_16(pay_len);
    buf[3] = LO_16(pay_len);
    buf[4] = HI_16(crc);
    buf[5] = LO_16(crc);
    buf[6] = HI_16(seq);
    buf[7] = LO_16(seq);
    memcpy(buf + NAVI_L1_HEADER_LEN, payload, pay_len);

    gatt_svc_send_data(s_conn_handle,
                       s_cid,
                       navi_service_id,
                       NAVI_ATTR_CTRL_RX_VALUE,
                       buf,
                       NAVI_L1_HEADER_LEN + pay_len,
                       GATT_PDU_TYPE_ANY);
}

static void navi_send_l1_ack(uint16_t seq)
{
    navi_send_l1_frame(NAVI_L1_CTRL_ACK, seq, NULL, 0);
}

static void navi_send_l1_error(uint16_t seq)
{
    navi_send_l1_frame(NAVI_L1_CTRL_ERROR, seq, NULL, 0);
}

static void navi_send_l2(uint8_t key, const uint8_t *value, uint16_t vlen)
{
    uint8_t buf[NAVI_L2_HEADER_LEN + 6 * NAVI_MAX_GAPS + 3];

    if (vlen > sizeof(buf) - NAVI_L2_HEADER_LEN)
    {
        APP_PRINT_ERROR1("L2 TX value too large: %u", vlen);
        return;
    }

    buf[0] = NAVI_L2_CMD;
    buf[1] = 0x00; /* reserved */
    buf[2] = key;
    buf[3] = HI_16(vlen);
    buf[4] = LO_16(vlen);
    if (vlen > 0 && value != NULL)
    {
        memcpy(buf + NAVI_L2_HEADER_LEN, value, vlen);
    }

    uint16_t total_len = NAVI_L2_HEADER_LEN + vlen;

    if (key <= NAVI_KEY_ERROR)
    {
        s_l1_tx_seq++;
        navi_send_l1_frame(NAVI_L1_CTRL_DATA, s_l1_tx_seq, buf, total_len);
    }
    else
    {
        gatt_svc_send_data(s_conn_handle,
                           s_cid,
                           navi_service_id,
                           NAVI_ATTR_DATA_RX_VALUE,
                           buf,
                           total_len,
                           GATT_PDU_TYPE_ANY);
    }
}

static void navi_l1_parse(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        if (!s_l1_synced)
        {
            if (data[i] == NAVI_L1_SYNC)
            {
                s_l1_synced = true;
                s_l1_rx_len = 0;
                s_l1_rx_buf[s_l1_rx_len++] = data[i];
            }
            continue;
        }

        s_l1_rx_buf[s_l1_rx_len++] = data[i];

        if (s_l1_rx_len >= NAVI_L1_HEADER_LEN)
        {
            uint8_t ctrl = s_l1_rx_buf[1];
            uint16_t paylen = BIG16(s_l1_rx_buf[2], s_l1_rx_buf[3]);

            if (paylen > NAVI_L1_MAX_PAYLOAD)
            {
                APP_PRINT_ERROR1("L1: payload too big %u", paylen);
                s_l1_synced = false;
                s_l1_rx_len = 0;
                continue;
            }

            uint16_t total_needed = NAVI_L1_HEADER_LEN + paylen;

            if (s_l1_rx_len >= total_needed)
            {
                uint16_t rx_crc = BIG16(s_l1_rx_buf[4], s_l1_rx_buf[5]);
                uint16_t rx_seq = BIG16(s_l1_rx_buf[6], s_l1_rx_buf[7]);
                uint8_t *payload = s_l1_rx_buf + NAVI_L1_HEADER_LEN;

                uint16_t calc_crc = navi_crc16(payload, paylen);

                if (rx_crc != calc_crc)
                {
                    APP_PRINT_ERROR2("L1 CRC fail: rx=0x%04X calc=0x%04X", rx_crc, calc_crc);
                    navi_send_l1_error(rx_seq);
                }
                else
                {
                    s_l1_rx_seq = rx_seq;

                    switch (ctrl)
                    {
                    case NAVI_L1_CTRL_DATA:
                        navi_send_l1_ack(rx_seq);
                        if (paylen >= NAVI_L2_HEADER_LEN)
                        {
                            navi_handle_l2_frame(payload, paylen);
                        }
                        break;

                    case NAVI_L1_CTRL_ACK:
                        APP_PRINT_INFO1("L1 ACK received: seq=%u", rx_seq);
                        break;

                    case NAVI_L1_CTRL_ERROR:
                        APP_PRINT_INFO1("L1 ERROR received: seq=%u", rx_seq);
                        break;

                    default:
                        break;
                    }
                }

                s_l1_synced = false;
                s_l1_rx_len = 0;

                if (s_l1_rx_len > 0)
                {
                    memmove(s_l1_rx_buf, s_l1_rx_buf + total_needed, s_l1_rx_len - total_needed);
                    s_l1_rx_len -= total_needed;
                }
            }
        }
    }
}

static void navi_handle_l2_frame(const uint8_t *data, uint16_t len)
{
    if (len < NAVI_L2_HEADER_LEN)
    {
        APP_PRINT_ERROR1("L2 frame too short: %u", len);
        return;
    }

    uint8_t cmd = data[0];
    uint8_t key = data[2];
    uint16_t vlen = BIG16(data[3], data[4]);
    const uint8_t *value = data + NAVI_L2_HEADER_LEN;

    if (vlen > (uint16_t)(len - NAVI_L2_HEADER_LEN))
    {
        APP_PRINT_ERROR2("L2 truncated: vlen=%u available=%u", vlen, len - NAVI_L2_HEADER_LEN);
        return;
    }

    if (cmd != NAVI_L2_CMD)
    {
        APP_PRINT_ERROR1("L2 unknown cmd: 0x%02X", cmd);
        return;
    }

    switch (key)
    {
    case NAVI_KEY_OPEN:
        navi_handle_open(value, vlen);
        break;

    case NAVI_KEY_CLOSE:
        if (vlen >= 1)
        {
            navi_handle_close(value[0]);
        }
        else
        {
            navi_handle_close(NAVI_CLOSE_NORMAL);
        }
        break;

    case NAVI_KEY_FRAME:
        APP_PRINT_ERROR0("FRAME on control channel, ignored");
        break;

    default:
        break;
    }
}

static void navi_handle_data_channel(const uint8_t *data, uint16_t len)
{
    navi_handle_frame_packet(data, len);
}

static void navi_handle_open(const uint8_t *value, uint16_t vlen)
{
    navi_open_params_t params;
    uint8_t ack_value[5];
    uint16_t credit;

    if (vlen < 11)
    {
        APP_PRINT_ERROR1("OPEN: v2 requires 11-byte value, got %u", vlen);
        ack_value[0] = NAVI_RESULT_UNSUPPORTED;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        return;
    }

    params.width = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
    params.height = (uint16_t)value[2] | ((uint16_t)value[3] << 8);
    params.fps = value[4];
    params.quality = value[5];
    params.flow_ctrl_enable = value[6] & 0x01;
    params.protocol_version = value[7];
    params.features = value[8];
    params.max_packet = (uint16_t)value[9] | ((uint16_t)value[10] << 8);

    if (params.protocol_version != NAVI_PROTOCOL_VERSION ||
        (params.features & NAVI_REQUIRED_FEATURES) != NAVI_REQUIRED_FEATURES ||
        params.flow_ctrl_enable == 0 || params.max_packet < NAVI_MIN_PACKET ||
        params.max_packet > NAVI_MAX_PACKET)
    {
        APP_PRINT_ERROR3("OPEN unsupported: version=%u features=0x%02X packet=%u",
                         params.protocol_version,
                         params.features,
                         params.max_packet);
        ack_value[0] = NAVI_RESULT_UNSUPPORTED;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        return;
    }

    if (params.width == 0 || params.height == 0 || params.height > 1024 || params.width > 1024)
    {
        ack_value[0] = NAVI_RESULT_UNSUPPORTED;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        return;
    }

    if (params.fps == 0 || params.fps > 30)
    {
        ack_value[0] = NAVI_RESULT_UNSUPPORTED;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        return;
    }

    if (s_state == NAVI_STATE_ACTIVE)
    {
        ack_value[0] = NAVI_RESULT_BUSY;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        APP_PRINT_WARN0("OPEN rejected: session already active");
        return;
    }

    memcpy(&s_open_params, &params, sizeof(navi_open_params_t));

    uint32_t first_capacity = params.max_packet - NAVI_FRAME_FIRST_HEADER_LEN;
    uint32_t packet_capacity = params.max_packet - NAVI_FRAME_HEADER_LEN;
    uint32_t remaining = NAVI_MAX_JPEG_SIZE - first_capacity;
    uint32_t frame_credits = 1U + (remaining + packet_capacity - 1U) / packet_capacity;
    uint32_t total_credits = frame_credits * NAVI_FRAME_SLOTS;
    if (total_credits > UINT16_MAX)
    {
        ack_value[0] = NAVI_RESULT_UNSUPPORTED;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        return;
    }
    credit = (uint16_t)total_credits;

    s_credit = credit;
    s_state = NAVI_STATE_ACTIVE;
    s_current_slot = 0;
    s_first_frame_link_info_pending = true;

    navi_request_high_throughput();

    /* Re-establish fixed storage addresses before clearing per-frame metadata. */
    if (!navi_psram_init())
    {
        s_state = NAVI_STATE_IDLE;
        ack_value[0] = NAVI_RESULT_LOW_MEMORY;
        memset(ack_value + 1, 0, 4);
        navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
        return;
    }
    navi_reset_frame_slots();
    s_ready_slot = -1;
    s_use_slot = -1;
    s_gui_msg_pending = false;
    navi_perf_reset();

    ack_value[0] = NAVI_RESULT_OK;
    ack_value[1] = LO_16(params.max_packet);
    ack_value[2] = HI_16(params.max_packet);
    ack_value[3] = LO_16(credit);
    ack_value[4] = HI_16(credit);

    navi_send_l2(NAVI_KEY_ACK, ack_value, 5);
}

static void navi_handle_close(uint8_t reason)
{
    APP_PRINT_INFO1("CLOSE: reason=%u", reason);
    navi_reset_session();
}

void navi_projection_close(void)
{
    if (s_state != NAVI_STATE_IDLE)
    {
        uint8_t err[2] = { NAVI_ERR_TIMEOUT, 0 };
        navi_send_l2(NAVI_KEY_ERROR, err, 2);
    }
    navi_reset_session();
}

static void navi_reset_session(void)
{
    s_state = NAVI_STATE_IDLE;
    s_credit = 0;
    s_current_slot = 0;
    s_l1_synced = false;
    s_l1_rx_len = 0;
    s_l1_tx_seq = 0;
    s_l1_rx_seq = 0;
    s_last_data_time_ms = 0;
    s_first_frame_link_info_pending = false;
    navi_psram_init();
    memset(&s_open_params, 0, sizeof(s_open_params));
    navi_reset_frame_slots();
    s_ready_slot = -1;
    s_use_slot = -1;
    s_gui_msg_pending = false;
}

static int navi_find_slot(uint16_t frame_seq)
{
    int free_slot = -1;

    for (int i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        navi_frame_slot_t *slot = &s_frame_slots[i];
        if (slot->state == NAVI_SLOT_RECEIVING && slot->frame_seq == frame_seq)
        {
            return i;
        }
        if (slot->state == NAVI_SLOT_FREE && free_slot < 0)
        {
            free_slot = i;
        }
    }

    if (free_slot < 0)
    {
        APP_PRINT_WARN1("No FREE frame slot for seq=%u", frame_seq);
    }
    return free_slot;
}

static uint32_t navi_get_time_ms(void)
{
    extern uint32_t sys_timestamp_get_us(void);
    return sys_timestamp_get_us() / 1000;
}

/**
 * @brief Parse one protocol-v2 FFD3 packet.
 *
 * Every GATT write is independently parseable. The common header is
 * frame_seq(2B BE) | chunk_offset(3B BE) | total_len(3B BE). Offset zero
 * additionally carries frame_crc32(4B BE) before the JPEG bytes.
 */
static void navi_handle_frame_packet(const uint8_t *data, uint16_t len)
{
    if (s_state != NAVI_STATE_ACTIVE)
    {
        APP_PRINT_WARN0("FRAME received but not ACTIVE, ignored");
        return;
    }

    navi_log_link_info_before_frame();

    if (len <= NAVI_FRAME_HEADER_LEN || len > s_open_params.max_packet)
    {
        APP_PRINT_ERROR1("FRAME packet invalid length: %u", len);
        return;
    }

    uint16_t frame_seq = BIG16(data[0], data[1]);
    uint32_t chunk_offset = BIG24(data[2], data[3], data[4]);
    uint32_t total_len = BIG24(data[5], data[6], data[7]);
    uint16_t header_len = NAVI_FRAME_HEADER_LEN;
    uint32_t packet_crc32 = 0;

    if (chunk_offset == 0)
    {
        if (len <= NAVI_FRAME_FIRST_HEADER_LEN)
        {
            APP_PRINT_ERROR1("FRAME first packet too short: %u", len);
            return;
        }
        packet_crc32 = BIG32(data[8], data[9], data[10], data[11]);
        header_len = NAVI_FRAME_FIRST_HEADER_LEN;
    }

    uint16_t data_len = len - header_len;
    const uint8_t *chunk_data = data + header_len;
    s_last_data_time_ms = navi_get_time_ms();

    if (total_len == 0 || total_len > NAVI_MAX_JPEG_SIZE)
    {
        APP_PRINT_ERROR1("FRAME total_len invalid: %u", total_len);
        uint8_t err[2] = { NAVI_ERR_BUFFER_OVERFLOW, (uint8_t)(frame_seq & 0xFF) };
        navi_send_l2(NAVI_KEY_ERROR, err, 2);
        return;
    }

    if (chunk_offset >= total_len || data_len > total_len - chunk_offset)
    {
        APP_PRINT_ERROR3(
            "FRAME offset OOB: off=%u + len=%u > total=%u", chunk_offset, data_len, total_len);
        return;
    }

    int slot_idx = navi_find_slot(frame_seq);
    if (slot_idx < 0)
    {
        return;
    }

    navi_frame_slot_t *slot = &s_frame_slots[slot_idx];
    if (slot->total_len == 0 || slot->frame_seq != frame_seq)
    {
        if (slot->jpeg_buffer == NULL || slot->chunk_bitmap == NULL)
        {
            APP_PRINT_ERROR1("FRAME slot has NULL storage: slot=%d", slot_idx);
            return;
        }

        memset(slot->jpeg_buffer, 0, total_len + NAVI_JPG_HEADER_SIZE);
        memset(slot->chunk_bitmap, 0, NAVI_COVERAGE_SIZE);
        slot->frame_seq = frame_seq;
        slot->total_len = total_len;
        slot->complete = false;
        slot->terminal_seen = false;
        slot->received_bytes = 0;
        slot->expected_crc32 = 0;
        slot->crc32_received = false;
        slot->credits_consumed = 0;
        slot->pending_retx_credits = 0;
        slot->state = NAVI_SLOT_RECEIVING;
        s_current_slot = slot_idx;
    }
    else if (slot->total_len != total_len)
    {
        APP_PRINT_ERROR3(
            "FRAME total changed: seq=%u old=%u new=%u", frame_seq, slot->total_len, total_len);
        return;
    }

    if (chunk_offset == 0)
    {
        if (slot->crc32_received && slot->expected_crc32 != packet_crc32)
        {
            APP_PRINT_ERROR1("FRAME CRC metadata changed: seq=%u", frame_seq);
            return;
        }
        slot->expected_crc32 = packet_crc32;
        slot->crc32_received = true;
    }

    uint32_t unique_bytes = 0;
    bool fills_reported_gap = slot->terminal_seen;
    for (uint32_t i = 0; i < data_len; i++)
    {
        uint32_t byte_offset = chunk_offset + i;
        uint32_t bitmap_index = byte_offset >> 3;
        uint8_t bitmap_mask = (uint8_t)(1U << (byte_offset & 7));
        if ((slot->chunk_bitmap[bitmap_index] & bitmap_mask) == 0)
        {
            unique_bytes++;
        }
    }

    if (unique_bytes > 0 && !fills_reported_gap && s_credit == 0)
    {
        APP_PRINT_WARN0("FRAME received but no credit, dropped");
        return;
    }

    for (uint32_t i = 0; i < data_len; i++)
    {
        uint32_t byte_offset = chunk_offset + i;
        slot->chunk_bitmap[byte_offset >> 3] |= (uint8_t)(1U << (byte_offset & 7));
    }

    memcpy(slot->jpeg_buffer + NAVI_JPG_HEADER_SIZE + chunk_offset, chunk_data, data_len);
    slot->received_bytes += unique_bytes;

    if (unique_bytes > 0 && !fills_reported_gap)
    {
        s_credit--;
        slot->credits_consumed++;
        slot->pending_retx_credits++;
    }

    if (chunk_offset + data_len == slot->total_len)
    {
        slot->terminal_seen = true;
    }

    if (slot->received_bytes == slot->total_len || slot->terminal_seen)
    {
        navi_detect_gaps_and_report(slot);
    }
}

static void navi_detect_gaps_and_report(navi_frame_slot_t *slot)
{
    if (slot == NULL)
    {
        return;
    }
    if (slot->total_len == 0)
    {
        return;
    }

    /* Build exact byte-range gaps from the coverage bitset. */
    navi_gap_t gaps[NAVI_MAX_GAPS];
    uint8_t gap_count = 0;
    uint32_t offset = 0;

    while (offset < slot->total_len && gap_count < NAVI_MAX_GAPS)
    {
        uint8_t mask = (uint8_t)(1U << (offset & 7));
        if (slot->chunk_bitmap[offset >> 3] & mask)
        {
            offset++;
            continue;
        }

        gaps[gap_count].gap_start = offset;
        do
        {
            offset++;
            if (offset >= slot->total_len)
            {
                break;
            }
            mask = (uint8_t)(1U << (offset & 7));
        } while ((slot->chunk_bitmap[offset >> 3] & mask) == 0);

        gaps[gap_count].gap_end = offset;
        gap_count++;
    }

    uint8_t report_buf[3 + 6 * NAVI_MAX_GAPS];
    report_buf[0] = HI_16(slot->frame_seq);
    report_buf[1] = LO_16(slot->frame_seq);
    report_buf[2] = gap_count;

    for (uint8_t g = 0; g < gap_count; g++)
    {
        report_buf[3 + g * 6 + 0] = HI_24(gaps[g].gap_start);
        report_buf[3 + g * 6 + 1] = MID_24(gaps[g].gap_start);
        report_buf[3 + g * 6 + 2] = LO_24(gaps[g].gap_start);
        report_buf[3 + g * 6 + 3] = HI_24(gaps[g].gap_end);
        report_buf[3 + g * 6 + 4] = MID_24(gaps[g].gap_end);
        report_buf[3 + g * 6 + 5] = LO_24(gaps[g].gap_end);

        APP_PRINT_INFO2("Gap: [%u, %u)", gaps[g].gap_start, gaps[g].gap_end);
    }

    uint16_t report_len = 3 + gap_count * 6;
    navi_send_l2(NAVI_KEY_REPORT, report_buf, report_len);

    if (gap_count > 0 && slot->pending_retx_credits > 0)
    {
        uint16_t credit_return = slot->pending_retx_credits;
        s_credit += credit_return;
        slot->credits_consumed -= credit_return;
        slot->pending_retx_credits = 0;

        uint8_t credit_buf[2] = { HI_16(credit_return), LO_16(credit_return) };
        navi_send_l2(NAVI_KEY_CREDIT, credit_buf, 2);
    }

    if (gap_count == 0)
    {
        slot->complete = true;
        navi_check_frame_complete(slot);
    }

    s_report_frame_seq = slot->frame_seq;
}

static void navi_check_frame_complete(navi_frame_slot_t *slot)
{
    /* Construct HoneyGUI JPEG header at the start of the PSRAM buffer.
     * Layout: gui_rgb_data_head_t(8) | size(4) | dummy(4) | JPEG data */
    gui_rgb_data_head_t *head = (gui_rgb_data_head_t *)slot->jpeg_buffer;
    memset(head, 0, NAVI_JPG_HEADER_SIZE);
    head->jpeg = 1;
    head->type = 12;
    head->w = (int16_t)s_open_params.width;
    head->h = (int16_t)s_open_params.height;
    *(uint32_t *)(slot->jpeg_buffer + 8) = slot->total_len;

    const uint8_t *jpeg = slot->jpeg_buffer + NAVI_JPG_HEADER_SIZE;
    if (!slot->crc32_received)
    {
        APP_PRINT_ERROR1("Frame missing CRC32 metadata: seq=%u", slot->frame_seq);
        uint8_t err[2] = { NAVI_ERR_CRC_MISMATCH, (uint8_t)(slot->frame_seq & 0xFF) };
        navi_send_l2(NAVI_KEY_ERROR, err, 2);
        uint16_t credit_return = slot->credits_consumed;
        s_credit += credit_return;
        if (credit_return > 0)
        {
            uint8_t credit_buf[2] = { HI_16(credit_return), LO_16(credit_return) };
            navi_send_l2(NAVI_KEY_CREDIT, credit_buf, 2);
        }
        s_perf_crc_errors++;
        navi_reset_slot_runtime(slot);
        return;
    }

    uint32_t actual_crc32 = navi_crc32(jpeg, slot->total_len);
    if (actual_crc32 != slot->expected_crc32)
    {
        DBG_DIRECT("Navi CRC32 mismatch: seq=%u expected=0x%08X actual=0x%08X",
                   slot->frame_seq,
                   slot->expected_crc32,
                   actual_crc32);
        uint8_t err[2] = { NAVI_ERR_CRC_MISMATCH, (uint8_t)(slot->frame_seq & 0xFF) };
        navi_send_l2(NAVI_KEY_ERROR, err, 2);
        uint16_t credit_return = slot->credits_consumed;
        s_credit += credit_return;
        if (credit_return > 0)
        {
            uint8_t credit_buf[2] = { HI_16(credit_return), LO_16(credit_return) };
            navi_send_l2(NAVI_KEY_CREDIT, credit_buf, 2);
        }
        s_perf_crc_errors++;
        navi_reset_slot_runtime(slot);
        return;
    }

    if (slot->total_len < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
        jpeg[slot->total_len - 2] != 0xFF || jpeg[slot->total_len - 1] != 0xD9)
    {
        APP_PRINT_ERROR1("Frame JPEG marker check failed: seq=%u", slot->frame_seq);
        uint8_t err[2] = { NAVI_ERR_DECODE_FAILED, (uint8_t)(slot->frame_seq & 0xFF) };
        navi_send_l2(NAVI_KEY_ERROR, err, 2);
        uint16_t credit_return = slot->credits_consumed;
        s_credit += credit_return;
        if (credit_return > 0)
        {
            uint8_t credit_buf[2] = { HI_16(credit_return), LO_16(credit_return) };
            navi_send_l2(NAVI_KEY_CREDIT, credit_buf, 2);
        }
        navi_reset_slot_runtime(slot);
        return;
    }

    /* CPU produced this buffer and both GUI and JPU consume it. Clean writes to
     * PSRAM while retaining the cache lines for subsequent CPU reads. */
    SCB_CleanDCache_by_Addr((uint32_t *)slot->jpeg_buffer,
                            (int32_t)(NAVI_JPG_HEADER_SIZE + slot->total_len));

    if (pfn_frame_ready_cb)
    {
        pfn_frame_ready_cb(jpeg, slot->total_len, slot->frame_seq);
    }

    s_perf_rx_frames++;
    s_perf_rx_bytes += slot->total_len;

    if (!navi_publish_ready(slot))
    {
        uint16_t failed_seq = slot->frame_seq;
        uint16_t credit_return = slot->credits_consumed;
        uint8_t err[2] = { NAVI_ERR_TIMEOUT, (uint8_t)(failed_seq & 0xFF) };
        navi_send_l2(NAVI_KEY_ERROR, err, 2);
        s_credit += credit_return;
        if (credit_return > 0)
        {
            uint8_t credit_buf[2] = { HI_16(credit_return), LO_16(credit_return) };
            navi_send_l2(NAVI_KEY_CREDIT, credit_buf, 2);
        }
        navi_reset_slot_runtime(slot);
        return;
    }

    /* Return exactly the credits consumed by unique FRAME writes. */
    uint16_t credit_return = slot->credits_consumed;
    s_credit += credit_return;

    uint8_t credit_buf[2] = { HI_16(credit_return), LO_16(credit_return) };
    navi_send_l2(NAVI_KEY_CREDIT, credit_buf, 2);

    navi_perf_report();
}

/* All navigation characteristics are write-only or notify-only. */
static T_APP_RESULT navi_attr_read_cb(uint16_t conn_handle,
                                      uint16_t cid,
                                      T_SERVER_ID service_id,
                                      uint16_t attrib_index,
                                      uint16_t offset,
                                      uint16_t *p_length,
                                      uint8_t **pp_value)
{
    APP_PRINT_WARN1("navi_attr_read_cb: attr %d not supported", attrib_index);
    return APP_RESULT_ATTR_NOT_FOUND;
}

static T_APP_RESULT navi_attr_write_cb(uint16_t conn_handle,
                                       uint16_t cid,
                                       T_SERVER_ID service_id,
                                       uint16_t attrib_index,
                                       T_WRITE_TYPE write_type,
                                       uint16_t length,
                                       uint8_t *p_value,
                                       P_FUN_EXT_WRITE_IND_POST_PROC *p_write_ind_post_proc)
{
    T_APP_RESULT cause = APP_RESULT_SUCCESS;

    s_conn_handle = conn_handle;
    s_cid = cid;

    switch (attrib_index)
    {
    case NAVI_ATTR_CTRL_TX_VALUE:
        if (length > 0 && p_value != NULL)
        {
            navi_l1_parse(p_value, length);
        }
        break;

    case NAVI_ATTR_DATA_TX_VALUE:
        if (s_state == NAVI_STATE_ACTIVE && length > 0 && p_value != NULL)
        {
            navi_handle_data_channel(p_value, length);
        }
        break;

    default:
        APP_PRINT_ERROR1("navi_attr_write_cb: unknown attr %u", attrib_index);
        cause = APP_RESULT_ATTR_NOT_FOUND;
        break;
    }

    return cause;
}

static void navi_cccd_update_cb(
    uint16_t conn_handle, uint16_t cid, T_SERVER_ID service_id, uint16_t index, uint16_t cccbits)
{
    if ((index == NAVI_ATTR_CTRL_RX_CCCD || index == NAVI_ATTR_DATA_RX_CCCD) &&
        (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY))
    {
        s_conn_handle = conn_handle;
        s_cid = cid;
    }
}

static const T_FUN_GATT_EXT_SERVICE_CBS navi_service_cbs = { navi_attr_read_cb,
                                                             navi_attr_write_cb,
                                                             navi_cccd_update_cb };

uint8_t navi_projection_init(void *service_cb, navi_frame_ready_cb_t frame_cb)
{
    NAVI_CRC_SELF_TEST();

    pfn_navi_general_cb = (P_FUN_EXT_SERVER_GENERAL_CB)service_cb;
    pfn_frame_ready_cb = frame_cb;

    if (false == gatt_svc_add(&navi_service_id,
                              (uint8_t *)navi_projection_tbl,
                              sizeof(navi_projection_tbl),
                              &navi_service_cbs,
                              NULL))
    {
        APP_PRINT_ERROR0("navi_projection_init: gatt_svc_add failed");
        navi_service_id = 0xFF;
        return 0xFF;
    }

    return navi_service_id;
}

navi_state_t navi_projection_get_state(void)
{
    return s_state;
}

uint8_t navi_projection_get_service_id(void)
{
    return navi_service_id;
}

void navi_projection_set_frame_cb(navi_frame_ready_cb_t frame_cb)
{
    pfn_frame_ready_cb = frame_cb;
}

void navi_projection_set_gui_frame_cb(navi_gui_frame_cb_t frame_cb)
{
    pfn_gui_frame_cb = frame_cb;
}
