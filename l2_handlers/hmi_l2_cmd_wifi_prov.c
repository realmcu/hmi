#include "hmi_l2_cmd_wifi_prov.h"
#include "protocol/hmi_l2.h"
#include "protocol/hmi_proto.h"
#include "protocol/hmi_protocal_task.h"
#include "protocol/proto_log.h"
#include "wifi_ctrl.h"
#include "app_timer.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* 设备 TCP 服务监听端口（与 BLE_PROTOCOL_SPEC 默认值一致）*/
#define WIFI_PROV_TCP_PORT  8783u

/* 配网兜底超时：连接迟迟不出终态时主动报 FAILED 并解锁会话。
 * 覆盖最坏的链式 AT 超时（ATPN→ATWQ→ATWT，每步独立 20s 超时），故取 60s。*/
#define WIFI_PROV_TIMEOUT_MS  60000u

/* WiFi 连接结果——在 wifi_task 上下文产生，经 hmi_proto_post_call 投递到
 * l2_task 后由 wifi_result_call 消费并上报 WIFI_STATUS。本模块私有。*/
typedef struct
{
    uint16_t req_id;
    uint8_t  state;   /* HMI_L2_WIFI_STATE_* */
    uint8_t  error;   /* HMI_L2_WIFI_STATUS_ERR_*（state=FAILED 时有效）*/
    uint16_t port;    /* TCP 服务端口（state=CONNECTED 时有效）*/
    char     ip[16];  /* 点分十进制 IP + '\0'；未连接为空串 */
} wifi_result_t;

static uint16_t s_wifi_req_id = 0;
static uint8_t  s_wifi_state  = HMI_L2_WIFI_STATE_IDLE;
static uint8_t  s_wifi_error  = HMI_L2_WIFI_STATUS_ERR_NONE;
static bool     s_wifi_busy   = false;
static char     s_wifi_ip[16] = {0};   /* 已连接时缓存 IP，供 STATUS_REQ 轮询回填 */
static uint16_t s_wifi_port   = 0;
static uint8_t  s_wifi_timer_module_id = 0;
static uint8_t  s_wifi_timer_handle    = 0;
static char     s_pending_ip[16] = {0};   /* 连上后暂存 IP，待 TCP server 就绪随 CONNECTED 上报 */

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

/* l2_task 上下文回调：消费一条 WiFi 连接结果，更新本地状态/缓存并上报
 * WIFI_STATUS，最后释放堆负载。经 hmi_proto_post_call 投递（见 post_wifi_result）。*/
static void wifi_result_call(l2_msg_t *p_msg)
{
    wifi_result_t *r = (wifi_result_t *)p_msg->u.buf;
    if (r == NULL)
    {
        return;
    }

    /* 丢弃过期结果：会话已结束(busy=false)或非当前会话(req_id 不符)。
     * 真实结果与超时兜底竞争时"先到者生效"，后到者在此被忽略。*/
    if (!s_wifi_busy || r->req_id != s_wifi_req_id)
    {
        free(r);
        return;
    }

    /* 收到终态结果，停掉配网兜底定时器 */
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

/* 在 WiFi 任务上下文调用：堆分配一份结果并投递到 l2_task（由 wifi_result_call
 * 释放）。不在此处直接发送，因 proto_send 非线程安全、只能在 l2_task 调用。*/
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

/* 配网兜底超时回调（app_timer 上下文，非 l2_task）：连接长时间未出终态时
 * 主动判失败。同样经 post_wifi_result 绕回 l2_task 上报，不在此直接发送。*/
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

/* 从一行文本中提取首个点分十进制 IPv4（如 "192.168.1.100"）。
 * 与具体 AT 响应字段顺序无关——只要 IP 以点分形式出现即可命中。
 * 命中且非 0.0.0.0 时返回 true 并写入 out（含 '\0'）。
 *
 * 注意：取的是"首个"点分四段。若 ATWQ 的 STA 行把子网掩码/网关排在
 *       本机 IP 之前，可能误取——对照下方 PROTO_LOG 打印的原始行核对，
 *       必要时改成跳过特定字段。*/
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

        /* 末尾若紧跟数字说明匹配越界（如 a.b.c.d.e），视为无效 */
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

/* ATWT(-s,-p,port) TCP server 开启响应回调——运行在 WiFi 任务上下文。
 * server 就绪后才上报 CONNECTED(ip,port)，确保上位机连入时设备已在监听，
 * 避免出现"报了 CONNECTED 但端口还没 listen → 连接被拒"。
 * [ATWT] 响应格式未文档化，乐观视为开启成功。*/
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

/* ATWQ（查询 IP/MAC/GW）响应回调——运行在 WiFi 任务上下文。
 * 以"是否拿到有效 IP"判定连接是否真正成功。拿到 IP 后先开 TCP server，
 * 待其就绪（on_tcp_open_rsp）再上报 CONNECTED。*/
static bool on_wifi_info_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    (void)cmd;
    PROTO_LOG("L2 WIFI ATWQ rsp: %s", rsp ? rsp : "(null)");

    /* 仅处理 STA 信息结果行，忽略命令执行期间的中间行，避免误报失败 */
    if (rsp == NULL || strncmp(rsp, "STA", 3) != 0)
    {
        return true;
    }

    if (extract_ipv4(rsp, s_pending_ip, sizeof(s_pending_ip)))
    {
        /* 连上且拿到 IP：开 8783 TCP server；就绪后由 on_tcp_open_rsp 报 CONNECTED */
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
        /* 未拿到有效 IP：按连接失败处理（DHCP/认证/无 AP 等无法细分，记为超时）*/
        wifi_result_t r;
        memset(&r, 0, sizeof(r));
        r.req_id = s_wifi_req_id;
        r.state  = HMI_L2_WIFI_STATE_FAILED;
        r.error  = HMI_L2_WIFI_STATUS_ERR_TIMEOUT;
        post_wifi_result(&r);
    }
    return true;
}

/* ATPN（连接 AP）响应回调——运行在 WiFi 任务上下文。
 * ATPN 的成功/失败文本格式未文档化，故不据其措辞判定，而是统一链式
 * 触发一次 ATWQ 查询 IP，以"是否拿到有效 IP"作为成功判据（见 on_wifi_info_rsp）。*/
static bool on_wifi_connect_rsp(T_ATCMD_TYPE cmd, const char *rsp)
{
    (void)cmd;
    PROTO_LOG("L2 WIFI ATPN rsp: %s", rsp ? rsp : "(null)");

    /* 仅在收到 [ATPN] 结果行时推进，忽略中间行，确保只触发一次 ATWQ */
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

                /* payload 中 ssid/pwd 不含 '\0'，先拷贝成 C 字符串再交给 WiFi
                 * 协议栈。wifi_ctrl_connect 内部会立即把凭据拷入 AT 命令队列，
                 * 故此处栈上缓冲在调用返回后即可释放。
                 * 等效于 shell 命令：wifi connect <ssid> <pwd> */
                char ssid_str[33];  /* 32 + '\0' */
                char pwd_str[65];   /* 64 + '\0' */
                memcpy(ssid_str, ssid, ssid_len);
                ssid_str[ssid_len] = '\0';
                memcpy(pwd_str, pwd, pwd_len);
                pwd_str[pwd_len] = '\0';
                (void)flags;  /* FLAG_SAVE_CREDENTIALS 暂未实现 */

                if (!wifi_ctrl_connect(ssid_str, pwd_str, on_wifi_connect_rsp))
                {
                    PROTO_LOG("L2 WIFI connect enqueue fail");
                    s_wifi_state = HMI_L2_WIFI_STATE_FAILED;
                    s_wifi_busy  = false;
                    wifi_prov_send_status(req_id, HMI_L2_WIFI_STATE_FAILED,
                                          HMI_L2_WIFI_STATUS_ERR_UNKNOWN, NULL, 0);
                    break;
                }

                /* 启动配网兜底定时器：超时仍未出终态则主动报 FAILED、解锁会话 */
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
