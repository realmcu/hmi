#include "hmi_l2_cmd_wifi_prov.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "hmi_protocal_task.h"
#include "proto_log.h"
#include "wifi_ctrl.h"
#include "app_timer.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Device TCP server listen port (matches the BLE_PROTOCOL_SPEC default) */
#define WIFI_PROV_TCP_PORT  8783u

/* Provisioning guard timeout: report FAILED and release the session when the
 * connection stays pending. Worst chained AT timeout (ATPN->ATWQ->ATWT, 20s each), hence 60s. */
#define WIFI_PROV_TIMEOUT_MS  60000u

/* WiFi connection result: produced in the wifi_task context, posted via hmi_proto_post_call
 * to l2_task, consumed by wifi_result_call which reports WIFI_STATUS. Private to this module. */
typedef struct
{
    uint16_t req_id;
    uint8_t  state;   /* HMI_L2_WIFI_STATE_* */
    uint8_t  error;   /* HMI_L2_WIFI_STATUS_ERR_* (valid when state=FAILED) */
    uint16_t port;    /* TCP server port (valid when state=CONNECTED) */
    char     ip[16];  /* dotted-decimal IP + '\0'; empty string when not connected */
} wifi_result_t;

static uint16_t s_wifi_req_id = 0;
static uint8_t  s_wifi_state  = HMI_L2_WIFI_STATE_IDLE;
static uint8_t  s_wifi_error  = HMI_L2_WIFI_STATUS_ERR_NONE;
static bool     s_wifi_busy   = false;
static char     s_wifi_ip[16] = {0};   /* cached IP when connected, returned on STATUS_REQ polls */
static uint16_t s_wifi_port   = 0;
static uint8_t  s_wifi_timer_module_id = 0;
static uint8_t  s_wifi_timer_handle    = 0;
static char     s_pending_ip[16] = {0};   /* IP staged after connect, reported with CONNECTED once ready */

static void wifi_prov_send(uint8_t key, const uint8_t *val, uint16_t val_len)
{
    /* 2 (L2 hdr) + 3 (key + v_len_hi + v_len_lo) + val_len */
    uint8_t  buf[2 + 3 + 64 + 32 + 8];  /* generous for status payload */
    uint16_t pos = 0;

    buf[pos++] = HMI_L2_CMD_WIFI_PROV;
    buf[pos++] = 0x00u;
    buf[pos++] = key;
    buf[pos++] = (uint8_t)((val_len >> 8) & 0xFFu);
    buf[pos++] = (uint8_t)(val_len & 0xFFu);
    for (uint16_t i = 0; i < val_len && pos < sizeof(buf); i++)
    {
        buf[pos++] = val[i];
    }
    proto_send(buf, pos);
}

static void wifi_prov_send_ack(uint16_t req_id, uint8_t result, uint8_t error)
{
    uint8_t val[4];
    val[0] = (uint8_t)(req_id >> 8);
    val[1] = (uint8_t)(req_id & 0xFFu);
    val[2] = result;
    val[3] = error;
    wifi_prov_send(HMI_L2_WIFI_CONFIG_ACK, val, sizeof(val));
}

static void wifi_prov_send_status(uint16_t req_id, uint8_t state, uint8_t error,
                                  const char *ip, uint16_t port)
{
    uint8_t  ip_buf[16] = {0};  /* max "255.255.255.255" = 15 chars */
    uint8_t  ip_len = 0;

    if (ip != NULL)
    {
        while (ip_len < sizeof(ip_buf) && ip[ip_len] != '\0')
        {
            ip_buf[ip_len] = (uint8_t)ip[ip_len];
            ip_len++;
        }
    }

    uint8_t  val[6 + sizeof(ip_buf)];
    uint16_t pos = 0;
    val[pos++] = (uint8_t)(req_id >> 8);
    val[pos++] = (uint8_t)(req_id & 0xFFu);
    val[pos++] = state;
    val[pos++] = error;
    val[pos++] = ip_len;
    for (uint8_t i = 0; i < ip_len; i++)
    {
        val[pos++] = ip_buf[i];
    }
    val[pos++] = (uint8_t)(port >> 8);
    val[pos++] = (uint8_t)(port & 0xFFu);
    wifi_prov_send(HMI_L2_WIFI_STATUS, val, pos);
}

/* l2_task context callback: consume one WiFi connection result, update the local
 * state/cache, report WIFI_STATUS and free the heap payload; posted by post_wifi_result. */
static void wifi_result_call(l2_msg_t *p_msg)
{
    wifi_result_t *r = (wifi_result_t *)p_msg->u.buf;
    if (r == NULL)
    {
        return;
    }

    /* Drop stale results: session already over (busy=false) or not the current session (req_id mismatch).
     * When a real result races the timeout fallback the first one wins; later arrivals are ignored here. */
    if (!s_wifi_busy || r->req_id != s_wifi_req_id)
    {
        free(r);
        return;
    }

    /* Final-state result received, stop the provisioning guard timer */
    app_stop_timer(&s_wifi_timer_handle);

    s_wifi_state = r->state;
    s_wifi_error = r->error;
    s_wifi_busy  = (r->state == HMI_L2_WIFI_STATE_CONNECTING);

    if (r->state == HMI_L2_WIFI_STATE_CONNECTED)
    {
        strncpy(s_wifi_ip, r->ip, sizeof(s_wifi_ip) - 1);
        s_wifi_ip[sizeof(s_wifi_ip) - 1] = '\0';
        s_wifi_port = r->port;
    }
    else
    {
        s_wifi_ip[0] = '\0';
        s_wifi_port  = 0;
    }

    PROTO_LOG("L2 WIFI result req_id=%d state=%d err=%d ip=%s port=%d",
              r->req_id, r->state, r->error,
              (s_wifi_ip[0] != '\0') ? s_wifi_ip : "-", r->port);

    wifi_prov_send_status(r->req_id, r->state, r->error,
                          (s_wifi_ip[0] != '\0') ? s_wifi_ip : NULL, r->port);

    free(r);
}

/* Called in the WiFi task context: heap-allocate a result and post it to l2_task
 * (freed by wifi_result_call). Not sent here: proto_send is not thread-safe, l2_task only. */
static void post_wifi_result(const wifi_result_t *src)
{
    wifi_result_t *r = malloc(sizeof(*r));
    if (r == NULL)
    {
        PROTO_LOG("L2 WIFI result alloc fail");
        return;
    }
    *r = *src;
    if (!hmi_proto_post_call(wifi_result_call, r))
    {
        free(r);
    }
}

/* Provisioning guard timeout callback (app_timer context, not l2_task): declare
 * failure when the connection stays non-final. Routed back via post_wifi_result, not sent here. */
static void wifi_prov_timeout_cb(uint8_t timer_evt, uint16_t param)
{
    (void)timer_evt;
    (void)param;
    app_stop_timer(&s_wifi_timer_handle);

    PROTO_LOG("L2 WIFI provisioning timeout, report FAILED");

    wifi_result_t r;
    memset(&r, 0, sizeof(r));
    r.req_id = s_wifi_req_id;
    r.state  = HMI_L2_WIFI_STATE_FAILED;
    r.error  = HMI_L2_WIFI_STATUS_ERR_TIMEOUT;
    post_wifi_result(&r);
}

/* Extract the first dotted-decimal IPv4 (e.g. "192.168.1.100") from a line of text.
 * Independent of the AT response field order: any dotted-quad hit works.
 * Returns true and writes out (with '\0') on a hit that is not 0.0.0.0.
 *
 * Note: this takes the "first" dotted quad. If the ATWQ STA line puts the netmask/gateway
 *       ahead of the local IP it may grab the wrong one -- check against the raw line
 *       logged by PROTO_LOG below and skip specific fields if needed. */
static bool extract_ipv4(const char *line, char *out, uint8_t out_sz)
{
    if (line == NULL || out == NULL || out_sz < 8u)
    {
        return false;
    }

    for (const char *p = line; *p != '\0'; p++)
    {
        const char *q       = p;
        bool        ok       = true;
        bool        nonzero  = false;
        uint8_t     groups;

        for (groups = 0; groups < 4u; groups++)
        {
            uint16_t v      = 0;
            uint8_t  digits = 0;
            while (*q >= '0' && *q <= '9' && digits < 3u)
            {
                v = (uint16_t)(v * 10u + (uint16_t)(*q - '0'));
                q++;
                digits++;
            }
            if (digits == 0 || v > 255u)
            {
                ok = false;
                break;
            }
            if (v != 0)
            {
                nonzero = true;
            }
            if (groups < 3u)
            {
                if (*q != '.')
                {
                    ok = false;
                    break;
                }
                q++;
            }
        }

        /* A trailing digit means the match ran past the end (e.g. a.b.c.d.e); invalid */
        if (ok && groups == 4u && !(*q >= '0' && *q <= '9') && nonzero)
        {
            uint16_t len = (uint16_t)(q - p);
            if (len < out_sz)
            {
                memcpy(out, p, len);
                out[len] = '\0';
                return true;
            }
        }
    }
    return false;
}

/* ATWT(-s,-p,port) TCP server open response callback -- runs in the WiFi task context.
 * CONNECTED(ip,port) is only reported once the server is ready, so the device is
 * already listening when the host connects (no "CONNECTED but not listening" refusal).
 * The [ATWT] response format is undocumented; optimistically treat it as success. */
static bool on_tcp_open_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    (void)cmd;
    PROTO_LOG("L2 WIFI ATWT rsp: %s", rsp ? rsp : "(null)");

    if (rsp == NULL || strncmp(rsp, "[ATWT]", 6) != 0)
    {
        return true;
    }

    wifi_result_t r;
    memset(&r, 0, sizeof(r));
    r.req_id = s_wifi_req_id;
    r.state  = HMI_L2_WIFI_STATE_CONNECTED;
    r.error  = HMI_L2_WIFI_STATUS_ERR_NONE;
    r.port   = WIFI_PROV_TCP_PORT;
    strncpy(r.ip, s_pending_ip, sizeof(r.ip) - 1);
    r.ip[sizeof(r.ip) - 1] = '\0';
    post_wifi_result(&r);
    return true;
}

/* ATWQ (query IP/MAC/GW) response callback -- runs in the WiFi task context.
 * A valid IP decides whether the connection really succeeded. Once the IP is in,
 * open the TCP server first and report CONNECTED after it is ready (on_tcp_open_rsp). */
static bool on_wifi_info_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    (void)cmd;
    PROTO_LOG("L2 WIFI ATWQ rsp: %s", rsp ? rsp : "(null)");

    /* Handle only the STA info result line; ignore intermediate lines to avoid false failures */
    if (rsp == NULL || strncmp(rsp, "STA", 3) != 0)
    {
        return true;
    }

    if (extract_ipv4(rsp, s_pending_ip, sizeof(s_pending_ip)))
    {
        /* Connected with a valid IP: open the 8783 TCP server; on_tcp_open_rsp reports CONNECTED */
        if (!wifi_ctrl_tcp_open(NULL, WIFI_PROV_TCP_PORT, true, on_tcp_open_rsp))
        {
            wifi_result_t r;
            memset(&r, 0, sizeof(r));
            r.req_id = s_wifi_req_id;
            r.state  = HMI_L2_WIFI_STATE_FAILED;
            r.error  = HMI_L2_WIFI_STATUS_ERR_TCP;
            post_wifi_result(&r);
        }
    }
    else
    {
        /* No valid IP: treat as connection failure (cannot separate DHCP/auth/no AP, log as timeout) */
        wifi_result_t r;
        memset(&r, 0, sizeof(r));
        r.req_id = s_wifi_req_id;
        r.state  = HMI_L2_WIFI_STATE_FAILED;
        r.error  = HMI_L2_WIFI_STATUS_ERR_TIMEOUT;
        post_wifi_result(&r);
    }
    return true;
}

/* ATPN (connect AP) response callback -- runs in the WiFi task context.
 * The ATPN success/failure text format is undocumented, so do not judge by its
 * wording; always chain one ATWQ IP query and use a valid IP as the success criterion. */
static bool on_wifi_connect_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    (void)cmd;
    PROTO_LOG("L2 WIFI ATPN rsp: %s", rsp ? rsp : "(null)");

    /* Advance only on the [ATPN] result line; ignore intermediate lines so ATWQ fires once */
    if (rsp == NULL || strncmp(rsp, "[ATPN]", 6) != 0)
    {
        return true;
    }

    if (!wifi_ctrl_info(on_wifi_info_rsp))
    {
        wifi_result_t r;
        memset(&r, 0, sizeof(r));
        r.req_id = s_wifi_req_id;
        r.state  = HMI_L2_WIFI_STATE_FAILED;
        r.error  = HMI_L2_WIFI_STATUS_ERR_UNKNOWN;
        post_wifi_result(&r);
    }
    return true;
}

static void on_cmd_wifi_prov(const hmi_l2_kv_t *kvs, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        uint8_t        key = kvs[i].key;
        const uint8_t *val = kvs[i].val;
        uint16_t       vl  = kvs[i].val_len;

        switch (key)
        {
        case HMI_L2_WIFI_CONFIG_SET:
            {
                if (vl < 4)
                {
                    PROTO_LOG("L2 WIFI CONFIG_SET too short (%d)", vl);
                    wifi_prov_send_ack(0, HMI_L2_WIFI_ACK_REJECTED,
                                       HMI_L2_WIFI_ERR_MALFORMED);
                    break;
                }

                uint16_t req_id   = ((uint16_t)val[0] << 8) | val[1];
                uint8_t  flags    = val[2];
                uint8_t  ssid_len = val[3];
                uint16_t offset   = 4;

                if (ssid_len == 0 || ssid_len > 32 || offset + ssid_len > vl)
                {
                    wifi_prov_send_ack(req_id, HMI_L2_WIFI_ACK_REJECTED,
                                       HMI_L2_WIFI_ERR_INVALID_SSID);
                    break;
                }

                const uint8_t *ssid = val + offset;
                offset += ssid_len;

                if (offset >= vl)
                {
                    wifi_prov_send_ack(req_id, HMI_L2_WIFI_ACK_REJECTED,
                                       HMI_L2_WIFI_ERR_MALFORMED);
                    break;
                }

                uint8_t        pwd_len = val[offset++];
                const uint8_t *pwd     = val + offset;

                if (pwd_len > 64 || offset + pwd_len > vl)
                {
                    wifi_prov_send_ack(req_id, HMI_L2_WIFI_ACK_REJECTED,
                                       HMI_L2_WIFI_ERR_INVALID_PWD);
                    break;
                }

                if (s_wifi_busy)
                {
                    wifi_prov_send_ack(req_id, HMI_L2_WIFI_ACK_REJECTED,
                                       HMI_L2_WIFI_ERR_BUSY);
                    break;
                }

                s_wifi_req_id = req_id;
                s_wifi_state  = HMI_L2_WIFI_STATE_CONNECTING;
                s_wifi_busy   = true;

                PROTO_LOG("L2 WIFI CONFIG_SET req_id=%d ssid_len=%d pwd_len=%d flags=0x%02x",
                          req_id, ssid_len, pwd_len, flags);

                wifi_prov_send_ack(req_id, HMI_L2_WIFI_ACK_ACCEPTED,
                                   HMI_L2_WIFI_ERR_NONE);

                /* ssid/pwd in the payload have no '\0'; copy them into C strings before
                 * handing them to the WiFi stack. wifi_ctrl_connect copies the credentials into
                 * the AT command queue immediately, so these stack buffers may be released on return.
                 * Equivalent to the shell command: wifi connect <ssid> <pwd> */
                char ssid_str[33];  /* 32 + '\0' */
                char pwd_str[65];   /* 64 + '\0' */
                memcpy(ssid_str, ssid, ssid_len);
                ssid_str[ssid_len] = '\0';
                memcpy(pwd_str, pwd, pwd_len);
                pwd_str[pwd_len] = '\0';
                (void)flags;  /* FLAG_SAVE_CREDENTIALS not implemented yet */

                if (!wifi_ctrl_connect(ssid_str, pwd_str, on_wifi_connect_rsp))
                {
                    PROTO_LOG("L2 WIFI connect enqueue fail");
                    s_wifi_state = HMI_L2_WIFI_STATE_FAILED;
                    s_wifi_busy  = false;
                    wifi_prov_send_status(req_id, HMI_L2_WIFI_STATE_FAILED,
                                          HMI_L2_WIFI_STATUS_ERR_UNKNOWN, NULL, 0);
                    break;
                }

                /* Start the provisioning guard timer: report FAILED and release on timeout */
                app_start_timer(&s_wifi_timer_handle, "wifiprov",
                                s_wifi_timer_module_id, 0, 0, false,
                                WIFI_PROV_TIMEOUT_MS);

                wifi_prov_send_status(req_id, HMI_L2_WIFI_STATE_CONNECTING,
                                      HMI_L2_WIFI_STATUS_ERR_NONE, NULL, 0);
                break;
            }

        case HMI_L2_WIFI_STATUS_REQ:
            {
                uint16_t req_id = (vl >= 2)
                                  ? (uint16_t)(((uint16_t)val[0] << 8) | val[1])
                                  : s_wifi_req_id;

                PROTO_LOG("L2 WIFI STATUS_REQ req_id=%d state=%d", req_id, s_wifi_state);

                bool        connected = (s_wifi_state == HMI_L2_WIFI_STATE_CONNECTED);
                const char *ip   = (connected && s_wifi_ip[0] != '\0') ? s_wifi_ip : NULL;
                uint16_t    port = connected ? s_wifi_port : 0;
                uint8_t     err  = (s_wifi_state == HMI_L2_WIFI_STATE_FAILED)
                                   ? s_wifi_error : HMI_L2_WIFI_STATUS_ERR_NONE;

                wifi_prov_send_status(req_id, s_wifi_state, err, ip, port);
                break;
            }

        default:
            PROTO_LOG("L2 WIFI unknown key=0x%02x", key);
            break;
        }
    }
}

void hmi_l2_wifi_prov_register(void)
{
    hmi_l2_register(HMI_L2_CMD_WIFI_PROV, on_cmd_wifi_prov);

    if (s_wifi_timer_module_id == 0)
    {
        app_timer_reg_cb(wifi_prov_timeout_cb, &s_wifi_timer_module_id);
    }
}
