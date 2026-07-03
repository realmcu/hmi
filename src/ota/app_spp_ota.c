/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * app_spp_ota.c — Bridge between the SPP transport and the OTA command handler.
 *
 * Incoming SPP frame format (Realtek standard, no trailing checksum):
 *   [0xAA][seqn:1][len_lo:1][len_hi:1][cmd_id_lo:1][cmd_id_hi:1][data:N]
 *   where len = sizeof(cmd_id) + sizeof(data) = 2 + N
 *
 * Partial frames across multiple BT_EVENT_SPP_DATA_IND events are reassembled
 * using app_db.br_link[].p_embedded_cmd (same pattern as watch_efl/bredr/app_spp.c).
 */

#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "app_main.h"
#include "app_link_util.h"
#include "app_cmd.h"
#include "app_ota.h"
#include "app_spp_ota.h"
#include "hmi_bt_spp.h"

/* -------------------------------------------------------------------------
 * Connection / disconnection
 * ---------------------------------------------------------------------- */

static void spp_ota_connect(uint8_t *bd_addr)
{
    T_APP_BR_LINK *p_link = app_link_alloc_br_link(bd_addr);
    if (p_link != NULL)
    {
        p_link->connected_profile |= SPP_PROFILE_MASK;
        APP_PRINT_INFO1("spp_ota_connect: bd_addr %s", TRACE_BDADDR(bd_addr));
    }
    else
    {
        APP_PRINT_ERROR0("spp_ota_connect: no free br_link slot");
    }
}

static void spp_ota_disconnect(uint8_t *bd_addr)
{
    T_APP_BR_LINK *p_link = app_link_find_br_link(bd_addr);
    if (p_link != NULL)
    {
        APP_PRINT_INFO1("spp_ota_disconnect: bd_addr %s", TRACE_BDADDR(bd_addr));
        app_link_free_br_link(p_link);
    }
}

/* -------------------------------------------------------------------------
 * RX frame parser
 *
 * Reassembly logic mirrors watch_efl/bredr/app_spp.c:
 *   - Walk raw bytes looking for 0xAA sync.
 *   - cmd_len = payload_len_field + 4 header bytes.
 *   - If a complete frame is available, dispatch to app_ota_cmd_handle.
 *   - If only a partial frame remains, save it in p_embedded_cmd for the
 *     next DATA_IND event.
 * ---------------------------------------------------------------------- */

static void dispatch_frames(uint8_t *p_data, uint16_t data_len, uint8_t app_idx)
{
    while (data_len > 0)
    {
        if (p_data[0] != CMD_SYNC_BYTE)
        {
            p_data++;
            data_len--;
            continue;
        }

        if (data_len < 4)
        {
            break; /* need more bytes to read the length field */
        }

        uint16_t payload_len = (uint16_t)(p_data[2] | (p_data[3] << 8));
        uint16_t cmd_len = payload_len + 4; /* sync + seqn + len_lo + len_hi */

        if (data_len < cmd_len)
        {
            break; /* incomplete frame — caller will buffer the remainder */
        }

        /* payload starts at p_data[4], length = payload_len (includes cmd_id) */
        app_ota_cmd_handle(CMD_PATH_SPP, payload_len, &p_data[4], app_idx);

        p_data   += cmd_len;
        data_len -= cmd_len;
    }

    /* Buffer any remaining partial frame */
    T_APP_BR_LINK *p_link = &app_db.br_link[app_idx];
    if (data_len > 0)
    {
        uint8_t *p_buf = malloc(data_len);
        if (p_buf != NULL)
        {
            memcpy(p_buf, p_data, data_len);
            p_link->p_embedded_cmd    = p_buf;
            p_link->embedded_cmd_len  = data_len;
        }
        else
        {
            APP_PRINT_ERROR1("spp_ota_rx: malloc failed, drop %u leftover bytes", data_len);
        }
    }
    else
    {
        p_link->p_embedded_cmd   = NULL;
        p_link->embedded_cmd_len = 0;
    }
}

static void spp_ota_rx(uint8_t *bd_addr, uint8_t *data, uint16_t len)
{
    T_APP_BR_LINK *p_link = app_link_find_br_link(bd_addr);
    if (p_link == NULL)
    {
        APP_PRINT_WARN1("spp_ota_rx: unknown bd_addr %s, drop", TRACE_BDADDR(bd_addr));
        return;
    }

    uint8_t  app_idx  = p_link->id;
    uint8_t *p_data   = data;
    uint16_t data_len = len;

    if (p_link->p_embedded_cmd != NULL)
    {
        /* Prepend the previously buffered partial frame */
        uint16_t total = p_link->embedded_cmd_len + len;
        uint8_t *p_combined = malloc(total);
        if (p_combined == NULL)
        {
            APP_PRINT_ERROR0("spp_ota_rx: malloc failed for reassembly");
            free(p_link->p_embedded_cmd);
            p_link->p_embedded_cmd   = NULL;
            p_link->embedded_cmd_len = 0;
            return;
        }
        memcpy(p_combined, p_link->p_embedded_cmd, p_link->embedded_cmd_len);
        memcpy(p_combined + p_link->embedded_cmd_len, data, len);
        free(p_link->p_embedded_cmd);
        p_link->p_embedded_cmd   = NULL;
        p_link->embedded_cmd_len = 0;

        dispatch_frames(p_combined, total, app_idx);
        free(p_combined);
    }
    else
    {
        dispatch_frames(p_data, data_len, app_idx);
    }
}

/* -------------------------------------------------------------------------
 * Init
 * ---------------------------------------------------------------------- */

void app_spp_ota_init(void)
{
    hmi_bt_spp_set_connect_cb(spp_ota_connect);
    hmi_bt_spp_set_disconnect_cb(spp_ota_disconnect);
    hmi_bt_spp_set_rx_cb(spp_ota_rx);
    APP_PRINT_INFO0("app_spp_ota_init: SPP OTA callbacks registered");
}
