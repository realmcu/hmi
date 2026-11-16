/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <stddef.h>
#include <string.h>

#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"

/*============================================================================*
 *                          L2 router
 *
 *  Payload layout per spec 2.4:
 *      cmd(1) | version/rsv(1) | key(1) | length(2, BE) | value(length)
 *
 *  The application handler gets first refusal: it owns 0x10 (file transfer) and
 *  0x11 (projection session). Everything else lands below, which currently only
 *  logs: this product does not implement the wearable command set (bind, sport,
 *  alarm, ...). Nothing is answered on the phone's behalf, so a missing feature
 *  surfaces as a timeout instead of a fake success.
 *============================================================================*/

static hmi_l2_app_fn_t  s_app;
static hmi_l2_info_fn_t s_info;

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/** Appends one TLV: id(1) + length(2, BE) + value. */
static uint16_t tlv_put(uint8_t *buf, uint16_t pos, uint8_t id,
                        const uint8_t *value, uint16_t len)
{
    buf[pos++] = id;
    put_u16(buf + pos, len);
    pos += 2;
    memcpy(buf + pos, value, len);
    return (uint16_t)(pos + len);
}

/**
 * 0x05 / 0x0B: report the projection limits. Everything the phone needs to
 * pick a resolution, an encoder rate and a chunk size is derived from this
 * answer; the layout is TLV so fields can be appended without a version bump.
 */
static void l2_send_device_info(void)
{
    hmi_l2_device_info_t info;
    uint8_t  value[48];
    uint8_t  tmp[4];
    uint16_t n = 0;

    if (s_info == NULL || !s_info(&info))
    {
        PROTO_LOG("device info requested but no provider");
        return;
    }

    put_u16(tmp, info.stream_width);
    put_u16(tmp + 2, info.stream_height);
    n = tlv_put(value, n, HMI_L2_INFO_ID_AREA, tmp, 4);

    put_u32(tmp, info.jpeg_max_bytes);
    n = tlv_put(value, n, HMI_L2_INFO_ID_JPEG_MAX, tmp, 4);

    /* One quality number, not a range: it is the JPEG quality the encoder
     * should use so a frame of the reported area still fits jpeg_max_bytes. */
    tmp[0] = (uint8_t)(info.quality < 10U ? 10U : (info.quality > 100U ? 100U : info.quality));
    n = tlv_put(value, n, HMI_L2_INFO_ID_QUALITY, tmp, 1);

    put_u32(tmp, info.features);
    n = tlv_put(value, n, HMI_L2_INFO_ID_FEATURES, tmp, 4);

    put_u16(tmp, info.chunk_max);
    n = tlv_put(value, n, HMI_L2_INFO_ID_CHUNK_MAX, tmp, 2);

    tmp[0] = info.requested_fps;
    n = tlv_put(value, n, HMI_L2_INFO_ID_FPS_REQ, tmp, 1);

    /* Spec 3.5 uses L2 version 1 for command 0x05 (header reads 05 10). */
    (void)proto_send_l2(HMI_L2_CMD_DATA, 1, HMI_L2_DATA_DEV_INFO_RSP, value, n);
}

#if PROTO_LOG_ENABLE
static const char *l2_cmd_name(uint8_t cmd)
{
    switch (cmd)
    {
    case HMI_L2_CMD_OTA:        return "OTA";
    case HMI_L2_CMD_SETTINGS:   return "SETTINGS";
    case HMI_L2_CMD_BIND:       return "BIND";
    case HMI_L2_CMD_NOTIFY:     return "NOTIFY";
    case HMI_L2_CMD_SPORT:      return "SPORT";
    case HMI_L2_CMD_FACTORY:    return "FACTORY";
    case HMI_L2_CMD_CONTROL:    return "CONTROL";
    case HMI_L2_CMD_DUMP_STACK: return "DUMP_STACK";
    case HMI_L2_CMD_TEST_FLASH: return "TEST_FLASH";
    case HMI_L2_CMD_LOG:        return "LOG";
    case HMI_L2_CMD_FILE_XFER:  return "FILE_XFER";
    case HMI_L2_CMD_POINT_TEST: return "POINT_TEST";
    default:                    return "UNKNOWN";
    }
}
#endif /* PROTO_LOG_ENABLE */

void hmi_l2_init(hmi_l2_app_fn_t app, hmi_l2_info_fn_t info)
{
    s_app = app;
    s_info = info;
}

void hmi_l2_handle(const uint8_t *l2, uint16_t len)
{
    uint8_t  cmd;
    uint8_t  key;
    uint16_t value_len;

    if (l2 == NULL || len < PROTO_L2_HDR_LEN)
    {
        PROTO_LOG("L2 frame too short (%u)", len);
        return;
    }

    cmd       = l2[0];
    key       = l2[2];
    value_len = (uint16_t)(((uint16_t)l2[3] << 8) | l2[4]);

    if ((uint32_t)value_len + PROTO_L2_HDR_LEN != len)
    {
        PROTO_LOG("L2 %s len mismatch: declared %u, actual %u",
                  l2_cmd_name(cmd), value_len, (unsigned)(len - PROTO_L2_HDR_LEN));
        return;
    }

    if (cmd == HMI_L2_CMD_DATA && key == HMI_L2_DATA_DEV_INFO_REQ)
    {
        l2_send_device_info();
        return;
    }

    if (s_app != NULL && s_app(cmd, key, l2 + PROTO_L2_HDR_LEN, value_len))
    {
        return;
    }

    /* Not owned by the application: no wearable command is implemented here
     * yet. Answer nothing (the phone's request will time out) rather than
     * pretending success. */
    PROTO_LOG("L2 %s(0x%02x) key=0x%02x len=%u: not implemented",
              l2_cmd_name(cmd), cmd, key, value_len);
}
