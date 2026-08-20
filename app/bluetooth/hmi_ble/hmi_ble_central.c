/**
 * @file    hmi_ble_central.c
 * @brief   BLE central (GATT client) role -- milestone A: scan / filter / connect / discover.
 *
 * See hmi_ble_central.h for the overall design.  Flow:
 *   start_scan -> le_adv_stop + le_scan_start
 *     GAP_MSG_LE_SCAN_INFO -> filter name contains "eBadge" -> cache + print MAC
 *   connect(idx) -> le_scan_stop + le_connect(candidate)
 *     CONNECTED(master) -> client_by_uuid128_srv_discovery(HMI 128-bit service)
 *       SRV_DONE  -> client_all_char_discovery
 *       CHAR_DONE -> client_all_char_descriptor_discovery (find EVENT CCCD)
 *       DESC_DONE -> write CCCD=notify -> state READY (ready for xfer, milestone B)
 *   disconnect -> le_disconnect ; on DISCONNECTED the GAP layer restarts advertising.
 */
#include "hmi_ble_central.h"

#include <string.h>
#include <trace.h>
#include <gap.h>
#include <gap_le.h>
#include <gap_scan.h>
#include <gap_conn_le.h>
#include <gap_adv.h>
#include <gap_le_types.h>
#include <gap_msg.h>            /* GAP_ADV_STATE_IDLE */
#include <profile_client.h>

#include "hmi_ctrl_service.h"   /* GATT_UUID128_HMI_SERVICE, BLE_UUID_HMI_CMD/EVENT */
#include "hmi_ble_gap_msg.h"    /* hmi_ble_gap_start_adv() -- deferral-safe adv restart */
#include "hmi_l2_xfer_client.h" /* sender-side xfer state machine */

/*============================================================================*
 *                              Types & State
 *============================================================================*/
typedef enum
{
    CEN_IDLE = 0,     /* not in send mode (peripheral/advertising default) */
    CEN_ADV_STOPPING, /* (unused) stopping advertising before scan          */
    CEN_SCAN_STOPPING,/* stopping the previous scan before a fresh re-scan  */
    CEN_SCANNING,
    CEN_CONNECTING,
    CEN_DISCOVERING,
    CEN_READY,        /* CMD/EVENT handles found, notify enabled            */
} T_CEN_STATE;

typedef struct
{
    uint8_t bd_addr[6];
    uint8_t addr_type;
    int8_t  rssi;
    char    name[HMI_CENTRAL_NAME_LEN];
} T_CEN_DEV;

static T_CEN_STATE s_state   = CEN_IDLE;
static uint8_t     s_conn_id = 0xFF;
/* le_scan_start() has physically started the scan.  On this single-link stack
 * the scan then wedges at GAP_SCAN_STATE_START(1) and keeps running forever --
 * connect and disconnect never reset it -- so once this is set, every later
 * start_scan reuses the running scan instead of calling le_scan_start() again
 * (which would return "invalid state 1").  Kept independent of s_state, which
 * handle_disconnected resets to CEN_IDLE even though the scan is still running. */
static bool        s_scan_started = false;

static T_CEN_DEV   s_devs[HMI_CENTRAL_MAX_DEVS];
static uint8_t     s_dev_cnt = 0;

static T_CLIENT_ID s_hmi_client = CLIENT_PROFILE_GENERAL_ID;

/* Discovered handles of the connected peer's HMI ctrl service. */
static uint16_t s_srv_start;
static uint16_t s_srv_end;
static uint16_t s_cmd_handle;    /* 0xFFC1 value handle (write)   */
static uint16_t s_event_handle;  /* 0xFFC2 value handle (notify)  */
static uint16_t s_event_cccd;    /* 0xFFC2 CCCD                   */

/* Standard CCCD (Client Characteristic Configuration) 16-bit UUID. */
#ifndef GATT_UUID_CHAR_CLIENT_CONFIG
#define GATT_UUID_CHAR_CLIENT_CONFIG   0x2902
#endif

/*============================================================================*
 *                              Helpers
 *============================================================================*/
/** Parse the Local Name (AD type 0x08/0x09) out of raw adv/scan-rsp data. */
static bool cen_parse_name(const uint8_t *data, uint8_t len, char *out, uint8_t out_sz)
{
    uint8_t i = 0;
    while (i + 1 < len)
    {
        uint8_t field_len = data[i];
        if (field_len == 0)
        {
            break;
        }
        if ((uint16_t)(i + 1 + field_len) > len)
        {
            break;      /* malformed */
        }
        uint8_t type = data[i + 1];
        if (type == GAP_ADTYPE_LOCAL_NAME_COMPLETE || type == GAP_ADTYPE_LOCAL_NAME_SHORT)
        {
            uint8_t nlen = field_len - 1;
            if (nlen >= out_sz)
            {
                nlen = out_sz - 1;
            }
            memcpy(out, &data[i + 2], nlen);
            out[nlen] = '\0';
            return true;
        }
        i += field_len + 1;
    }
    return false;
}

static void cen_reset_handles(void)
{
    s_srv_start = 0;
    s_srv_end = 0;
    s_cmd_handle = 0;
    s_event_handle = 0;
    s_event_cccd = 0;
}

/*============================================================================*
 *                       GATT client discovery callbacks
 *============================================================================*/
static void cen_on_discovery_done(uint8_t conn_id)
{
    APP_PRINT_INFO3("[central] discovery done: cmd(FFC1)=0x%x event(FFC2)=0x%x cccd=0x%x",
                    s_cmd_handle, s_event_handle, s_event_cccd);

    if (s_cmd_handle == 0 || s_event_handle == 0)
    {
        APP_PRINT_ERROR0("[central] required HMI chars missing, disconnecting");
        le_disconnect(conn_id);
        return;
    }

    /* Subscribe to EVENT (0xFFC2) notifications so xfer responses arrive. */
    if (s_event_cccd != 0)
    {
        uint16_t ccc = 0x0001;  /* enable notification */
        client_attr_write(conn_id, s_hmi_client, GATT_WRITE_TYPE_REQ,
                          s_event_cccd, sizeof(ccc), (uint8_t *)&ccc);
    }

    s_state = CEN_READY;
    APP_PRINT_INFO0("[central] READY -- link up, HMI ctrl service resolved");
}

static void cen_discover_state_cb(uint8_t conn_id, T_DISCOVERY_STATE state)
{
    APP_PRINT_INFO1("[central] discover_state %d", state);

    switch (state)
    {
    case DISC_STATE_SRV_DONE:
        if (s_srv_start != 0 && s_srv_end != 0)
        {
            if (client_all_char_discovery(conn_id, s_hmi_client,
                                          s_srv_start, s_srv_end) != GAP_CAUSE_SUCCESS)
            {
                APP_PRINT_ERROR0("[central] char discovery request failed");
                le_disconnect(conn_id);
            }
        }
        else
        {
            APP_PRINT_ERROR0("[central] HMI 128-bit service not found on peer");
            le_disconnect(conn_id);
        }
        break;

    case DISC_STATE_CHAR_DONE:
        /* Look for the EVENT (0xFFC2) CCCD in the descriptors that follow it. */
        if (s_event_handle != 0)
        {
            if (client_all_char_descriptor_discovery(conn_id, s_hmi_client,
                                                     s_event_handle, s_srv_end) != GAP_CAUSE_SUCCESS)
            {
                APP_PRINT_ERROR0("[central] descriptor discovery request failed");
                cen_on_discovery_done(conn_id);  /* proceed without CCCD */
            }
        }
        else
        {
            APP_PRINT_ERROR0("[central] EVENT char (0xFFC2) not found");
            le_disconnect(conn_id);
        }
        break;

    case DISC_STATE_CHAR_DESCRIPTOR_DONE:
        cen_on_discovery_done(conn_id);
        break;

    case DISC_STATE_FAILED:
        APP_PRINT_ERROR0("[central] discovery failed");
        le_disconnect(conn_id);
        break;

    default:
        break;
    }
}

static void cen_discover_result_cb(uint8_t conn_id, T_DISCOVERY_RESULT_TYPE type,
                                   T_DISCOVERY_RESULT_DATA data)
{
    switch (type)
    {
    case DISC_RESULT_SRV_DATA:
        s_srv_start = data.p_srv_disc_data->att_handle;
        s_srv_end   = data.p_srv_disc_data->end_group_handle;
        APP_PRINT_INFO2("[central] HMI srv handle range 0x%x..0x%x", s_srv_start, s_srv_end);
        break;

    case DISC_RESULT_CHAR_UUID16:
        {
            uint16_t vh   = data.p_char_uuid16_disc_data->value_handle;
            uint16_t uuid = data.p_char_uuid16_disc_data->uuid16;
            APP_PRINT_INFO2("[central]   char uuid 0x%04x value_handle 0x%x", uuid, vh);
            if (uuid == BLE_UUID_HMI_CMD)
            {
                s_cmd_handle = vh;
            }
            else if (uuid == BLE_UUID_HMI_EVENT)
            {
                s_event_handle = vh;
            }
        }
        break;

    case DISC_RESULT_CHAR_DESC_UUID16:
        if (data.p_char_desc_uuid16_disc_data->uuid16 == GATT_UUID_CHAR_CLIENT_CONFIG)
        {
            uint16_t h = data.p_char_desc_uuid16_disc_data->handle;
            if (h > s_event_handle && s_event_cccd == 0)
            {
                s_event_cccd = h;
            }
        }
        break;

    default:
        break;
    }
}

static void cen_read_result_cb(uint8_t conn_id, uint16_t cause, uint16_t handle,
                               uint16_t value_size, uint8_t *p_value)
{
    (void)conn_id; (void)value_size; (void)p_value;
    APP_PRINT_INFO2("[central] read result handle 0x%x cause 0x%x", handle, cause);
}

static void cen_write_result_cb(uint8_t conn_id, T_GATT_WRITE_TYPE type, uint16_t handle,
                                uint16_t cause, uint8_t credits)
{
    (void)conn_id; (void)credits;
    APP_PRINT_INFO2("[central] write result handle 0x%x cause 0x%x", handle, cause);
    /* Pace the xfer DATA pump on the CMD-char write-response. */
    hmi_l2_xfer_client_on_write_done((uint8_t)type, handle, cause);
}

static T_APP_RESULT cen_notif_ind_result_cb(uint8_t conn_id, bool notify, uint16_t handle,
                                            uint16_t value_size, uint8_t *p_value)
{
    (void)conn_id; (void)notify;
    APP_PRINT_INFO2("[central] notif handle 0x%x len %d", handle, value_size);
    /* EVENT (0xFFC2) carries the peer's proto frames (xfer responses + ACKs). */
    if (handle == s_event_handle)
    {
        hmi_l2_xfer_client_on_notify(p_value, value_size);
    }
    return APP_RESULT_SUCCESS;
}

static void cen_disconnect_cb(uint8_t conn_id)
{
    (void)conn_id;
    APP_PRINT_INFO0("[central] client disconnect_cb");
}

static const T_FUN_CLIENT_CBS s_hmi_client_cbs =
{
    cen_discover_state_cb,      /* discover_state_cb    */
    cen_discover_result_cb,     /* discover_result_cb   */
    cen_read_result_cb,         /* read_result_cb       */
    cen_write_result_cb,        /* write_result_cb      */
    cen_notif_ind_result_cb,    /* notify_ind_result_cb */
    cen_disconnect_cb,          /* disconnect_cb        */
};

/*============================================================================*
 *                              Lifecycle
 *============================================================================*/
void hmi_ble_central_init(void)
{
    /* Scan parameters (active scan so we also receive scan-response data). */
    uint8_t  scan_mode          = GAP_SCAN_MODE_ACTIVE;
    uint16_t scan_interval      = 0x50;   /* 0x50 * 0.625ms = 50ms  */
    uint16_t scan_window        = 0x30;   /* 0x30 * 0.625ms = 30ms  */
    uint8_t  scan_filter_policy = GAP_SCAN_FILTER_ANY;
    /* Duplicate filtering DISABLED on purpose: on this single-link stack a
     * running scan cannot be stopped/restarted (it wedges at GAP_SCAN_STATE_
     * START(1); see hmi_ble_central_start_scan), so a rescan must REUSE the one
     * running scan and merely clear the list.  For the list to refill, devices
     * still in range must keep re-reporting -- which dup-filtering would block. */
    uint8_t  scan_filter_dup    = GAP_SCAN_FILTER_DUPLICATE_DISABLE;

    le_scan_set_param(GAP_PARAM_SCAN_MODE, sizeof(scan_mode), &scan_mode);
    le_scan_set_param(GAP_PARAM_SCAN_INTERVAL, sizeof(scan_interval), &scan_interval);
    le_scan_set_param(GAP_PARAM_SCAN_WINDOW, sizeof(scan_window), &scan_window);
    le_scan_set_param(GAP_PARAM_SCAN_FILTER_POLICY, sizeof(scan_filter_policy), &scan_filter_policy);
    le_scan_set_param(GAP_PARAM_SCAN_FILTER_DUPLICATES, sizeof(scan_filter_dup), &scan_filter_dup);

    /* Connection parameters for the initiator.  The RTL87x3G stack requires
     * le_set_conn_param() to have been called before le_connect(); without it
     * le_connect_int returns cause 2 (GAP_CAUSE_INVALID_STATE) even when link
     * count is 0 and no prior initiator is pending.
     *
     * Values tuned for MAXIMUM THROUGHPUT (file push over the HMI L2 xfer):
     *   - conn_interval 7.5 ms..15 ms  (min = spec floor, max gives the peer a
     *     little accept margin; a smaller interval => more LL events per second
     *     => more MTU-sized packets per second)
     *   - conn_latency 0               (slave must never skip events during a
     *     transfer -- skipping is throughput loss, not power savings)
     *   - supv_tout 5 s                (spec: > (1+latency)*interval_max*2 =
     *     30 ms; 5 s is comfortably above and lets a real link loss surface fast)
     *   - ce_len 0xFFFF                (let the controller consume the full
     *     event; the header's example formula 2*(interval-1) collapses to 10
     *     at interval=6, throttling each event to a handful of packets) */
    {
        T_GAP_LE_CONN_REQ_PARAM cp;
        cp.scan_interval     = 0x60;                             /* 0x60 * 0.625 ms = 60 ms   */
        cp.scan_window       = 0x60;                             /* 100 % duty during initiate */
        cp.conn_interval_min = 6;                                /* 6  * 1.25 ms = 7.5 ms      */
        cp.conn_interval_max = 12;                               /* 12 * 1.25 ms = 15  ms      */
        cp.conn_latency      = 0;                                /* MUST be 0 for throughput   */
        cp.supv_tout         = 500;                              /* 500 * 10 ms = 5 s          */
        cp.ce_len_min        = 0xFFFF;                           /* controller picks max useful */
        cp.ce_len_max        = 0xFFFF;
        le_set_conn_param(GAP_CONN_PARAM_1M, &cp);
    }

    /* GATT client: reserve 1 specific client and register our callbacks. */
    client_init(1);
    if (client_register_spec_client_cb(&s_hmi_client, &s_hmi_client_cbs) == false)
    {
        s_hmi_client = CLIENT_PROFILE_GENERAL_ID;
        APP_PRINT_ERROR0("[central] client_register_spec_client_cb failed");
    }
    else
    {
        APP_PRINT_INFO1("[central] client registered, id %d", s_hmi_client);
    }

    s_state   = CEN_IDLE;
    s_conn_id = 0xFF;
    s_dev_cnt = 0;
    cen_reset_handles();
}

/*============================================================================*
 *                              UI control
 *============================================================================*/
/* Actually kick off scanning (adv is already idle at this point). */
static void cen_begin_scan(void)
{
    s_state = CEN_SCANNING;

    T_GAP_CAUSE cause = le_scan_start();
    if (cause != GAP_CAUSE_SUCCESS)
    {
        APP_PRINT_ERROR1("[central] le_scan_start failed 0x%x", cause);
        s_state = CEN_IDLE;
        hmi_ble_gap_start_adv();
        return;
    }
    s_scan_started = true;   /* scan is now physically running for the session */
    APP_PRINT_INFO0("[central] scanning, filter = name contains 'eBadge'");
}

bool hmi_ble_central_start_scan(void)
{
    /* The RTL87x3 single-link stack (le_gap_init(1)) never advances scan past
     * GAP_SCAN_STATE_START(1) while advertising runs concurrently: the
     * controller does scan (scan_info arrives) but the host's START->SCANNING
     * completion event never comes, so gap_scan_state is wedged at START(1)
     * forever -- the same temporary-state deadlock as adv wedged at STOP(3).
     * Once wedged, le_scan_stop()/le_scan_start() both return "invalid state 1",
     * and connect/disconnect never reset it either: the scan opened by the very
     * first le_scan_start() just keeps running for the whole session.  So start
     * it exactly once (s_scan_started) and, on EVERY later scan -- a UI rescan
     * or a rescan after connect/disconnect -- REUSE that running scan: clear the
     * software list and re-arm s_state so handle_scan_info() accepts reports
     * again.  Duplicate filtering is DISABLED (see init) so in-range devices
     * re-report within a few advertising intervals and refill the list; gone
     * devices stay gone.  A link being set up / up must not be disturbed. */
    if (s_state == CEN_CONNECTING || s_state == CEN_DISCOVERING || s_state == CEN_READY)
    {
        APP_PRINT_WARN1("[central] start_scan rejected, state %d", s_state);
        return false;
    }

    s_dev_cnt = 0;
    memset(s_devs, 0, sizeof(s_devs));

    if (s_scan_started)
    {
        /* Scan is already physically running (wedged at START) -- do NOT call
         * le_scan_start() again (returns invalid state 1).  Just re-arm the
         * state so incoming scan_info repopulates the freshly-cleared list.
         * This is what recovers a rescan after connect/disconnect, where
         * handle_disconnected reset s_state to CEN_IDLE while the scan kept
         * running (scan_info still arriving). */
        s_state = CEN_SCANNING;
        APP_PRINT_INFO1("[central] rescan: reuse running scan (adv_state=%d), list cleared",
                        hmi_ble_gap_get_adv_state());
        return true;
    }

    /* First scan of the session: actually start the scanner.  Do NOT stop
     * advertising here -- connecting as master while adv is already IDLE wedges
     * adv in GAP_ADV_STATE_STOP(3) (the connection's implicit adv-disable finds
     * nothing advertising and never completes).  Keeping adv ADVERTISING lets
     * the master connection auto-stop it cleanly (ADVERTISING->IDLE,
     * GAP_ADV_TO_IDLE_CAUSE_CONN), then le_adv_start() works after disconnect.
     * Scanning runs concurrently (observer+broadcaster). */
    APP_PRINT_INFO1("[central] start_scan: adv_state=%d (first scan, kept advertising)",
                    hmi_ble_gap_get_adv_state());
    cen_begin_scan();
    return true;
}

bool hmi_ble_central_stop_scan(void)
{
    if (s_state != CEN_SCANNING)
    {
        return false;
    }
    le_scan_stop();
    s_state = CEN_IDLE;
    hmi_ble_gap_start_adv();     /* back to receiver(advertising) */
    return true;
}

uint8_t hmi_ble_central_get_dev_count(void)
{
    return s_dev_cnt;
}

bool hmi_ble_central_get_dev(uint8_t idx, uint8_t bd_addr[6], uint8_t *addr_type,
                             int8_t *rssi, char *name, uint8_t name_len)
{
    if (idx >= s_dev_cnt)
    {
        return false;
    }
    T_CEN_DEV *d = &s_devs[idx];
    if (bd_addr)
    {
        memcpy(bd_addr, d->bd_addr, 6);
    }
    if (addr_type)
    {
        *addr_type = d->addr_type;
    }
    if (rssi)
    {
        *rssi = d->rssi;
    }
    if (name && name_len)
    {
        strncpy(name, d->name, name_len - 1);
        name[name_len - 1] = '\0';
    }
    return true;
}

bool hmi_ble_central_connect(uint8_t idx)
{
    if (s_state != CEN_SCANNING && s_state != CEN_IDLE)
    {
        APP_PRINT_WARN1("[central] connect rejected, state %d", s_state);
        return false;
    }
    if (idx >= s_dev_cnt)
    {
        APP_PRINT_ERROR2("[central] connect bad idx %d (cnt %d)", idx, s_dev_cnt);
        return false;
    }

    if (s_state == CEN_SCANNING)
    {
        le_scan_stop();
    }

    T_CEN_DEV *d = &s_devs[idx];
    s_state = CEN_CONNECTING;

    T_GAP_CAUSE cause = le_connect(GAP_PHYS_CONN_INIT_1M_BIT, d->bd_addr,
                                   (T_GAP_REMOTE_ADDR_TYPE)d->addr_type,
                                   GAP_LOCAL_ADDR_LE_PUBLIC, 1000 /* scan timeout */);
    if (cause != GAP_CAUSE_SUCCESS)
    {
        APP_PRINT_ERROR1("[central] le_connect failed 0x%x", cause);
        s_state = CEN_IDLE;
        hmi_ble_gap_start_adv();
        return false;
    }
    APP_PRINT_INFO1("[central] connecting to %s", TRACE_BDADDR(d->bd_addr));
    return true;
}

bool hmi_ble_central_disconnect(void)
{
    if (s_conn_id == 0xFF)
    {
        return false;
    }
    return (le_disconnect(s_conn_id) == GAP_CAUSE_SUCCESS);
}

bool hmi_ble_central_send_file(uint8_t type, const uint8_t *src, uint32_t total,
                               const char *fname, xfer_client_done_cb_t done_cb)
{
    if (s_state != CEN_READY)
    {
        APP_PRINT_WARN1("[central] send_file rejected, not READY (state %d)", s_state);
        return false;
    }
    return hmi_l2_xfer_client_start(s_conn_id, s_hmi_client, s_cmd_handle,
                                    type, src, total, fname, done_cb);
}

bool hmi_ble_central_get_send_progress(uint32_t *bytes_sent, uint32_t *total,
                                       T_XFER_CLIENT_PHASE *phase)
{
    return hmi_l2_xfer_client_get_progress(bytes_sent, total, phase);
}

bool hmi_ble_central_is_active(void)
{
    return (s_state != CEN_IDLE);
}

bool hmi_ble_central_is_ready(void)
{
    /* Only CEN_READY means: link up, HMI ctrl service discovered, CMD/EVENT
     * handles resolved and EVENT notifications enabled -- i.e. the only state
     * in which hmi_ble_central_send_file() will be accepted.  CEN_CONNECTING /
     * CEN_DISCOVERING are "almost ready" but a send there is still rejected. */
    return (s_state == CEN_READY);
}

/*============================================================================*
 *                       Hooks from the GAP layer
 *============================================================================*/
void hmi_ble_central_handle_adv_state(uint8_t adv_state)
{
    /* Second half of the serialized adv-stop -> scan-start: once advertising
     * has fully stopped (IDLE), it is safe to start scanning. */
    if (s_state == CEN_ADV_STOPPING && adv_state == GAP_ADV_STATE_IDLE)
    {
        cen_begin_scan();
    }
}

void hmi_ble_central_handle_scan_state(uint8_t scan_state)
{
    /* Defensive self-heal.  On this single-link stack the scan never leaves
     * START(1) once running while advertising (see hmi_ble_central_start_scan),
     * so this is effectively never taken.  But should the scan ever genuinely
     * return to IDLE (a different stack, or firmware that really stops it), drop
     * the "started" flag so the next start_scan re-issues le_scan_start()
     * instead of reusing a scan that is no longer physically running. */
    if (scan_state == GAP_SCAN_STATE_IDLE)
    {
        s_scan_started = false;
    }
}

void hmi_ble_central_handle_scan_info(T_LE_SCAN_INFO *p_info)
{
    if (p_info == NULL || s_state != CEN_SCANNING)
    {
        return;
    }

    char name[HMI_CENTRAL_NAME_LEN] = {0};
    if (cen_parse_name(p_info->data, p_info->data_len, name, sizeof(name)) == false)
    {
        return;
    }
    /* Software filter: keep only devices whose name contains "eBadge". */
    if (strstr(name, "eBadge") == NULL)
    {
        return;
    }

    /* De-duplicate by address; refresh RSSI if already seen. */
    for (uint8_t i = 0; i < s_dev_cnt; i++)
    {
        if (memcmp(s_devs[i].bd_addr, p_info->bd_addr, 6) == 0)
        {
            s_devs[i].rssi = p_info->rssi;
            return;
        }
    }
    if (s_dev_cnt >= HMI_CENTRAL_MAX_DEVS)
    {
        return;
    }

    T_CEN_DEV *d = &s_devs[s_dev_cnt];
    memcpy(d->bd_addr, p_info->bd_addr, 6);
    d->addr_type = (uint8_t)p_info->remote_addr_type;
    d->rssi      = p_info->rssi;
    strncpy(d->name, name, HMI_CENTRAL_NAME_LEN - 1);
    d->name[HMI_CENTRAL_NAME_LEN - 1] = '\0';
    s_dev_cnt++;

    /* Two %s in one Realtek trace line garbles the args -- print MAC and name
     * separately (MAC via TRACE_BDADDR, name via TRACE_STRING). */
    APP_PRINT_INFO3("[central] found eBadge #%d mac %s rssi %d",
                    s_dev_cnt - 1, TRACE_BDADDR(d->bd_addr), d->rssi);
    APP_PRINT_INFO1("[central]   name %s", TRACE_STRING(d->name));
}

void hmi_ble_central_handle_connected(uint8_t conn_id)
{
    s_conn_id = conn_id;
    s_state   = CEN_DISCOVERING;
    cen_reset_handles();

    /* Negotiate a larger ATT MTU up front so xfer DATA chunks are big. */
    client_send_exchange_mtu_req(conn_id);

    APP_PRINT_INFO2("[central] connected conn_id %d (adv_state=%d), discovering HMI service",
                    conn_id, hmi_ble_gap_get_adv_state());

    T_GAP_CAUSE cause = client_by_uuid128_srv_discovery(conn_id, s_hmi_client,
                                                        (uint8_t *)GATT_UUID128_HMI_SERVICE);
    if (cause != GAP_CAUSE_SUCCESS)
    {
        APP_PRINT_ERROR1("[central] srv discovery request failed 0x%x", cause);
        le_disconnect(conn_id);
    }
}

void hmi_ble_central_handle_disconnected(uint8_t conn_id, uint16_t disc_cause)
{
    APP_PRINT_INFO2("[central] disconnected conn_id %d cause 0x%x", conn_id, disc_cause);
    hmi_l2_xfer_client_on_disconnect();   /* fail any in-progress transfer */
    s_conn_id = 0xFF;
    s_state   = CEN_IDLE;
    cen_reset_handles();
    /* GAP layer restarts advertising -> device returns to receiver state. */
}
