/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "navi_projection_service.h"
#include "app_flags.h"

#include <string.h>
#include <stdio.h>
#include "app_timer.h"
#include "bt_gatt_svc.h"
#include "def_file.h"
#include "draw_img.h"
#include "gap.h"
#include "gap_conn_le.h"
#include "gap_msg.h"
#include "gui_message.h"
#include "hmi_ble_gap_msg.h"
#include "os_sync.h"
#include "rtl876x.h"
#include "stream_transport.h"
#include "trace.h"

extern uint32_t sys_timestamp_get(void);

#define NAVI_L1_SYNC                       0xAB
#define NAVI_L1_HEADER_LEN                 8U
#define NAVI_L1_MAX_PAYLOAD                248U
#define NAVI_L1_CTRL_DATA                  0x00
#define NAVI_L1_CTRL_ACK                   0x10
#define NAVI_L1_CTRL_ERROR                 0x30

#define NAVI_L2_CMD                        0x11
#define NAVI_L2_HEADER_LEN                 5U
#define NAVI_KEY_OPEN                      0x01
#define NAVI_KEY_ACK                       0x02
#define NAVI_KEY_CLOSE                     0x03
#define NAVI_KEY_ERROR                     0x04
#define NAVI_KEY_CREDIT                    0x06
#define NAVI_KEY_REPORT                    0x07

#define NAVI_RESULT_OK                     0x00
#define NAVI_RESULT_BUSY                   0x01
#define NAVI_RESULT_UNSUPPORTED            0x02
#define NAVI_RESULT_LOW_MEMORY             0x04

#define NAVI_ERR_BUFFER_OVERFLOW           0x01
#define NAVI_ERR_DECODE_FAILED             0x02
#define NAVI_ERR_TIMEOUT                   0x03
#define NAVI_ERR_CRC_MISMATCH              0x05

#define NAVI_FRAME_HEADER_LEN              8U
#define NAVI_FRAME_FIRST_HEADER_LEN        12U
#define NAVI_MAX_PACKET                    244U
#define NAVI_MAX_GAPS                      16U
#define NAVI_MIN_PACKET                    (NAVI_L2_HEADER_LEN + 3U + 6U * NAVI_MAX_GAPS)
#define NAVI_MAX_DECODED_PIXELS            192000UL
#define NAVI_MAX_FRAME_PACKETS             \
    (1U + (NAVI_MAX_JPEG_SIZE - (NAVI_MIN_PACKET - NAVI_FRAME_FIRST_HEADER_LEN) + \
           (NAVI_MIN_PACKET - NAVI_FRAME_HEADER_LEN) - 1U) / \
          (NAVI_MIN_PACKET - NAVI_FRAME_HEADER_LEN))
#define NAVI_SESSION_TIMEOUT_MS            10000UL
#define NAVI_POLL_PERIOD_MS                100UL
#define NAVI_TX_FIFO_DEPTH                 8U
#define NAVI_TX_MAX_LEN                    (NAVI_L1_HEADER_LEN + NAVI_L1_MAX_PAYLOAD)
#define NAVI_RECENT_SEQ_COUNT              3U
#define NAVI_INVALID_CONN_HANDLE           0xFFFF
#define NAVI_INVALID_CID                   0xFFFF
#define NAVI_INVALID_CONN_ID               0xFF
#define NAVI_INVALID_SERVICE_ID            0xFF

#define BIG16(a, b)                        (((uint16_t)(a) << 8) | (uint16_t)(b))
#define BIG24(a, b, c)                     \
    (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))
#define BIG32(a, b, c, d)                  \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define HI_24(v)                           ((uint8_t)(((v) >> 16) & 0xFFU))
#define MID_24(v)                          ((uint8_t)(((v) >> 8) & 0xFFU))
#define LO_24(v)                           ((uint8_t)((v) & 0xFFU))
#define HI_16(v)                           ((uint8_t)(((v) >> 8) & 0xFFU))
#define LO_16(v)                           ((uint8_t)((v) & 0xFFU))

#define NAVI_ATTR_SERVICE_DECL             0U
#define NAVI_ATTR_CTRL_TX_CHAR_DECL        1U
#define NAVI_ATTR_CTRL_TX_VALUE            2U
#define NAVI_ATTR_CTRL_RX_CHAR_DECL        3U
#define NAVI_ATTR_CTRL_RX_VALUE            4U
#define NAVI_ATTR_CTRL_RX_CCCD              5U
#define NAVI_ATTR_DATA_TX_CHAR_DECL        6U
#define NAVI_ATTR_DATA_TX_VALUE            7U
#define NAVI_ATTR_DATA_RX_CHAR_DECL        8U
#define NAVI_ATTR_DATA_RX_VALUE            9U
#define NAVI_ATTR_DATA_RX_CCCD              10U

typedef enum
{
    NAVI_SLOT_FREE = 0,
    NAVI_SLOT_RECEIVING,
    NAVI_SLOT_READY
} navi_slot_state_t;

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    uint8_t quality;
    uint8_t flow_ctrl_enable;
    uint8_t protocol_version;
    uint8_t features;
    uint16_t max_packet;
} navi_open_params_t;

typedef struct
{
    uint16_t frame_seq;
    uint32_t total_len;
    uint32_t received_bytes;
    uint32_t expected_crc32;
    uint32_t last_rx_ms;
    uint16_t credits_consumed;
    uint8_t *jpeg_buffer;
    stp_frame_t buf_desc;      /* transport buffer the frame is assembled into */
    bool buf_held;             /* true while buf_desc is owned by this slot     */
    navi_slot_state_t state;
    bool crc32_received;
    bool validated;
    bool gui_claimed;
} navi_frame_slot_t;

typedef enum
{
    NAVI_TX_EFFECT_NONE = 0,
    NAVI_TX_EFFECT_CREDIT
} navi_tx_effect_t;

typedef struct
{
    uint16_t attrib_idx;
    uint16_t len;
    uint16_t credit_amount;
    navi_tx_effect_t effect;
    uint8_t data[NAVI_TX_MAX_LEN];
} navi_tx_item_t;

static const T_ATTRIB_APPL navi_projection_tbl[] =
{
    {
        ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_LE,
        { LO_WORD(GATT_UUID_PRIMARY_SERVICE), HI_WORD(GATT_UUID_PRIMARY_SERVICE),
          LO_WORD(NAVI_SERVICE_UUID), HI_WORD(NAVI_SERVICE_UUID) },
        UUID_16BIT_SIZE, NULL, GATT_PERM_READ
    },
    {
        ATTRIB_FLAG_VALUE_INCL,
        { LO_WORD(GATT_UUID_CHARACTERISTIC), HI_WORD(GATT_UUID_CHARACTERISTIC),
          GATT_CHAR_PROP_WRITE },
        1, NULL, GATT_PERM_READ
    },
    {
        ATTRIB_FLAG_VALUE_APPL,
        { LO_WORD(NAVI_CHAR_CTRL_TX_UUID), HI_WORD(NAVI_CHAR_CTRL_TX_UUID) },
        0, NULL, GATT_PERM_WRITE
    },
    {
        ATTRIB_FLAG_VALUE_INCL,
        { LO_WORD(GATT_UUID_CHARACTERISTIC), HI_WORD(GATT_UUID_CHARACTERISTIC),
          GATT_CHAR_PROP_NOTIFY },
        1, NULL, GATT_PERM_READ
    },
    {
        ATTRIB_FLAG_VALUE_APPL,
        { LO_WORD(NAVI_CHAR_CTRL_RX_UUID), HI_WORD(NAVI_CHAR_CTRL_RX_UUID) },
        0, NULL, GATT_PERM_NONE
    },
    {
        ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL,
        { LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG), HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
          LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT), HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT) },
        2, NULL, GATT_PERM_READ | GATT_PERM_WRITE
    },
    {
        ATTRIB_FLAG_VALUE_INCL,
        { LO_WORD(GATT_UUID_CHARACTERISTIC), HI_WORD(GATT_UUID_CHARACTERISTIC),
          GATT_CHAR_PROP_WRITE_NO_RSP },
        1, NULL, GATT_PERM_READ
    },
    {
        ATTRIB_FLAG_VALUE_APPL,
        { LO_WORD(NAVI_CHAR_DATA_TX_UUID), HI_WORD(NAVI_CHAR_DATA_TX_UUID) },
        0, NULL, GATT_PERM_WRITE
    },
    {
        ATTRIB_FLAG_VALUE_INCL,
        { LO_WORD(GATT_UUID_CHARACTERISTIC), HI_WORD(GATT_UUID_CHARACTERISTIC),
          GATT_CHAR_PROP_NOTIFY },
        1, NULL, GATT_PERM_READ
    },
    {
        ATTRIB_FLAG_VALUE_APPL,
        { LO_WORD(NAVI_CHAR_DATA_RX_UUID), HI_WORD(NAVI_CHAR_DATA_RX_UUID) },
        0, NULL, GATT_PERM_NONE
    },
    {
        ATTRIB_FLAG_VALUE_INCL | ATTRIB_FLAG_CCCD_APPL,
        { LO_WORD(GATT_UUID_CHAR_CLIENT_CONFIG), HI_WORD(GATT_UUID_CHAR_CLIENT_CONFIG),
          LO_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT), HI_WORD(GATT_CLIENT_CHAR_CONFIG_DEFAULT) },
        2, NULL, GATT_PERM_READ | GATT_PERM_WRITE
    },
};

static T_SERVER_ID s_service_id = NAVI_INVALID_SERVICE_ID;
static navi_state_t s_state = NAVI_STATE_IDLE;
static navi_open_params_t s_open;
static stp_transport_t *s_transport;
static navi_frame_slot_t s_slots[NAVI_FRAME_SLOTS];
static int8_t s_ready_slot = -1;
static uint32_t s_session_epoch;
static uint32_t s_last_data_ms;
static uint16_t s_credit;
static uint16_t s_credit_limit;

static uint16_t s_conn_handle = NAVI_INVALID_CONN_HANDLE;
static uint16_t s_cid = NAVI_INVALID_CID;
static uint8_t s_conn_id = NAVI_INVALID_CONN_ID;
static bool s_link_alive;
static bool s_ctrl_cccd_enabled;
static bool s_data_cccd_enabled;
static bool s_address_ready;
static uint8_t s_public_address[6];
static uint16_t s_recent_seq[NAVI_RECENT_SEQ_COUNT];
static uint8_t s_recent_seq_count;
static uint8_t s_recent_seq_next;

static uint8_t s_l1_rx_buf[NAVI_L1_HEADER_LEN + NAVI_L1_MAX_PAYLOAD];
static uint16_t s_l1_rx_len;
static uint16_t s_l1_expected;
static uint16_t s_l1_tx_seq;

static navi_tx_item_t s_tx_fifo[NAVI_TX_FIFO_DEPTH];
static uint8_t s_tx_head;
static uint8_t s_tx_tail;
static uint8_t s_tx_count;
static bool s_tx_inflight;

static navi_gui_frame_cb_t s_gui_frame_cb;
static navi_gui_reset_cb_t s_gui_reset_cb;
static navi_qr_ready_cb_t s_qr_ready_cb;
static bool s_gui_attached;
static bool s_gui_reset_msg_pending;
static bool s_gui_reset_needed;
static bool s_qr_msg_pending;
static bool s_qr_ready_delivered;
static bool s_stream_started;          /* a frame committed this session (latch) */
static bool s_stream_active_pending;   /* stream-active GUI msg is in flight      */
static bool s_stream_active_delivered; /* GUI has hidden the QR page              */
static uint32_t s_gui_reset_msg_epoch;
static navi_gui_stream_active_cb_t s_stream_active_cb;

static uint8_t s_timer_module_id;
static uint8_t s_poll_timer_idx;

static uint32_t navi_time_ms(void)
{
    return sys_timestamp_get();
}

static bool navi_address_is_valid(const uint8_t address[6])
{
    uint8_t combined_or = 0;
    uint8_t combined_and = 0xFFU;

    if (address == NULL)
    {
        return false;
    }
    for (uint8_t i = 0; i < 6U; i++)
    {
        combined_or |= address[i];
        combined_and &= address[i];
    }
    return combined_or != 0U && combined_and != 0xFFU;
}

static bool navi_seq_is_recent(uint16_t frame_seq)
{
    for (uint8_t i = 0; i < s_recent_seq_count; i++)
    {
        if (s_recent_seq[i] == frame_seq)
        {
            return true;
        }
    }
    return false;
}

static void navi_remember_seq(uint16_t frame_seq)
{
    if (navi_seq_is_recent(frame_seq))
    {
        return;
    }

    s_recent_seq[s_recent_seq_next] = frame_seq;
    s_recent_seq_next = (uint8_t)((s_recent_seq_next + 1U) % NAVI_RECENT_SEQ_COUNT);
    if (s_recent_seq_count < NAVI_RECENT_SEQ_COUNT)
    {
        s_recent_seq_count++;
    }
}

static bool navi_seq_older(uint16_t lhs, uint16_t rhs)
{
    return (int16_t)(lhs - rhs) < 0;
}

static uint16_t navi_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;

    while (len--)
    {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
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

static bool navi_crc_self_test(void)
{
    static const uint8_t vector[] = "123456789";
    return navi_crc16(vector, 9) == 0xBB3D && navi_crc32(vector, 9) == 0xCBF43926U;
}

static void navi_slot_clear(navi_frame_slot_t *slot)
{
    uint8_t *jpeg_buffer = slot->jpeg_buffer;
    stp_frame_t buf_desc = slot->buf_desc;
    bool buf_held = slot->buf_held;

    memset(slot, 0, sizeof(*slot));
    slot->jpeg_buffer = jpeg_buffer;
    /* An un-committed transport buffer stays attached to the slot for reuse;
     * only a successful stp_commit hands it off (clearing buf_held there). */
    slot->buf_desc = buf_desc;
    slot->buf_held = buf_held;
    slot->state = NAVI_SLOT_FREE;
}

static void navi_tx_clear(void)
{
    s_tx_head = 0;
    s_tx_tail = 0;
    s_tx_count = 0;
    s_tx_inflight = false;
    memset(s_tx_fifo, 0, sizeof(s_tx_fifo));
}

static void navi_tx_commit_head(void)
{
    navi_tx_item_t *item;

    if (s_tx_count == 0)
    {
        s_tx_inflight = false;
        return;
    }

    item = &s_tx_fifo[s_tx_head];
    if (item->effect == NAVI_TX_EFFECT_CREDIT && s_state == NAVI_STATE_ACTIVE)
    {
        uint16_t returned = item->credit_amount;
        if ((uint32_t)s_credit + returned > s_credit_limit)
        {
            returned = (uint16_t)(s_credit_limit - s_credit);
        }
        s_credit = (uint16_t)(s_credit + returned);
    }

    memset(item, 0, sizeof(*item));
    s_tx_head = (uint8_t)((s_tx_head + 1U) % NAVI_TX_FIFO_DEPTH);
    s_tx_count--;
    s_tx_inflight = false;
}

static bool navi_tx_channel_enabled(uint16_t attrib_idx)
{
    if (attrib_idx == NAVI_ATTR_CTRL_RX_VALUE)
    {
        return s_ctrl_cccd_enabled;
    }
    if (attrib_idx == NAVI_ATTR_DATA_RX_VALUE)
    {
        return s_data_cccd_enabled;
    }
    return false;
}

static void navi_tx_try_send(void)
{
    if (s_tx_inflight || s_tx_count == 0 || !s_link_alive ||
        s_conn_handle == NAVI_INVALID_CONN_HANDLE || s_cid == NAVI_INVALID_CID)
    {
        return;
    }

    navi_tx_item_t *item = &s_tx_fifo[s_tx_head];
    if (!navi_tx_channel_enabled(item->attrib_idx))
    {
        /* Preserve protocol state while unsubscribed. Disconnect/session cleanup
         * owns abandoning notifications that can no longer be delivered. */
        return;
    }

    s_tx_inflight = true;
    if (!gatt_svc_send_data(s_conn_handle, s_cid, s_service_id, item->attrib_idx,
                            item->data, item->len, GATT_PDU_TYPE_NOTIFICATION))
    {
        s_tx_inflight = false;
        APP_PRINT_ERROR1("Navi notify immediate failure: attr=%u", item->attrib_idx);
    }
}

static bool navi_tx_enqueue_effect(uint16_t attrib_idx, const uint8_t *data, uint16_t len,
                                   navi_tx_effect_t effect, uint16_t credit_amount,
                                   bool send_now)
{
    if (!s_link_alive || data == NULL || len == 0 || len > NAVI_TX_MAX_LEN ||
        !navi_tx_channel_enabled(attrib_idx))
    {
        return false;
    }
    if (s_tx_count >= NAVI_TX_FIFO_DEPTH)
    {
        APP_PRINT_ERROR2("Navi notify enqueue rejected: len=%u depth=%u", len, s_tx_count);
        return false;
    }

    navi_tx_item_t *item = &s_tx_fifo[s_tx_tail];
    memset(item, 0, sizeof(*item));
    item->attrib_idx = attrib_idx;
    item->len = len;
    item->effect = effect;
    item->credit_amount = credit_amount;
    memcpy(item->data, data, len);
    s_tx_tail = (uint8_t)((s_tx_tail + 1U) % NAVI_TX_FIFO_DEPTH);
    s_tx_count++;
    if (send_now)
    {
        navi_tx_try_send();
    }
    return true;
}

static bool navi_tx_enqueue_deferred(uint16_t attrib_idx, const uint8_t *data, uint16_t len)
{
    return navi_tx_enqueue_effect(attrib_idx, data, len, NAVI_TX_EFFECT_NONE, 0, false);
}

static bool navi_tx_enqueue(uint16_t attrib_idx, const uint8_t *data, uint16_t len)
{
    return navi_tx_enqueue_effect(attrib_idx, data, len, NAVI_TX_EFFECT_NONE, 0, true);
}

static void navi_send_complete_cb(T_EXT_SEND_DATA_RESULT result)
{
    if (result.service_id != s_service_id || !s_tx_inflight || s_tx_count == 0)
    {
        return;
    }

    navi_tx_item_t *item = &s_tx_fifo[s_tx_head];
    if (result.conn_handle != s_conn_handle || result.cid != s_cid ||
        result.attrib_idx != item->attrib_idx)
    {
        APP_PRINT_WARN0("Navi ignored stale notify completion");
        return;
    }

    if (result.cause != GAP_SUCCESS)
    {
        APP_PRINT_ERROR2("Navi notify asynchronous failure: attr=%u cause=0x%x",
                         result.attrib_idx, result.cause);
        s_tx_inflight = false;
        return;
    }

    navi_tx_commit_head();
    navi_tx_try_send();
}

static bool navi_send_l1_internal(uint8_t control, uint16_t seq, const uint8_t *payload,
                                  uint16_t len, bool send_now)
{
    uint8_t packet[NAVI_L1_HEADER_LEN + NAVI_L1_MAX_PAYLOAD];
    uint16_t crc;

    if (len > NAVI_L1_MAX_PAYLOAD || (len > 0 && payload == NULL))
    {
        return false;
    }

    crc = navi_crc16(payload, len);
    packet[0] = NAVI_L1_SYNC;
    packet[1] = control;
    packet[2] = HI_16(len);
    packet[3] = LO_16(len);
    packet[4] = HI_16(crc);
    packet[5] = LO_16(crc);
    packet[6] = HI_16(seq);
    packet[7] = LO_16(seq);
    if (len > 0)
    {
        memcpy(packet + NAVI_L1_HEADER_LEN, payload, len);
    }
    if (send_now)
    {
        return navi_tx_enqueue(NAVI_ATTR_CTRL_RX_VALUE, packet, NAVI_L1_HEADER_LEN + len);
    }
    return navi_tx_enqueue_deferred(NAVI_ATTR_CTRL_RX_VALUE, packet, NAVI_L1_HEADER_LEN + len);
}

static bool navi_send_l1(uint8_t control, uint16_t seq, const uint8_t *payload, uint16_t len)
{
    return navi_send_l1_internal(control, seq, payload, len, true);
}

static bool navi_send_l2_internal(uint8_t key, const uint8_t *value, uint16_t len, bool send_now)
{
    uint8_t packet[NAVI_L2_HEADER_LEN + 3U + 6U * NAVI_MAX_GAPS];
    uint16_t total_len = NAVI_L2_HEADER_LEN + len;

    if (total_len > sizeof(packet) || (len > 0 && value == NULL))
    {
        return false;
    }

    packet[0] = NAVI_L2_CMD;
    packet[1] = 0;
    packet[2] = key;
    packet[3] = HI_16(len);
    packet[4] = LO_16(len);
    if (len > 0)
    {
        memcpy(packet + NAVI_L2_HEADER_LEN, value, len);
    }

    if (key <= NAVI_KEY_ERROR)
    {
        return navi_send_l1_internal(NAVI_L1_CTRL_DATA, ++s_l1_tx_seq,
                                     packet, total_len, send_now);
    }
    if (send_now)
    {
        return navi_tx_enqueue(NAVI_ATTR_DATA_RX_VALUE, packet, total_len);
    }
    return navi_tx_enqueue_deferred(NAVI_ATTR_DATA_RX_VALUE, packet, total_len);
}

static bool navi_send_l2(uint8_t key, const uint8_t *value, uint16_t len)
{
    return navi_send_l2_internal(key, value, len, true);
}

static bool navi_return_credit(navi_frame_slot_t *slot)
{
    uint8_t packet[NAVI_L2_HEADER_LEN + 2U];
    uint16_t returned = slot->credits_consumed;

    if (returned == 0)
    {
        return true;
    }
    /* Credits already represented by queued notifications are not reflected in
     * s_credit until completion, so never clamp a newly-queued return here. */

    packet[0] = NAVI_L2_CMD;
    packet[1] = 0;
    packet[2] = NAVI_KEY_CREDIT;
    packet[3] = 0;
    packet[4] = 2;
    packet[5] = HI_16(returned);
    packet[6] = LO_16(returned);
    if (!navi_tx_enqueue_effect(NAVI_ATTR_DATA_RX_VALUE, packet, sizeof(packet),
                                NAVI_TX_EFFECT_CREDIT, returned, false))
    {
        return false;
    }
    /* The queue owns this credit return now. Local available credit is changed
     * only by successful asynchronous completion in navi_tx_commit_head(). */
    slot->credits_consumed = 0;
    return true;
}

bool navi_projection_validate_jpeg(const uint8_t *jpeg, uint32_t len,
                                   uint16_t expected_w, uint16_t expected_h)
{
    uint32_t pos = 2;
    bool found_sof = false;
    bool found_sos = false;

    if (len < 8 || jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
        jpeg[len - 2] != 0xFF || jpeg[len - 1] != 0xD9)
    {
        return false;
    }

    while (pos + 1U < len - 2U)
    {
        if (jpeg[pos++] != 0xFF)
        {
            return false;
        }
        while (pos < len && jpeg[pos] == 0xFF)
        {
            pos++;
        }
        if (pos >= len)
        {
            return false;
        }

        uint8_t marker = jpeg[pos++];
        if (marker == 0xDA)
        {
            if (pos + 2U > len)
            {
                return false;
            }
            uint16_t seg_len = BIG16(jpeg[pos], jpeg[pos + 1U]);
            if (seg_len < 6U || pos + seg_len > len - 2U)
            {
                return false;
            }
            found_sos = true;
            break;
        }

        if (marker == 0xD8 || marker == 0xD9 || marker == 0x00 ||
            (marker >= 0xD0 && marker <= 0xD7))
        {
            return false;
        }
        if (pos + 2U > len)
        {
            return false;
        }

        uint16_t seg_len = BIG16(jpeg[pos], jpeg[pos + 1U]);
        if (seg_len < 2U || pos + seg_len > len)
        {
            return false;
        }

        if (marker == 0xC0)
        {
            if (found_sof || seg_len < 8U || jpeg[pos + 2U] != 8U)
            {
                return false;
            }
            uint16_t height = BIG16(jpeg[pos + 3U], jpeg[pos + 4U]);
            uint16_t width = BIG16(jpeg[pos + 5U], jpeg[pos + 6U]);
            uint8_t components = jpeg[pos + 7U];
            if (width != expected_w || height != expected_h ||
                (components != 1U && components != 3U) ||
                seg_len != (uint16_t)(8U + 3U * components))
            {
                return false;
            }
            found_sof = true;
        }
        else if ((marker >= 0xC1 && marker <= 0xCF) &&
                 marker != 0xC4 && marker != 0xC8)
        {
            /* Progressive, arithmetic and non-8-bit SOF modes are not accepted. */
            return false;
        }

        pos += seg_len;
    }

    return found_sof && found_sos;
}

static bool navi_projection_jpeg_fits_heap(const uint8_t *jpeg, uint32_t len)
{
    uint32_t pos = 2;
    uint32_t plane_bytes = 0;

    while (pos + 1U < len - 2U)
    {
        if (jpeg[pos++] != 0xFF)
        {
            return false;
        }
        while (pos < len && jpeg[pos] == 0xFF)
        {
            pos++;
        }
        if (pos >= len)
        {
            return false;
        }

        uint8_t marker = jpeg[pos++];
        if (marker == 0xDA)
        {
            return false;
        }
        if (pos + 2U > len)
        {
            return false;
        }

        uint16_t seg_len = BIG16(jpeg[pos], jpeg[pos + 1U]);
        if (seg_len < 2U || pos + seg_len > len)
        {
            return false;
        }
        if (marker == 0xC0)
        {
            uint16_t height = BIG16(jpeg[pos + 3U], jpeg[pos + 4U]);
            uint16_t width = BIG16(jpeg[pos + 5U], jpeg[pos + 6U]);
            uint8_t components = jpeg[pos + 7U];
            uint8_t h_max = 1U;
            uint8_t v_max = 1U;

            for (uint8_t i = 0; i < components; i++)
            {
                uint8_t sampling = jpeg[pos + 9U + 3U * i];
                uint8_t h = sampling >> 4;
                uint8_t v = sampling & 0x0FU;
                if (h == 0U || h > 4U || v == 0U || v > 4U)
                {
                    return false;
                }
                if (h > h_max)
                {
                    h_max = h;
                }
                if (v > v_max)
                {
                    v_max = v;
                }
            }

            uint32_t mcu_x = (width + h_max * 8U - 1U) / (h_max * 8U);
            uint32_t mcu_y = (height + v_max * 8U - 1U) / (v_max * 8U);
            for (uint8_t i = 0; i < components; i++)
            {
                uint8_t sampling = jpeg[pos + 9U + 3U * i];
                uint8_t h = sampling >> 4;
                uint8_t v = sampling & 0x0FU;
                if ((h_max % h) != 0U || (v_max % v) != 0U)
                {
                    return false;
                }
                plane_bytes += mcu_x * h * 8U * mcu_y * v * 8U + 15U;
            }

            /* stb keeps all component planes while allocating an RGB888 output.
             * Keep a conservative 64 KiB margin for line buffers and live UI data. */
            uint32_t peak_bytes = plane_bytes + (uint32_t)width * height * 3U + 65536U;
            return peak_bytes <= PSRAM_GUI_HEAP_SIZE;
        }
        pos += seg_len;
    }
    return false;
}

static void navi_gui_reset_event(void *message);
static void navi_qr_ready_event(void *message);
static bool navi_fail_frame(navi_frame_slot_t *slot, uint8_t error);
static void navi_finish_frame(navi_frame_slot_t *slot);

/* Acquire a transport buffer for a slot about to receive a new frame. A buffer
 * already attached to the slot (kept from a prior aborted frame) is reused
 * as-is; only a successful stp_commit ever detaches one. All transport buffers
 * share a single size class, so a held buffer fits any admissible frame.
 * Returns false when no free buffer is available (the caller reports
 * BUFFER_OVERFLOW) or the frame is larger than the transport buffer size. */
static bool navi_slot_acquire_buffer(navi_frame_slot_t *slot, uint32_t total_len)
{
    if (slot->buf_held)
    {
        return total_len <= slot->buf_desc.capacity;
    }
    if (s_transport == NULL ||
        !stp_acquire_free(s_transport, total_len, &slot->buf_desc))
    {
        return false;
    }
    slot->buf_held = true;
    slot->jpeg_buffer = (uint8_t *)slot->buf_desc.addr;
    return true;
}

static void navi_queue_gui_reset(void)
{
    s_gui_reset_needed = true;
    if (!s_gui_attached)
    {
        s_gui_reset_needed = false;
        return;
    }
    if (s_gui_reset_msg_pending)
    {
        return;
    }
    s_gui_reset_msg_pending = true;
    s_gui_reset_msg_epoch = s_session_epoch;

    gui_msg_t msg = { .event = GUI_EVENT_USER_DEFINE, .cb = navi_gui_reset_event };
    if (!gui_send_msg_to_server(&msg))
    {
        s_gui_reset_msg_pending = false;
    }
}

static void navi_gui_reset_event(void *message)
{
    (void)message;
    if (s_gui_reset_msg_epoch == s_session_epoch &&
        s_gui_attached && s_gui_reset_cb != NULL)
    {
        s_gui_reset_cb();
    }

    if (s_gui_reset_msg_epoch == s_session_epoch)
    {
        s_gui_reset_needed = false;
    }
    s_gui_reset_msg_pending = false;
}

static void navi_gui_active_event(void *message)
{
    (void)message;
    if (s_gui_attached && s_stream_active_cb != NULL)
    {
        s_stream_active_cb();
        s_stream_active_delivered = true;
    }
    s_stream_active_pending = false;
}

/* Notify the GUI (once per session) that decoded frames are now flowing so it
 * can hide the QR page and reveal the stream widget. Runs from the BT task on
 * the first committed frame; the actual UI mutation happens on the GUI task. */
static void navi_queue_stream_active(void)
{
    if (!s_stream_started || !s_gui_attached || s_stream_active_cb == NULL ||
        s_stream_active_pending || s_stream_active_delivered)
    {
        return;
    }
    s_stream_active_pending = true;
    gui_msg_t msg = { .event = GUI_EVENT_USER_DEFINE, .cb = navi_gui_active_event };
    if (!gui_send_msg_to_server(&msg))
    {
        s_stream_active_pending = false;
    }
}

static void navi_qr_ready_event(void *message)
{
    (void)message;
    if (s_gui_attached && s_qr_ready_cb != NULL)
    {
        s_qr_ready_cb();
        s_qr_ready_delivered = true;
    }
    s_qr_msg_pending = false;
}

static void navi_queue_qr_ready(void)
{
    if (!s_address_ready || !s_gui_attached || s_qr_ready_cb == NULL ||
        s_qr_msg_pending || s_qr_ready_delivered)
    {
        return;
    }
    s_qr_msg_pending = true;
    gui_msg_t msg = { .event = GUI_EVENT_USER_DEFINE, .cb = navi_qr_ready_event };
    if (!gui_send_msg_to_server(&msg))
    {
        s_qr_msg_pending = false;
    }
}

static void navi_reset_session(bool link_lost)
{
    uint32_t lock = os_lock();

    s_state = NAVI_STATE_IDLE;
    memset(&s_open, 0, sizeof(s_open));
    s_credit = 0;
    s_credit_limit = 0;
    s_last_data_ms = 0;
    s_l1_rx_len = 0;
    s_l1_expected = 0;
    s_l1_tx_seq = 0;
    s_recent_seq_count = 0;
    s_recent_seq_next = 0;
    s_session_epoch++;
    s_ready_slot = -1;
    s_stream_started = false;
    s_stream_active_pending = false;
    s_stream_active_delivered = false;

    for (uint8_t i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        if (!s_slots[i].gui_claimed)
        {
            navi_slot_clear(&s_slots[i]);
        }
    }

    if (link_lost)
    {
        s_link_alive = false;
        s_ctrl_cccd_enabled = false;
        s_data_cccd_enabled = false;
        s_conn_handle = NAVI_INVALID_CONN_HANDLE;
        s_cid = NAVI_INVALID_CID;
        s_conn_id = NAVI_INVALID_CONN_ID;
        navi_tx_clear();
    }
    os_unlock(lock);
    navi_queue_gui_reset();
}

static int navi_find_or_reserve_slot(uint16_t frame_seq, uint32_t total_len)
{
    int free_idx = -1;
    int ready_idx = -1;
    int oldest_receiving_idx = -1;

    for (uint8_t i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        if (s_slots[i].state == NAVI_SLOT_RECEIVING)
        {
            if (s_slots[i].frame_seq == frame_seq)
            {
                return i;
            }
            if (oldest_receiving_idx < 0 ||
                navi_seq_older(s_slots[i].frame_seq,
                               s_slots[oldest_receiving_idx].frame_seq))
            {
                oldest_receiving_idx = i;
            }
        }
        if (s_slots[i].state == NAVI_SLOT_FREE && free_idx < 0)
        {
            free_idx = i;
        }
        if (s_slots[i].state == NAVI_SLOT_READY && !s_slots[i].gui_claimed && ready_idx < 0)
        {
            ready_idx = i;
        }
    }

    if (free_idx < 0 && ready_idx >= 0)
    {
        if (s_ready_slot == ready_idx)
        {
            s_ready_slot = -1;
        }
        navi_slot_clear(&s_slots[ready_idx]);
        free_idx = ready_idx;
    }
    if (free_idx < 0 && oldest_receiving_idx >= 0)
    {
        navi_frame_slot_t *oldest = &s_slots[oldest_receiving_idx];
        if (navi_seq_older(oldest->frame_seq, frame_seq))
        {
            /* HoneyBox retains only its latest three frames. Admit the newer
             * sequence and retire the oldest incomplete one. Reserve queue
             * capacity before queuing its ERROR and CREDIT notifications. */
            uint8_t needed_entries = oldest->credits_consumed != 0 ? 2U : 1U;
            uint8_t error_value[2] =
            {
                NAVI_ERR_BUFFER_OVERFLOW,
                (uint8_t)(oldest->frame_seq & 0xFFU)
            };
            if (s_tx_count > NAVI_TX_FIFO_DEPTH - needed_entries ||
                !navi_send_l2_internal(NAVI_KEY_ERROR, error_value,
                                       sizeof(error_value), false) ||
                (oldest->credits_consumed != 0 && !navi_return_credit(oldest)))
            {
                return -1;
            }
            navi_tx_try_send();
            navi_remember_seq(oldest->frame_seq);
            navi_slot_clear(oldest);
            free_idx = oldest_receiving_idx;
        }
    }

    if (free_idx >= 0)
    {
        navi_frame_slot_t *slot = &s_slots[free_idx];
        /* Reassemble straight into a transport buffer (zero-copy). If none is
         * free, decline the slot so the caller reports BUFFER_OVERFLOW rather
         * than admitting a frame with nowhere to land. */
        if (!navi_slot_acquire_buffer(slot, total_len))
        {
            return -1;
        }
        slot->frame_seq = frame_seq;
        slot->total_len = total_len;
        slot->last_rx_ms = navi_time_ms();
        slot->state = NAVI_SLOT_RECEIVING;
    }
    return free_idx;
}

static bool navi_fail_frame(navi_frame_slot_t *slot, uint8_t error)
{
    uint8_t value[2] = { error, (uint8_t)(slot->frame_seq & 0xFFU) };
    uint8_t needed_entries = slot->credits_consumed != 0 ? 2U : 1U;

    if (s_tx_count > NAVI_TX_FIFO_DEPTH - needed_entries ||
        !navi_send_l2_internal(NAVI_KEY_ERROR, value, sizeof(value), false))
    {
        return false;
    }
    if (slot->credits_consumed != 0 && !navi_return_credit(slot))
    {
        return false;
    }
    navi_tx_try_send();
    navi_remember_seq(slot->frame_seq);
    navi_slot_clear(slot);
    return true;
}

static bool navi_report_complete(navi_frame_slot_t *slot, bool send_now)
{
    uint8_t packet[NAVI_L2_HEADER_LEN + 3U];

    packet[0] = NAVI_L2_CMD;
    packet[1] = 0;
    packet[2] = NAVI_KEY_REPORT;
    packet[3] = 0;
    packet[4] = 3;
    packet[5] = HI_16(slot->frame_seq);
    packet[6] = LO_16(slot->frame_seq);
    packet[7] = 0;
    return navi_tx_enqueue_effect(NAVI_ATTR_DATA_RX_VALUE, packet, sizeof(packet),
                                  NAVI_TX_EFFECT_NONE, 0, send_now);
}

static void navi_finish_frame(navi_frame_slot_t *slot)
{
    /* The JPEG is reassembled at offset 0 of the transport buffer -- the
     * gui_stream JPEG consumer decodes slot->jpeg_buffer directly. */
    uint8_t *jpeg = slot->jpeg_buffer;

    if (!slot->validated)
    {
        if (!slot->crc32_received || navi_crc32(jpeg, slot->total_len) != slot->expected_crc32)
        {
            (void)navi_fail_frame(slot, NAVI_ERR_CRC_MISMATCH);
            return;
        }
        if (!navi_projection_validate_jpeg(jpeg, slot->total_len, s_open.width, s_open.height) ||
            !navi_projection_jpeg_fits_heap(jpeg, slot->total_len))
        {
            (void)navi_fail_frame(slot, NAVI_ERR_DECODE_FAILED);
            return;
        }
        /* Flush the CPU-written frame so a DMA-backed decoder sees it. */
        slot->validated = true;
    }

    uint8_t needed_entries = slot->credits_consumed != 0 ? 2U : 1U;
    if (s_tx_count > NAVI_TX_FIFO_DEPTH - needed_entries)
    {
        slot->last_rx_ms = navi_time_ms();
        return;
    }
    /* Hand the assembled frame to the stream transport. On success the buffer
     * belongs to the transport/consumer until released; the slot then acquires
     * a fresh buffer for its next frame. A full ready queue is transient
     * backpressure -- keep the buffer and retry on the next poll. */
    if (slot->buf_held)
    {
        if (s_transport == NULL ||
            !stp_commit(s_transport, &slot->buf_desc, slot->total_len, true))
        {
            slot->last_rx_ms = navi_time_ms();
            return;
        }
        slot->buf_held = false;
        s_stream_started = true;
        navi_queue_stream_active();
    }
    if (!navi_report_complete(slot, false))
    {
        slot->last_rx_ms = navi_time_ms();
        return;
    }
    if (slot->credits_consumed != 0 && !navi_return_credit(slot))
    {
        slot->last_rx_ms = navi_time_ms();
        return;
    }
    navi_tx_try_send();
    navi_remember_seq(slot->frame_seq);
    navi_slot_clear(slot);
}

static void navi_handle_frame_packet(const uint8_t *data, uint16_t len)
{
    uint16_t frame_seq;
    uint32_t chunk_offset;
    uint32_t total_len;
    uint32_t packet_crc32 = 0;
    uint16_t header_len = NAVI_FRAME_HEADER_LEN;
    uint16_t data_len;
    int slot_idx;
    navi_frame_slot_t *slot;

    if (s_state != NAVI_STATE_ACTIVE || len <= NAVI_FRAME_HEADER_LEN ||
        len > s_open.max_packet)
    {
        return;
    }

    frame_seq = BIG16(data[0], data[1]);
    chunk_offset = BIG24(data[2], data[3], data[4]);
    total_len = BIG24(data[5], data[6], data[7]);
    if (chunk_offset == 0)
    {
        if (len <= NAVI_FRAME_FIRST_HEADER_LEN)
        {
            return;
        }
        packet_crc32 = BIG32(data[8], data[9], data[10], data[11]);
        header_len = NAVI_FRAME_FIRST_HEADER_LEN;
    }
    data_len = (uint16_t)(len - header_len);

    if (total_len == 0 || total_len > NAVI_MAX_JPEG_SIZE ||
        chunk_offset >= total_len || data_len > total_len - chunk_offset)
    {
        uint8_t value[2] = { NAVI_ERR_BUFFER_OVERFLOW, (uint8_t)(frame_seq & 0xFFU) };
        navi_send_l2(NAVI_KEY_ERROR, value, sizeof(value));
        return;
    }
    if (navi_seq_is_recent(frame_seq))
    {
        /* A REPORT may cross retransmissions already queued by the phone. The
         * completed sequence remains terminal and must not consume a new slot. */
        return;
    }

    slot_idx = navi_find_or_reserve_slot(frame_seq, total_len);
    if (slot_idx < 0)
    {
        uint8_t value[2] = { NAVI_ERR_BUFFER_OVERFLOW, (uint8_t)(frame_seq & 0xFFU) };
        navi_send_l2(NAVI_KEY_ERROR, value, sizeof(value));
        return;
    }
    slot = &s_slots[slot_idx];

    if (slot->total_len != total_len)
    {
        return;
    }
    if (chunk_offset == 0)
    {
        if (slot->crc32_received && slot->expected_crc32 != packet_crc32)
        {
            (void)navi_fail_frame(slot, NAVI_ERR_CRC_MISMATCH);
            return;
        }
        slot->expected_crc32 = packet_crc32;
        slot->crc32_received = true;
    }

    /* No coverage bitmap: no per-byte dedup. Every packet is trusted, so the
     * byte tally can double-count a retransmitted chunk (received_bytes may
     * overshoot total_len); the frame is finished on >= and the CRC32 still
     * rejects an actually incomplete frame. */
    if (data_len > 0 && s_credit == 0)
    {
        APP_PRINT_WARN0("Navi FRAME dropped: no credit");
        return;
    }

    memcpy(slot->jpeg_buffer + chunk_offset, data + header_len, data_len);
    slot->received_bytes += data_len;
    slot->last_rx_ms = navi_time_ms();
    s_last_data_ms = slot->last_rx_ms;

    s_credit--;
    slot->credits_consumed++;

    if (slot->received_bytes >= total_len)
    {
        navi_finish_frame(slot);
    }
}

static void navi_send_open_ack(uint8_t result, uint16_t max_packet, uint16_t credit)
{
    uint8_t value[5] =
    {
        result,
        LO_16(max_packet), HI_16(max_packet),
        LO_16(credit), HI_16(credit)
    };
    navi_send_l2(NAVI_KEY_ACK, value, sizeof(value));
}

static void navi_handle_open(const uint8_t *value, uint16_t len)
{
    navi_open_params_t params;
    uint16_t mtu = 23;
    uint16_t live_max_packet;
    uint32_t frame_packets;
    uint32_t total_credit;

    if (len != 11 || !s_link_alive || s_conn_id == NAVI_INVALID_CONN_ID)
    {
        navi_send_open_ack(NAVI_RESULT_UNSUPPORTED, 0, 0);
        return;
    }
    if (s_state == NAVI_STATE_ACTIVE)
    {
        navi_send_open_ack(NAVI_RESULT_BUSY, 0, 0);
        return;
    }

    params.width = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
    params.height = (uint16_t)value[2] | ((uint16_t)value[3] << 8);
    params.fps = value[4];
    params.quality = value[5];
    params.flow_ctrl_enable = value[6] & 1U;
    params.protocol_version = value[7];
    params.features = value[8];
    params.max_packet = (uint16_t)value[9] | ((uint16_t)value[10] << 8);

    if (le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu, s_conn_id) != GAP_CAUSE_SUCCESS || mtu < 23)
    {
        mtu = 23;
    }
    live_max_packet = (uint16_t)(mtu - 3U);
    if (live_max_packet > NAVI_MAX_PACKET)
    {
        live_max_packet = NAVI_MAX_PACKET;
    }

    if (params.protocol_version != NAVI_PROTOCOL_VERSION ||
        (params.features & NAVI_REQUIRED_FEATURES) != NAVI_REQUIRED_FEATURES ||
        !params.flow_ctrl_enable || params.max_packet < NAVI_MIN_PACKET ||
        params.max_packet > live_max_packet || params.width == 0 || params.height == 0 ||
        params.width > 640 || params.height > 640 ||
        (uint32_t)params.width * params.height > NAVI_MAX_DECODED_PIXELS ||
        params.fps == 0 || params.fps > 30 || params.quality < 10 || params.quality > 100)
    {
        APP_PRINT_ERROR4("Navi OPEN rejected: %ux%u mtu=%u packet=%u",
                         params.width, params.height, mtu, params.max_packet);
        navi_send_open_ack(NAVI_RESULT_UNSUPPORTED, 0, 0);
        return;
    }

    memcpy(&s_open, &params, sizeof(params));
    frame_packets = 1U +
                    (NAVI_MAX_JPEG_SIZE - (params.max_packet - NAVI_FRAME_FIRST_HEADER_LEN) +
                     (params.max_packet - NAVI_FRAME_HEADER_LEN) - 1U) /
                    (params.max_packet - NAVI_FRAME_HEADER_LEN);
    total_credit = frame_packets * NAVI_FRAME_SLOTS;
    if (total_credit == 0 || total_credit >= UINT16_MAX)
    {
        navi_send_open_ack(NAVI_RESULT_UNSUPPORTED, 0, 0);
        return;
    }

    uint32_t lock = os_lock();
    for (uint8_t i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        if (!s_slots[i].gui_claimed)
        {
            navi_slot_clear(&s_slots[i]);
        }
    }
    s_ready_slot = -1;
    s_recent_seq_count = 0;
    s_recent_seq_next = 0;
    os_unlock(lock);
    s_credit = (uint16_t)total_credit;
    s_credit_limit = (uint16_t)total_credit;
    s_last_data_ms = navi_time_ms();
    s_state = NAVI_STATE_ACTIVE;
    navi_send_open_ack(NAVI_RESULT_OK, params.max_packet, s_credit);
}

static void navi_handle_l2(const uint8_t *data, uint16_t len)
{
    if (len < NAVI_L2_HEADER_LEN || data[0] != NAVI_L2_CMD)
    {
        return;
    }

    uint16_t value_len = BIG16(data[3], data[4]);
    if (value_len != len - NAVI_L2_HEADER_LEN)
    {
        return;
    }

    switch (data[2])
    {
    case NAVI_KEY_OPEN:
        navi_handle_open(data + NAVI_L2_HEADER_LEN, value_len);
        break;
    case NAVI_KEY_CLOSE:
        navi_reset_session(false);
        break;
    default:
        break;
    }
}

static void navi_l1_consume(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        uint8_t byte = data[i];

        if (s_l1_rx_len == 0 && byte != NAVI_L1_SYNC)
        {
            continue;
        }
        if (s_l1_rx_len >= sizeof(s_l1_rx_buf))
        {
            s_l1_rx_len = 0;
            s_l1_expected = 0;
            continue;
        }
        s_l1_rx_buf[s_l1_rx_len++] = byte;

        if (s_l1_rx_len == NAVI_L1_HEADER_LEN)
        {
            uint16_t payload_len = BIG16(s_l1_rx_buf[2], s_l1_rx_buf[3]);
            if (payload_len > NAVI_L1_MAX_PAYLOAD)
            {
                s_l1_rx_len = 0;
                s_l1_expected = 0;
                continue;
            }
            s_l1_expected = (uint16_t)(NAVI_L1_HEADER_LEN + payload_len);
        }

        if (s_l1_expected != 0 && s_l1_rx_len == s_l1_expected)
        {
            uint8_t control = s_l1_rx_buf[1];
            uint16_t payload_len = (uint16_t)(s_l1_expected - NAVI_L1_HEADER_LEN);
            uint16_t received_crc = BIG16(s_l1_rx_buf[4], s_l1_rx_buf[5]);
            uint16_t seq = BIG16(s_l1_rx_buf[6], s_l1_rx_buf[7]);
            const uint8_t *payload = s_l1_rx_buf + NAVI_L1_HEADER_LEN;

            if (received_crc != navi_crc16(payload, payload_len))
            {
                navi_send_l1(NAVI_L1_CTRL_ERROR, seq, NULL, 0);
            }
            else if (control == NAVI_L1_CTRL_DATA)
            {
                navi_send_l1(NAVI_L1_CTRL_ACK, seq, NULL, 0);
                navi_handle_l2(payload, payload_len);
            }
            s_l1_rx_len = 0;
            s_l1_expected = 0;
        }
    }
}

static bool navi_callback_link_matches(uint16_t conn_handle, uint16_t cid)
{
    uint8_t conn_id;

    if (!s_link_alive || !le_get_conn_id_by_handle(conn_handle, &conn_id) || conn_id != s_conn_id)
    {
        return false;
    }
    if (s_conn_handle != conn_handle)
    {
        return false;
    }
    s_cid = cid;
    return true;
}

static T_APP_RESULT navi_attr_read_cb(uint16_t conn_handle, uint16_t cid,
                                      T_SERVER_ID service_id, uint16_t attrib_index,
                                      uint16_t offset, uint16_t *p_length, uint8_t **pp_value)
{
    (void)conn_handle;
    (void)cid;
    (void)service_id;
    (void)attrib_index;
    (void)offset;
    (void)p_length;
    (void)pp_value;
    return APP_RESULT_ATTR_NOT_FOUND;
}

static T_APP_RESULT navi_attr_write_cb(uint16_t conn_handle, uint16_t cid,
                                       T_SERVER_ID service_id, uint16_t attrib_index,
                                       T_WRITE_TYPE write_type, uint16_t length,
                                       uint8_t *p_value,
                                       P_FUN_EXT_WRITE_IND_POST_PROC *p_write_ind_post_proc)
{
    (void)p_write_ind_post_proc;

    if (service_id != s_service_id || !navi_callback_link_matches(conn_handle, cid) ||
        p_value == NULL || length == 0)
    {
        return APP_RESULT_APP_ERR;
    }

    if (attrib_index == NAVI_ATTR_CTRL_TX_VALUE)
    {
        if (write_type != WRITE_REQUEST)
        {
            return APP_RESULT_APP_ERR;
        }
        navi_l1_consume(p_value, length);
        return APP_RESULT_SUCCESS;
    }
    if (attrib_index == NAVI_ATTR_DATA_TX_VALUE)
    {
        if (write_type != WRITE_WITHOUT_RESPONSE)
        {
            return APP_RESULT_APP_ERR;
        }
        /* One callback is exactly one independent protocol-v2 ATT packet. */
        navi_handle_frame_packet(p_value, length);
        return APP_RESULT_SUCCESS;
    }
    return APP_RESULT_ATTR_NOT_FOUND;
}

static void navi_cccd_update_cb(uint16_t conn_handle, uint16_t cid,
                                T_SERVER_ID service_id, uint16_t index, uint16_t cccbits)
{
    if (service_id != s_service_id || !navi_callback_link_matches(conn_handle, cid))
    {
        return;
    }

    if (index == NAVI_ATTR_CTRL_RX_CCCD)
    {
        s_ctrl_cccd_enabled = (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) != 0;
    }
    else if (index == NAVI_ATTR_DATA_RX_CCCD)
    {
        s_data_cccd_enabled = (cccbits & GATT_CLIENT_CHAR_CONFIG_NOTIFY) != 0;
    }
    navi_tx_try_send();
}

static const T_FUN_GATT_EXT_SERVICE_CBS navi_service_cbs =
{
    navi_attr_read_cb,
    navi_attr_write_cb,
    navi_cccd_update_cb
};

static void navi_poll_timer_cb(uint8_t timer_evt, uint16_t param)
{
    (void)timer_evt;
    (void)param;
    navi_projection_process();
}

T_SERVER_ID navi_projection_add_service(void)
{
    if (!navi_crc_self_test())
    {
        APP_PRINT_ERROR0("Navi CRC self-test failed");
        return NAVI_INVALID_SERVICE_ID;
    }

    if (!gatt_svc_add(&s_service_id, (uint8_t *)navi_projection_tbl,
                      sizeof(navi_projection_tbl), &navi_service_cbs,
                      navi_send_complete_cb))
    {
        s_service_id = NAVI_INVALID_SERVICE_ID;
        return s_service_id;
    }

    hmi_le_msg_cback_register(navi_projection_handle_gap_msg);
    if (app_timer_reg_cb(navi_poll_timer_cb, &s_timer_module_id) == 0)
    {
        app_start_timer(&s_poll_timer_idx, "navi_poll", s_timer_module_id, 0, 0,
                        true, NAVI_POLL_PERIOD_MS);
    }
    return s_service_id;
}

void navi_projection_process(void)
{
    uint32_t now_ms = navi_time_ms();

    navi_tx_try_send();
    if (s_gui_reset_needed && !s_gui_reset_msg_pending)
    {
        navi_queue_gui_reset();
    }
    if (s_address_ready && s_gui_attached && !s_qr_ready_delivered)
    {
        navi_queue_qr_ready();
    }
    if (s_stream_started && s_gui_attached && !s_stream_active_delivered)
    {
        navi_queue_stream_active();
    }
    if (s_state != NAVI_STATE_ACTIVE)
    {
        return;
    }

    for (uint8_t i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        navi_frame_slot_t *slot = &s_slots[i];
        if (slot->state != NAVI_SLOT_RECEIVING || slot->received_bytes == 0)
        {
            continue;
        }
        /* No coverage bitmap: no gap REPORT. Rely on the byte tally reaching
         * total_len (>=, dup packets overshoot) or on the session timeout. */
        if (slot->received_bytes >= slot->total_len)
        {
            navi_finish_frame(slot);
        }
    }

    if (now_ms - s_last_data_ms >= NAVI_SESSION_TIMEOUT_MS)
    {
        uint8_t value[2] = { NAVI_ERR_TIMEOUT, 0 };
        navi_send_l2(NAVI_KEY_ERROR, value, sizeof(value));
        navi_reset_session(false);
    }
}

void navi_projection_handle_gap_msg(T_IO_MSG *p_gap_msg)
{
    T_LE_GAP_MSG gap_msg;

    if (p_gap_msg == NULL || p_gap_msg->type != IO_MSG_TYPE_BT_STATUS ||
        p_gap_msg->subtype != GAP_MSG_LE_CONN_STATE_CHANGE)
    {
        return;
    }

    memcpy(&gap_msg, &p_gap_msg->u.param, sizeof(p_gap_msg->u.param));
    if ((T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state ==
        GAP_CONN_STATE_CONNECTED)
    {
        s_conn_id = gap_msg.msg_data.gap_conn_state_change.conn_id;
        s_conn_handle = le_get_conn_handle(s_conn_id);
        s_cid = L2C_FIXED_CID_ATT;
        s_link_alive = s_conn_handle != NAVI_INVALID_CONN_HANDLE;
        s_ctrl_cccd_enabled = false;
        s_data_cccd_enabled = false;
        navi_tx_clear();
    }
    else if ((T_GAP_CONN_STATE)gap_msg.msg_data.gap_conn_state_change.new_state ==
             GAP_CONN_STATE_DISCONNECTED &&
             gap_msg.msg_data.gap_conn_state_change.conn_id == s_conn_id)
    {
        navi_reset_session(true);
    }
}

void navi_projection_on_address_ready(const uint8_t address[6])
{
    if (!navi_address_is_valid(address))
    {
        APP_PRINT_ERROR0("Navi public address is invalid");
        s_address_ready = false;
        memset(s_public_address, 0, sizeof(s_public_address));
        return;
    }

    memcpy(s_public_address, address, sizeof(s_public_address));
    s_address_ready = true;
    s_qr_ready_delivered = false;
    navi_queue_qr_ready();
}

void navi_projection_set_gui_callbacks(navi_gui_frame_cb_t frame_cb,
                                       navi_gui_reset_cb_t reset_cb,
                                       navi_qr_ready_cb_t qr_ready_cb)
{
    s_gui_frame_cb = frame_cb;
    s_gui_reset_cb = reset_cb;
    s_qr_ready_cb = qr_ready_cb;
}

void navi_projection_set_stream_active_cb(navi_gui_stream_active_cb_t active_cb)
{
    s_stream_active_cb = active_cb;
}

void navi_projection_set_transport(void *transport)
{
    s_transport = (stp_transport_t *)transport;
}

void navi_projection_gui_attached(void)
{
    s_gui_attached = true;
    s_qr_ready_delivered = false;
    if (s_address_ready)
    {
        navi_queue_qr_ready();
    }
    if (s_stream_started && !s_stream_active_delivered)
    {
        navi_queue_stream_active();
    }
}

void navi_projection_gui_detached(void)
{
    uint32_t lock = os_lock();

    s_gui_attached = false;
    s_qr_ready_delivered = false;
    s_session_epoch++;
    s_ready_slot = -1;
    for (uint8_t i = 0; i < NAVI_FRAME_SLOTS; i++)
    {
        if (s_slots[i].state == NAVI_SLOT_READY && !s_slots[i].gui_claimed)
        {
            s_slots[i].state = NAVI_SLOT_RECEIVING;
            s_slots[i].last_rx_ms = navi_time_ms();
        }
    }
    s_gui_reset_needed = false;
    os_unlock(lock);
}

navi_state_t navi_projection_get_state(void)
{
    return s_state;
}

T_SERVER_ID navi_projection_get_service_id(void)
{
    return s_service_id;
}

int navi_projection_build_qr_url(char *buf, size_t len)
{
    if (buf == NULL || len == 0 || !s_address_ready ||
        !navi_address_is_valid(s_public_address))
    {
        return -1;
    }
    return snprintf(buf, len,
                    NAVI_QR_URL_BASE "?modelid=" NAVI_QR_MODEL_ID
                    "&sn=" NAVI_QR_SN
                    "&addr=%02X%%3A%02X%%3A%02X%%3A%02X%%3A%02X%%3A%02X",
                    s_public_address[5], s_public_address[4], s_public_address[3],
                    s_public_address[2], s_public_address[1], s_public_address[0]);
}
