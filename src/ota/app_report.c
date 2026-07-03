/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * OTA event reporting.
 *
 * BLE OTA responses are sent via ota_service_send_notification() called
 * inside app_ota_ble_handle_cp_req().
 *
 * SPP OTA responses are built here and sent via hmi_bt_spp_send().
 * SPP frame format (Realtek standard, no trailing checksum):
 *   [0xAA][seqn:1][len_lo:1][len_hi:1][event_id_lo:1][event_id_hi:1][data:N]
 *   where len = 2 (event_id) + N (data)
 */

#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "app_report.h"
#include "app_cmd.h"
#include "app_main.h"
#include "app_link_util.h"
#include "hmi_bt_spp.h"

void app_report_event(uint8_t cmd_path, uint16_t event_id, uint8_t app_index,
                      uint8_t *data, uint16_t len)
{
    APP_PRINT_TRACE4("app_report_event: cmd_path %d, event_id 0x%04x, app_index %d, len %d",
                     cmd_path, event_id, app_index, len);

    if (cmd_path != CMD_PATH_SPP)
    {
        return;
    }

    if (app_index >= MAX_BR_LINK_NUM)
    {
        APP_PRINT_WARN1("app_report_event: app_index %d out of range", app_index);
        return;
    }

    T_APP_BR_LINK *p_link = &app_db.br_link[app_index];
    if (!(p_link->connected_profile & SPP_PROFILE_MASK))
    {
        APP_PRINT_WARN1("app_report_event: br_link[%d] SPP not connected", app_index);
        return;
    }

    /* Frame: [0xAA][seqn][len_lo][len_hi][event_id_lo][event_id_hi][data...] */
    uint16_t frame_len = 4 + 2 + len;
    uint8_t *buf = malloc(frame_len);
    if (buf == NULL)
    {
        APP_PRINT_ERROR1("app_report_event: malloc failed, frame_len %d", frame_len);
        return;
    }

    p_link->tx_event_seqn++;
    if (p_link->tx_event_seqn == 0)
    {
        p_link->tx_event_seqn = 1;
    }

    buf[0] = CMD_SYNC_BYTE;
    buf[1] = p_link->tx_event_seqn;
    buf[2] = (uint8_t)(len + 2);
    buf[3] = (uint8_t)((len + 2) >> 8);
    buf[4] = (uint8_t)event_id;
    buf[5] = (uint8_t)(event_id >> 8);
    if (len > 0 && data != NULL)
    {
        memcpy(&buf[6], data, len);
    }

    if (!hmi_bt_spp_send(p_link->bd_addr, buf, frame_len))
    {
        APP_PRINT_WARN1("app_report_event: SPP send failed for event 0x%04x", event_id);
    }

    free(buf);
}
