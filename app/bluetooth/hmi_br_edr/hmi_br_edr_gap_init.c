/*
 * Copyright (c) 2018, Realsil Semiconductor Corporation. All rights reserved.
 */

#include <string.h>
#include "trace.h"
#include "gap.h"
#include "gap_br.h"
#include "hmi_br_edr_gap_cb.h"
#include "hmi_br_edr_gap_init.h"
#include "hmi_bt_task.h"
#include "app_msg.h"
#include "remote.h"
#include "btm.h"

#define DEFAULT_PAGESCAN_WINDOW             0x12
#define DEFAULT_PAGESCAN_INTERVAL           0x800 //0x800
#define DEFAULT_PAGE_TIMEOUT                0x4000
#define DEFAULT_SUPVISIONTIMEOUT            0x1f40 //0x7D00
#define DEFAULT_INQUIRYSCAN_WINDOW          0x12
#define DEFAULT_INQUIRYSCAN_INTERVAL        0x800 //0x1000

void hmi_br_edr_gap_init(void)
{
    uint32_t class_of_device = (uint32_t)(0x18 | (0x04 << 8) | (0x24 << 16));
    uint16_t supervision_timeout = DEFAULT_SUPVISIONTIMEOUT;

    uint16_t link_policy = GAP_LINK_POLICY_ROLE_SWITCH | GAP_LINK_POLICY_SNIFF_MODE;

    uint8_t radio_mode = GAP_RADIO_MODE_VISIBLE_CONNECTABLE;
    bool limited_discoverable = false;
    bool auto_accept_acl = false;

    uint8_t pagescan_type = GAP_RADIO_MODE_VISIBLE_CONNECTABLE;
    uint16_t pagescan_interval = DEFAULT_PAGESCAN_INTERVAL;
    uint16_t pagescan_window = DEFAULT_PAGESCAN_WINDOW;
    uint16_t page_timeout = DEFAULT_PAGE_TIMEOUT;

    uint8_t inquiryscan_type = GAP_INQUIRY_SCAN_TYPE_STANDARD;
    uint16_t inquiryscan_window = DEFAULT_INQUIRYSCAN_WINDOW;
    uint16_t inquiryscan_interval = DEFAULT_INQUIRYSCAN_INTERVAL;
    uint8_t inquiry_mode = GAP_INQUIRY_MODE_EXTENDED_RESULT;

    uint8_t pair_mode = GAP_PAIRING_MODE_PAIRABLE;
    uint16_t auth_flags = GAP_AUTHEN_BIT_GENERAL_BONDING_FLAG | GAP_AUTHEN_BIT_SC_FLAG;
    uint8_t io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint8_t oob_enable = false;
    uint8_t bt_mode = GAP_BT_MODE_21ENABLED;

    gap_lib_init();

    gap_br_set_param(GAP_BR_PARAM_NAME, GAP_DEVICE_NAME_LEN, "eBadge");
    //1: to be slave when accept the acl connect request by default.
    gap_br_cfg_accept_role(0);


    gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(uint8_t), &pair_mode);
    gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(uint16_t), &auth_flags);
    gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(uint8_t), &io_cap);
    gap_set_param(GAP_PARAM_BOND_OOB_ENABLED, sizeof(uint8_t), &oob_enable);

    gap_br_set_param(GAP_BR_PARAM_BT_MODE, sizeof(uint8_t), &bt_mode);
    gap_br_set_param(GAP_BR_PARAM_COD, sizeof(uint32_t), &class_of_device);
    gap_br_set_param(GAP_BR_PARAM_LINK_POLICY, sizeof(uint16_t), &link_policy);
    gap_br_set_param(GAP_BR_PARAM_SUPV_TOUT, sizeof(uint16_t), &supervision_timeout);
    gap_br_set_param(GAP_BR_PARAM_AUTO_ACCEPT_ACL, sizeof(bool), &auto_accept_acl);


    gap_br_set_param(GAP_BR_PARAM_RADIO_MODE, sizeof(uint8_t), &radio_mode);
    gap_br_set_param(GAP_BR_PARAM_LIMIT_DISCOV, sizeof(bool), &limited_discoverable);

    gap_br_set_param(GAP_BR_PARAM_PAGE_SCAN_TYPE, sizeof(uint8_t), &pagescan_type);
    gap_br_set_param(GAP_BR_PARAM_PAGE_SCAN_INTERVAL, sizeof(uint16_t), &pagescan_interval);
    gap_br_set_param(GAP_BR_PARAM_PAGE_SCAN_WINDOW, sizeof(uint16_t), &pagescan_window);
    gap_br_set_param(GAP_BR_PARAM_PAGE_TIMEOUT, sizeof(uint16_t), &page_timeout);

    gap_br_set_param(GAP_BR_PARAM_INQUIRY_SCAN_TYPE, sizeof(uint8_t), &inquiryscan_type);
    gap_br_set_param(GAP_BR_PARAM_INQUIRY_SCAN_INTERVAL, sizeof(uint16_t), &inquiryscan_interval);
    gap_br_set_param(GAP_BR_PARAM_INQUIRY_SCAN_WINDOW, sizeof(uint16_t), &inquiryscan_window);
    gap_br_set_param(GAP_BR_PARAM_INQUIRY_MODE, sizeof(uint8_t), &inquiry_mode);

    gap_register_app_cb(hmi_br_edr_gap_common_cb);

    bt_mgr_cback_register(hmi_br_edr_gap_bt_cback);
}

/*----------------------------------------------------------------------------*
 *  Radio on/off
 *
 *  The badge comes up GAP_RADIO_MODE_VISIBLE_CONNECTABLE, so the controller runs
 *  page scan *and* inquiry scan for as long as it is powered -- both on a 0x800
 *  slot interval, i.e. one scan window every 1.28 s.  Each window is a BT MAC
 *  wakeup, and each one hands its slot timer callback to a task to run.  A
 *  power-off that leaves BR/EDR in this state therefore cannot be quiet, no
 *  matter what else it stops.
 *
 *  GAP_RADIO_MODE_NONE_DISCOVERABLE turns both scans off.  It is confirmed
 *  asynchronously by BT_EVENT_DEVICE_MODE_RSP, hence the latch below and
 *  hmi_br_edr_radio_is_idle() for callers that have to wait for it.
 *----------------------------------------------------------------------------*/
static bool s_radio_gate_closed;
static bool s_radio_idle;

bool hmi_br_edr_radio_gate_is_closed(void)
{
    return s_radio_gate_closed;
}

bool hmi_br_edr_radio_is_idle(void)
{
    return s_radio_idle;
}

void hmi_br_edr_radio_mode_rsp(uint8_t dev_mode, uint16_t cause)
{
    APP_PRINT_INFO2("hmi_br_edr_radio_mode_rsp: dev_mode %d cause 0x%04x", dev_mode, cause);

    if (cause == 0)
    {
        s_radio_idle = (dev_mode == BT_DEVICE_MODE_IDLE);
    }
}

void hmi_br_edr_set_radio_enabled(bool enable)
{
    T_BT_DEVICE_MODE mode = enable ? BT_DEVICE_MODE_DISCOVERABLE_CONNECTABLE
                                   : BT_DEVICE_MODE_IDLE;
    uint8_t radio_mode = enable ? GAP_RADIO_MODE_VISIBLE_CONNECTABLE
                                : GAP_RADIO_MODE_NONE_DISCOVERABLE;

    s_radio_gate_closed = !enable;
    s_radio_idle        = false;

    /* Two layers, deliberately, and no shortcut on the mode BTM reports.
     *
     * BTM keeps its own idea of the device mode and ours is stale: the scan that is
     * actually running was switched on by gap_br_set_param(GAP_BR_PARAM_RADIO_MODE)
     * in hmi_br_edr_gap_init() *before the stack started*, which never went through
     * BTM.  So bt_device_mode_get() answers BT_DEVICE_MODE_IDLE while the controller
     * keeps logging "Scan Info: ... enable 3" every 1.28 s, and an earlier version
     * of this function trusted it, logged "already in mode 0" and stopped nothing.
     *
     * bt_device_mode_set() keeps BTM's bookkeeping in step -- the ACL handlers in
     * hmi_br_edr_gap_cb.c drive the mode through it.  gap_br_set_radio_mode() is the
     * call that actually reaches the controller, being the same layer that turned
     * scan on in the first place. */
    bool ok = bt_device_mode_set(mode);
    T_GAP_CAUSE cause = gap_br_set_radio_mode(radio_mode, false, 0);

    APP_PRINT_INFO4("hmi_br_edr_set_radio_enabled: enable %d, dev_mode %d ret %d, radio_mode %d",
                    enable, mode, ok, radio_mode);
    APP_PRINT_INFO1("hmi_br_edr_set_radio_enabled: gap_br_set_radio_mode cause 0x%02x", cause);

    if (!enable && cause != GAP_CAUSE_SUCCESS)
    {
        /* Nothing more we can do from here; let the caller's wait time out and say
         * so, rather than claim a radio-off that never happened. */
        APP_PRINT_ERROR0("hmi_br_edr_set_radio_enabled: radio stays up");
    }
}

bool hmi_br_edr_request_radio_off(void)
{
    T_IO_MSG msg;

    msg.type    = IO_MSG_TYPE_WRISTBNAD;
    msg.subtype = IO_MSG_BREDR_RADIO_OFF;
    msg.u.param = 0;

    return hmi_send_msg_to_bt_task(&msg);
}

