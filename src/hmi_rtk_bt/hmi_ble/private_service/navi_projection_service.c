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
#include "hmi_bt_log.h"
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
/* byte1 bit fields, HoneyProtocol 2.3.2 */
#define NAVI_L1_ACK_REQ                    0x40  /* request an ACK from the peer */
#define NAVI_L1_BIT_ERR                    0x20
#define NAVI_L1_BIT_ACK                    0x10

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
static bool     s_link_info_logged;
static uint16_t s_navi_file_frame_w;     /* geometry of the accepted stream */
static uint16_t s_navi_file_frame_h;

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

/* L1 carrier: when set, every outbound L1 frame is handed to this function
 * instead of the private FFD characteristic. The NUS transport installs it so
 * the same 0x10 stack runs on the spec-mandated UART profile. */
static navi_carrier_tx_t s_carrier_tx;

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


static bool navi_send_l1_internal(uint8_t control, uint16_t seq, const uint8_t *payload,
                                  uint16_t len, bool send_now)
{
    uint8_t packet[NAVI_L1_HEADER_LEN + NAVI_L1_MAX_PAYLOAD];
    uint16_t crc;

    (void)send_now;

    if (len > NAVI_L1_MAX_PAYLOAD || (len > 0 && payload == NULL))
    {
        return false;
    }
    /* HoneyProtocol L0 is the HMIS notify characteristic; without an installed
     * carrier there is no way to put bytes on the air. */
    if (s_carrier_tx == NULL)
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
    s_carrier_tx(packet, (uint16_t)(NAVI_L1_HEADER_LEN + len));
    return true;
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
static void navi_file_stop(void);
static void navi_file_drop_buffer(void);
static bool navi_file_acquire_buffer(uint32_t total, stp_frame_t *out);

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
        s_link_info_logged = false;
        s_navi_file_frame_w = 0;
        s_navi_file_frame_h = 0;
        navi_file_drop_buffer();
        navi_tx_clear();
    }
    navi_file_stop();
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

/*============================================================================*
 *                     L2 0x10 file transfer (one JPEG per file)
 *
 *  L2 = [0x10][ver|rsv][key][len_hi][len_lo][value ...], carried inside one L1
 *  frame (0xAB header + CRC16 + seq). BEGIN opens the session, DATA carries
 *  "chunk seq + bytes", END carries the whole-file CRC32 and triggers
 *  validation + commit into the stream transport.
 *============================================================================*/

#define NAVI_FILE_CMD                  0x10
#define NAVI_FILE_KEY_BEGIN_REQ        0x01
#define NAVI_FILE_KEY_BEGIN_RSP        0x02
#define NAVI_FILE_KEY_DATA             0x03
#define NAVI_FILE_KEY_END_REQ          0x05
#define NAVI_FILE_KEY_END_RSP          0x06
#define NAVI_FILE_KEY_ABORT            0x07

#define NAVI_FILE_TYPE_IMAGE           0x01
#define NAVI_FILE_TYPE_VIDEO           0x02
#define NAVI_FILE_TYPE_RAW             0x03

/* 248 (L1 max payload) - 5 (L2 header) - 2 (chunk seq) */
#define NAVI_FILE_CHUNK_MAX            241U
#define NAVI_FILE_CHUNK_REQ_MAX        2048U
#define NAVI_FILE_SESSION_TIMEOUT_MS   10000UL
#define NAVI_FILE_RSP_LEN              3U

#define NAVI_FILE_RSP_OK               0x00
#define NAVI_FILE_RSP_BUSY             0x01
#define NAVI_FILE_RSP_NO_SPACE         0x02
#define NAVI_FILE_RSP_BAD_TYPE         0x03

#define NAVI_FILE_END_OK               0x00
#define NAVI_FILE_END_CRC_FAIL         0x01
#define NAVI_FILE_END_INCOMPLETE       0x02

#define NAVI_FILE_ERR_NONE             0x00
#define NAVI_FILE_ERR_WRITE_FAIL       0x01
#define NAVI_FILE_ERR_DECODE_FAIL      0x02

#define NAVI_FILE_ABORT_USER           0x00
#define NAVI_FILE_ABORT_ERROR          0x01
#define NAVI_FILE_ABORT_TIMEOUT        0x02

static bool        s_navi_file_active;
static bool        s_navi_file_held;      /* transport buffer owned by this session */
static stp_frame_t s_navi_file_desc;
static uint32_t    s_navi_file_total;
static uint32_t    s_navi_file_got;
static uint16_t    s_navi_file_chunk;
static uint16_t    s_navi_file_next_seq;
static uint32_t    s_navi_file_last_ms;
static uint32_t    s_navi_file_drop_log_ms;

static void navi_file_send(uint8_t key, const uint8_t *value, uint16_t len)
{
    uint8_t packet[5U + 8U];

    if (len > sizeof(packet) - 5U)
    {
        return;
    }
    packet[0] = NAVI_FILE_CMD;
    packet[1] = 0;
    packet[2] = key;
    packet[3] = HI_16(len);
    packet[4] = LO_16(len);
    if (len > 0 && value != NULL)
    {
        memcpy(packet + 5, value, len);
    }
    (void)navi_send_l1_internal(NAVI_L1_CTRL_DATA, ++s_l1_tx_seq, packet,
                                (uint16_t)(5U + len), true);
}

static void navi_file_stop(void)
{
    s_navi_file_active   = false;
    s_navi_file_total    = 0;
    s_navi_file_got      = 0;
    s_navi_file_chunk    = 0;
    s_navi_file_next_seq = 0;
}

static void navi_file_drop_buffer(void)
{
    if (s_navi_file_held && s_transport != NULL)
    {
        (void)stp_release(s_transport, &s_navi_file_desc);
    }
    s_navi_file_held = false;
    memset(&s_navi_file_desc, 0, sizeof(s_navi_file_desc));
}

/* Take a transit buffer, reclaiming the oldest stale frame if the ring is full
 * (the GUI transport runs STP_DROP_NONE and never discards on its own). */
static bool navi_file_acquire_buffer(uint32_t total, stp_frame_t *out)
{
    stp_frame_t stale;
    uint8_t     reclaimed = 0;
    uint8_t     guard;

    if (s_transport == NULL || out == NULL)
    {
        return false;
    }
    if (stp_acquire_free(s_transport, total, out))
    {
        return true;
    }

    for (guard = 0; guard < (NAVI_FRAME_SLOTS + 4U); guard++)
    {
        if (!stp_acquire_oldest(s_transport, 0, &stale))
        {
            break;
        }
        (void)stp_release(s_transport, &stale);
        reclaimed++;
        if (stp_acquire_free(s_transport, total, out))
        {
            NAVI_FILE_LOG_WARN1("navi_file: transport pool was full, reclaimed %u stale frame(s)",
                                reclaimed);
            return true;
        }
    }
    return false;
}

/* Locate the baseline SOF0 marker and return its dimensions. */
static bool navi_file_jpeg_dims(const uint8_t *jpeg, uint32_t len,
                                uint16_t *w, uint16_t *h)
{
    uint32_t pos = 2;

    if (len < 10U || jpeg[0] != 0xFF || jpeg[1] != 0xD8)
    {
        return false;
    }
    while (pos + 3U < len)
    {
        if (jpeg[pos] != 0xFF)
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
        if (marker == 0xDA || marker == 0xD9)
        {
            return false;
        }
        if (pos + 2U > len)
        {
            return false;
        }
        uint16_t seg = BIG16(jpeg[pos], jpeg[pos + 1U]);
        if (seg < 2U || pos + seg > len)
        {
            return false;
        }
        if (marker == 0xC0)
        {
            if (seg < 8U)
            {
                return false;
            }
            *h = BIG16(jpeg[pos + 3U], jpeg[pos + 4U]);
            *w = BIG16(jpeg[pos + 5U], jpeg[pos + 6U]);
            return (*w != 0U && *h != 0U);
        }
        pos += seg;
    }
    return false;
}

static void navi_file_abort(uint8_t reason)
{
    if (s_navi_file_active)
    {
        uint8_t value = reason;
        navi_file_send(NAVI_FILE_KEY_ABORT, &value, 1);
    }
    navi_file_stop();
    /* A gap/CRC error only kills one frame; keep the session "active" so the GUI
     * stays on the map. Anything else (peer cancel, timeout) ends the stream. */
    if (reason != NAVI_FILE_ABORT_ERROR)
    {
        s_state = NAVI_STATE_IDLE;
        s_navi_file_frame_w = 0;
        s_navi_file_frame_h = 0;
        navi_queue_gui_reset();
    }
}

static void navi_file_handle_begin(const uint8_t *v, uint16_t len)
{
    uint8_t  rsp[NAVI_FILE_RSP_LEN] = { NAVI_FILE_RSP_BAD_TYPE, 0, 0 };
    uint32_t total;
    uint16_t chunk;
    uint8_t  type;
    uint8_t  name_len;

    if (len < 8U)
    {
        navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));
        return;
    }

    type     = v[0];
    total    = BIG32(v[1], v[2], v[3], v[4]);
    chunk    = BIG16(v[5], v[6]);
    name_len = v[7];

    /* The pool, not NAVI_MAX_JPEG_SIZE, is the real ceiling; reject up front
     * instead of failing later as a misleading NO_SPACE. */
    uint32_t frame_cap = (s_transport != NULL) ? stp_max_frame_size(s_transport) : 0U;
    if (frame_cap == 0U || frame_cap > NAVI_MAX_JPEG_SIZE)
    {
        frame_cap = NAVI_MAX_JPEG_SIZE;
    }

    if (len != (uint16_t)(8U + name_len) ||
        type < NAVI_FILE_TYPE_IMAGE || type > NAVI_FILE_TYPE_RAW ||
        total == 0U || total > frame_cap)
    {
        NAVI_FILE_LOG_WARN2("navi_file: BEGIN rejected total=%u cap=%u", total, frame_cap);
        navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));
        return;
    }
    if (s_navi_file_active)
    {
        rsp[0] = NAVI_FILE_RSP_BUSY;
        navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));
        return;
    }

    if (chunk == 0U || chunk > NAVI_FILE_CHUNK_REQ_MAX)
    {
        chunk = NAVI_FILE_CHUNK_REQ_MAX;
    }
    {
        /* Keep one L1 frame inside a single ATT write: the sender must not have
         * to split a frame across writes. Budget = MTU-3 - L1(8) - L2(5) - seq(2). */
        uint16_t mtu = 23;
        uint8_t  conn_id = NAVI_INVALID_CONN_ID;
        uint16_t frame_max = 1;

        if (s_conn_handle != NAVI_INVALID_CONN_HANDLE &&
            le_get_conn_id_by_handle(s_conn_handle, &conn_id))
        {
            le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu, conn_id);
        }
        if (mtu > (3U + NAVI_L1_HEADER_LEN + NAVI_L2_HEADER_LEN + 2U))
        {
            frame_max = (uint16_t)(mtu - 3U - NAVI_L1_HEADER_LEN - NAVI_L2_HEADER_LEN - 2U);
        }
        if (chunk > frame_max)
        {
            chunk = frame_max;
        }
    }
    if (chunk > NAVI_FILE_CHUNK_MAX)
    {
        chunk = NAVI_FILE_CHUNK_MAX;
    }

    if (!s_navi_file_held)
    {
        if (s_transport == NULL)
        {
            /* Transport never bound by the GUI: report BUSY so the phone log can
             * tell this apart from a genuinely exhausted frame pool. */
            APP_PRINT_ERROR0("navi_file: stream transport not bound by the GUI");
            rsp[0] = NAVI_FILE_RSP_BUSY;
            navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));
            return;
        }
        if (!navi_file_acquire_buffer(total, &s_navi_file_desc))
        {
            rsp[0] = NAVI_FILE_RSP_NO_SPACE;
            navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));
            return;
        }
        s_navi_file_held = true;
    }
    else if (total > s_navi_file_desc.capacity)
    {
        rsp[0] = NAVI_FILE_RSP_NO_SPACE;
        navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));
        return;
    }

    s_navi_file_total    = total;
    s_navi_file_got      = 0;
    s_navi_file_chunk    = chunk;
    s_navi_file_next_seq = 0;
    s_navi_file_last_ms  = navi_time_ms();
    s_navi_file_active   = true;
    /* A phone that opened a transfer is streaming: mirror the v2 OPEN semantics
     * so the GUI swaps the QR page for the live map view. */
    s_state = NAVI_STATE_ACTIVE;

    /* One-shot readback of the negotiated link so the serial log always states
     * the truth, even when the stack's own PHY-update callback is compiled out. */
    if (!s_link_info_logged)
    {
        uint8_t link_id = 0;
        if (le_get_conn_id_by_handle(s_conn_handle, &link_id))
        {
            uint16_t iv = 0, lat = 0, mtu = 0;
            uint8_t  txp = 0, rxp = 0;

            le_get_conn_param(GAP_PARAM_CONN_INTERVAL, &iv, link_id);
            le_get_conn_param(GAP_PARAM_CONN_LATENCY, &lat, link_id);
            le_get_conn_param(GAP_PARAM_CONN_MTU_SIZE, &mtu, link_id);
            le_get_conn_param(GAP_PARAM_CONN_TX_PHY_TYPE, &txp, link_id);
            le_get_conn_param(GAP_PARAM_CONN_RX_PHY_TYPE, &rxp, link_id);
            APP_PRINT_INFO5("HMI link: interval=0x%x latency=%u mtu=%u tx_phy=%u rx_phy=%u",
                            iv, lat, mtu, txp, rxp);

            if (txp != GAP_PHYS_2M && rxp != GAP_PHYS_2M)
            {
                if (le_set_phy(link_id, 0, GAP_PHYS_PREFER_2M_BIT, GAP_PHYS_PREFER_2M_BIT,
                               GAP_PHYS_OPTIONS_CODED_PREFER_NO) == GAP_CAUSE_SUCCESS)
                {
                    APP_PRINT_INFO0("HMI link: retried the 2M PHY request");
                }
            }
        }
        s_link_info_logged = true;
    }

    rsp[0] = NAVI_FILE_RSP_OK;
    rsp[1] = HI_16(chunk);
    rsp[2] = LO_16(chunk);
    navi_file_send(NAVI_FILE_KEY_BEGIN_RSP, rsp, sizeof(rsp));

    NAVI_FILE_LOG_DEBUG3("navi_file: BEGIN type=%u total=%u chunk=%u",
                         type, total, chunk);
}

static void navi_file_handle_data(const uint8_t *v, uint16_t len)
{
    uint16_t seq;
    uint16_t data_len;

    if (!s_navi_file_active || len < 3U)
    {
        /* Rate-limited: this fires per packet while a dead session drains. */
        if (s_navi_file_drop_log_ms == 0U || (navi_time_ms() - s_navi_file_drop_log_ms) >= 1000U)
        {
            s_navi_file_drop_log_ms = navi_time_ms();
            NAVI_FILE_LOG_WARN2("navi_file: DATA dropped active=%d len=%u", s_navi_file_active, len);
        }
        return;
    }

    seq      = BIG16(v[0], v[1]);
    data_len = (uint16_t)(len - 2U);

    if (seq != s_navi_file_next_seq || s_navi_file_desc.addr == NULL ||
        s_navi_file_got + data_len > s_navi_file_total)
    {
        NAVI_FILE_LOG_WARN2("navi_file: DATA out of order seq=%u expect=%u",
                            seq, s_navi_file_next_seq);
        navi_file_abort(NAVI_FILE_ABORT_ERROR);
        return;
    }

    memcpy((uint8_t *)s_navi_file_desc.addr + s_navi_file_got, v + 2, data_len);
    s_navi_file_got += data_len;
    s_navi_file_next_seq++;
    s_navi_file_last_ms = navi_time_ms();
}

static void navi_file_handle_end(const uint8_t *v, uint16_t len)
{
    uint8_t  rsp[2] = { NAVI_FILE_END_INCOMPLETE, NAVI_FILE_ERR_NONE };
    uint32_t crc;
    uint16_t w = 0;
    uint16_t h = 0;
    bool     ok;

    if (!s_navi_file_active || len < 4U || s_navi_file_desc.addr == NULL)
    {
        NAVI_FILE_LOG_WARN3("navi_file: END dropped active=%d len=%u addr=%p",
                            s_navi_file_active, len, s_navi_file_desc.addr);
        return;
    }

    crc = BIG32(v[0], v[1], v[2], v[3]);
    ok  = (s_navi_file_got == s_navi_file_total) &&
          (navi_crc32((const uint8_t *)s_navi_file_desc.addr, s_navi_file_total) == crc);

    if (ok)
    {
        const uint8_t *jpeg = (const uint8_t *)s_navi_file_desc.addr;

        /* validate_jpeg() compares the SOF geometry with what it is handed, so
         * the pixel budget has to be stated here explicitly. */
        ok = navi_file_jpeg_dims(jpeg, s_navi_file_total, &w, &h) &&
             ((uint32_t)w * (uint32_t)h <= NAVI_MAX_DECODED_PIXELS) &&
             ((s_navi_file_frame_w == 0U ||
               (w == s_navi_file_frame_w && h == s_navi_file_frame_h))) &&
             navi_projection_validate_jpeg(jpeg, s_navi_file_total, w, h) &&
             navi_projection_jpeg_fits_heap(jpeg, s_navi_file_total);
        if (!ok)
        {
            NAVI_FILE_LOG_WARN5("navi_file: END rejected geometry=%ux%u (stream=%ux%u budget %u px)",
                                w, h, s_navi_file_frame_w, s_navi_file_frame_h,
                                (unsigned)NAVI_MAX_DECODED_PIXELS);
            rsp[1] = NAVI_FILE_ERR_DECODE_FAIL;
        }
    }
    else
    {
        rsp[0] = (s_navi_file_got != s_navi_file_total) ? NAVI_FILE_END_INCOMPLETE
                 : NAVI_FILE_END_CRC_FAIL;
    }

    if (ok)
    {
        if (stp_commit(s_transport, &s_navi_file_desc, s_navi_file_total, true))
        {
            s_navi_file_held = false;
            memset(&s_navi_file_desc, 0, sizeof(s_navi_file_desc));
            s_navi_file_frame_w = w;
            s_navi_file_frame_h = h;
            /* Reveal the map only once a frame is on screen: revealing on the
             * first DATA left a black map when the frame was later rejected. */
            s_stream_started = true;
            navi_queue_stream_active();
            rsp[0] = NAVI_FILE_END_OK;
            rsp[1] = NAVI_FILE_ERR_NONE;
            NAVI_FILE_LOG_DEBUG2("navi_file: frame committed %ux%u", w, h);
        }
        else
        {
            rsp[0] = NAVI_FILE_END_INCOMPLETE;
            rsp[1] = NAVI_FILE_ERR_WRITE_FAIL;
            NAVI_FILE_LOG_WARN0("navi_file: stp_commit failed");
        }
    }
    else
    {
        NAVI_FILE_LOG_WARN2("navi_file: END rejected status=%u err=%u", rsp[0], rsp[1]);
    }

    navi_file_send(NAVI_FILE_KEY_END_RSP, rsp, sizeof(rsp));
    navi_file_stop();
}

static void navi_file_dispatch(uint8_t key, const uint8_t *v, uint16_t len)
{
    switch (key)
    {
    case NAVI_FILE_KEY_BEGIN_REQ:
        navi_file_handle_begin(v, len);
        break;
    case NAVI_FILE_KEY_DATA:
        navi_file_handle_data(v, len);
        break;
    case NAVI_FILE_KEY_END_REQ:
        navi_file_handle_end(v, len);
        break;
    case NAVI_FILE_KEY_ABORT:
        NAVI_FILE_LOG_INFO1("navi_file: ABORT from peer reason=%u", len > 0 ? v[0] : 0);
        navi_file_stop();
        /* The phone cancelled: leave "active" so the GUI brings the QR back. */
        s_state = NAVI_STATE_IDLE;
        s_navi_file_frame_w = 0;
        s_navi_file_frame_h = 0;
        navi_queue_gui_reset();
        break;
    default:
        NAVI_FILE_LOG_WARN1("navi_file: unknown key=0x%02x", key);
        break;
    }
}

static void navi_handle_l2(const uint8_t *data, uint16_t len)
{
    if (len < NAVI_L2_HEADER_LEN)
    {
        return;
    }

    uint16_t value_len = BIG16(data[3], data[4]);
    if (value_len != len - NAVI_L2_HEADER_LEN)
    {
        APP_PRINT_WARN3("navi: L2 length mismatch cmd=0x%02x declared=%u actual=%u",
                        data[0], value_len, (unsigned)(len - NAVI_L2_HEADER_LEN));
        return;
    }

    /* HoneyProtocol 3.12 file transfer (one JPEG frame per file). */
    if (data[0] == NAVI_FILE_CMD)
    {
        navi_file_dispatch(data[2], data + NAVI_L2_HEADER_LEN, value_len);
        return;
    }

    if (data[0] != NAVI_L2_CMD)
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
            uint8_t  control = s_l1_rx_buf[1];
            uint16_t payload_len = (uint16_t)(s_l1_expected - NAVI_L1_HEADER_LEN);
            uint16_t received_crc = BIG16(s_l1_rx_buf[4], s_l1_rx_buf[5]);
            uint16_t seq = BIG16(s_l1_rx_buf[6], s_l1_rx_buf[7]);
            const uint8_t *payload = s_l1_rx_buf + NAVI_L1_HEADER_LEN;
            bool ack_requested = (control & NAVI_L1_ACK_REQ) != 0;

            if (control & NAVI_L1_BIT_ACK)
            {
                /* Streaming mode: this endpoint never waits for ACKs, so an
                 * inbound ACK carries no state we need. */
            }
            else if (received_crc != navi_crc16(payload, payload_len))
            {
                /* HoneyProtocol 2.3.1: the receiver answers per request. A
                 * streaming sender that did not ask for an ACK learns about the
                 * gap from the L2 sequence check instead. */
                if (ack_requested)
                {
                    navi_send_l1(NAVI_L1_CTRL_ERROR, seq, NULL, 0);
                }
                else
                {
                    APP_PRINT_WARN1("navi: L1 CRC error seq=%u (streaming, no ACK)", seq);
                }
            }
            else
            {
                if (ack_requested)
                {
                    navi_send_l1(NAVI_L1_CTRL_ACK, seq, NULL, 0);
                }
                if (!(control & NAVI_L1_BIT_ERR))
                {
                    navi_handle_l2(payload, payload_len);
                }
            }
            s_l1_rx_len = 0;
            s_l1_expected = 0;
        }
    }
}


static void navi_poll_timer_cb(uint8_t timer_evt, uint16_t param)
{
    (void)timer_evt;
    (void)param;
    navi_projection_process();
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
    if (s_navi_file_active && (now_ms - s_navi_file_last_ms >= NAVI_FILE_SESSION_TIMEOUT_MS))
    {
        NAVI_FILE_LOG_WARN0("navi_file: session timeout, aborting");
        navi_file_abort(NAVI_FILE_ABORT_TIMEOUT);
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

    /* The protocol-v2 idle watchdog lived here. It keyed off s_last_data_ms,
     * which only the v2 packet path ever refreshed, so once 0x10 took over it
     * fired on every tick and tore the session down between BEGIN and DATA.
     * Idle handling for 0x10 lives above (NAVI_FILE_SESSION_TIMEOUT_MS). */
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

/*============================================================================*
 *                  Transport-agnostic entry points (NUS carrier)
 *============================================================================*/

void navi_projection_set_carrier(navi_carrier_tx_t tx)
{
    s_carrier_tx = tx;
}

void navi_projection_feed_l1(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0)
    {
        return;
    }
    navi_l1_consume(data, len);
}

void navi_projection_core_init(void)
{
    if (!navi_crc_self_test())
    {
        APP_PRINT_ERROR0("Navi CRC self-test failed");
        return;
    }

    /* No GATT table of our own: HoneyProtocol L1/L2 ride on the HMIS private
     * service (0xFFC1/0xFFC2) that hmi_ctrl_service owns. Only the session
     * engine, the stream transport hook-up and the QR helpers live here. */
    hmi_le_msg_cback_register(navi_projection_handle_gap_msg);
    if (app_timer_reg_cb(navi_poll_timer_cb, &s_timer_module_id) == 0)
    {
        app_start_timer(&s_poll_timer_idx, "navi_poll", s_timer_module_id, 0, 0,
                        true, NAVI_POLL_PERIOD_MS);
    }
}

