/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "wifi_atcmd.h"
#include "os_mem.h"
#include "os_queue.h"
#include "app_timer.h"
#include "wifi_uart.h"
#include "wifi_types.h"
#include "wifi_task.h"

#define MIN_RSP_LEN         5
#define ATCMD_RX_BUF_SIZE   256
#define ATCMD_TIMEOUT_MS    20000

/* ---- AT CMD 队列节点 ---- */
typedef struct t_atcmd_queue_node
{
    struct t_atcmd_queue_node *p_next; /* 必须在首位 */
    T_ATCMD_TYPE               cmd;
    T_ATCMD_RSP_CB             cb;
    char                       param[0];
} T_ATCMD_QUEUE_NODE;

/* ---- 命令解析状态 ---- */
typedef struct
{
    T_ATCMD_TYPE cur_cmd;
    uint8_t      rx_buf[ATCMD_RX_BUF_SIZE];
    uint16_t     rx_cnt;
} T_ATCMD_STATE;

/* ---- 命令表条目 ---- */
typedef bool (*T_RSP_FUNC)(const char *rsp_line, T_ATCMD_RSP_CB cb);

typedef struct
{
    const char *cmd_str;
    const char *rsp_prefix;
    T_RSP_FUNC  rsp_func;
} T_ATCMD_ENTRY;

static bool rsp_default(const char *rsp, T_ATCMD_RSP_CB cb);

static const T_ATCMD_ENTRY at_cmd_table[ATCMD_NUM] =
{
    [ATCMD_ATWS] = {"ATWS",  "[ATWS]", rsp_default},
    [ATCMD_ATW0] = {"ATW0=", "[ATW0]", NULL},
    [ATCMD_ATW1] = {"ATW1=", "[ATW1]", NULL},
    [ATCMD_ATWC] = {"ATWC",  "[ATWC]", NULL},
    [ATCMD_ATWI] = {"ATWI=", "[ATWI]", rsp_default},
    [ATCMD_ATWU] = {"ATWU=", "[ATWU]", rsp_default},
    [ATCMD_ATWT] = {"ATWT=", "[ATWT]", rsp_default},
    [ATCMD_ATPN] = {"ATPN=", "[ATPN]", rsp_default},
    [ATCMD_ATSL] = {"ATSL=", "[ATSL]", NULL},
    [ATCMD_ATWO] = {"ATWO=", "[ATWO]", NULL},
    [ATCMD_ATSD] = {"ATSD=", "[ATSD]", NULL},
    [ATCMD_ATST] = {"ATST=", "[ATST]", rsp_default},
    [ATCMD_ATWQ] = {"ATW?",  "STA,",   rsp_default},
};

static T_ATCMD_STATE s_atcmd = {.cur_cmd = ATCMD_NUM};
static T_OS_QUEUE    s_atcmd_queue;
static uint8_t       s_timer_module_id = 0;
static uint8_t       s_timer_handle    = 0;

static bool rsp_default(const char *rsp, T_ATCMD_RSP_CB cb)
{
    printf("[atcmd] rsp: %s\n", rsp);
    if (cb)
    {
        cb(s_atcmd.cur_cmd, rsp);
    }
    return true;
}

static bool atcmd_send_raw(T_ATCMD_TYPE cmd, const char *param)
{
    uint16_t cmd_len   = strlen(at_cmd_table[cmd].cmd_str);
    uint16_t param_len = param ? strlen(param) : 0;
    uint16_t total     = cmd_len + param_len + 2;

    uint8_t *buf = os_mem_alloc(OS_MEM_TYPE_DATA, total);
    if (!buf)
    {
        return false;
    }

    memcpy(buf, at_cmd_table[cmd].cmd_str, cmd_len);
    if (param_len)
    {
        memcpy(buf + cmd_len, param, param_len);
    }
    buf[total - 2] = '\r';
    buf[total - 1] = '\n';

    bool ret = wifi_uart_tx(buf, total);
    os_mem_free(buf);
    return ret;
}

static void atcmd_trigger_next(void)
{
    T_WIFI_MSG msg = {.event = EVENT_UART_CMD_FLOW_CTRL};
    if (wifi_task_send_msg(&msg) == false)
    {
        printf("[atcmd] flow ctrl msg send fail\n");
    }
}

static void atcmd_timeout_cb(uint8_t timer_evt, uint16_t param)
{
    app_stop_timer(&s_timer_handle);

    /* 超时即丢弃当前命令并推进队列。
     * 注：原 resend 机制无法工作——flow_ctrl 见 cur_cmd 非空会拒发，
     * 重发分支既不重发也不推进，反而使命令永久卡死，故彻底移除。*/
    printf("[atcmd] timeout, drop cmd %d\n", s_atcmd.cur_cmd);
    s_atcmd.cur_cmd = ATCMD_NUM;
    s_atcmd.rx_cnt  = 0;

    T_ATCMD_QUEUE_NODE *done = os_queue_out(&s_atcmd_queue);
    if (done)
    {
        os_mem_free(done);
    }
    atcmd_trigger_next();
}

/* ---- 公开接口 ---- */

void wifi_atcmd_init(void)
{
    if (s_timer_module_id == 0)
    {
        app_timer_reg_cb(atcmd_timeout_cb, &s_timer_module_id);
    }
    os_queue_init(&s_atcmd_queue);
}

bool wifi_atcmd_enqueue(T_ATCMD_TYPE cmd, const char *param, T_ATCMD_RSP_CB cb)
{
    if (cmd >= ATCMD_NUM)
    {
        printf("[atcmd] invalid cmd %d\n", cmd);
        return false;
    }

    uint16_t param_len = param ? strlen(param) + 1 : 1;
    T_ATCMD_QUEUE_NODE *node = os_mem_alloc(OS_MEM_TYPE_DATA,
                                            sizeof(T_ATCMD_QUEUE_NODE) + param_len);
    if (!node)
    {
        printf("[atcmd] enqueue alloc fail\n");
        return false;
    }

    node->cmd = cmd;
    node->cb  = cb;
    memset(node->param, 0, param_len);
    if (param)
    {
        memcpy(node->param, param, param_len - 1);
    }

    os_queue_in(&s_atcmd_queue, node);
    return true;
}

void wifi_atcmd_rx_handler(void)
{
cmdbuf_read:
    {
        uint16_t recv = wifi_uart_rx_read(s_atcmd.rx_buf + s_atcmd.rx_cnt,
                                          ATCMD_RX_BUF_SIZE - s_atcmd.rx_cnt);
        s_atcmd.rx_cnt += recv;
    }

    if (s_atcmd.rx_cnt < MIN_RSP_LEN)
    {
        return;
    }

    uint16_t parser_ofs = 0;
    while (parser_ofs < s_atcmd.rx_cnt)
    {
        uint16_t start_ofs = s_atcmd.rx_cnt;
        uint16_t end_ofs   = 0;

        for (uint16_t i = parser_ofs; i < s_atcmd.rx_cnt; i++)
        {
            if (s_atcmd.rx_buf[i] != '\r' && s_atcmd.rx_buf[i] != '\n')
            {
                if (i < start_ofs)
                {
                    start_ofs = i;
                }
            }
            else if (start_ofs < s_atcmd.rx_cnt)
            {
                end_ofs = i;
                break;
            }
        }

        if (end_ofs)
        {
            uint16_t line_len = end_ofs - start_ofs;
            char *line = os_mem_alloc(OS_MEM_TYPE_DATA, line_len + 1);
            if (!line)
            {
                return;
            }
            memcpy(line, &s_atcmd.rx_buf[start_ofs], line_len);
            line[line_len] = '\0';
            bool prefix_matched = false;
            for (uint16_t i = 0; i < ATCMD_NUM; i++)
            {
                if (!memcmp(line, at_cmd_table[i].rsp_prefix,
                            strlen(at_cmd_table[i].rsp_prefix)))
                {
                    prefix_matched = true;
                    if (s_atcmd.cur_cmd == i)
                    {
                        app_stop_timer(&s_timer_handle);
                        if (at_cmd_table[i].rsp_func)
                        {
                            T_ATCMD_QUEUE_NODE *node = os_queue_peek(&s_atcmd_queue, 0);
                            T_ATCMD_RSP_CB cb = (node != NULL) ? node->cb : NULL;
                            at_cmd_table[i].rsp_func(line, cb);
                        }
                        s_atcmd.cur_cmd = ATCMD_NUM;
                        s_atcmd.rx_cnt  = 0;

                        T_ATCMD_QUEUE_NODE *done = os_queue_out(&s_atcmd_queue);
                        if (done)
                        {
                            os_mem_free(done);
                        }
                        os_mem_free(line);
                        atcmd_trigger_next();
                        return;
                    }
                    else if (at_cmd_table[i].rsp_func)
                    {
                        at_cmd_table[i].rsp_func(line, NULL);
                    }
                }
            }
            /* 命令执行期间收到的不匹配行（如扫描 AP 信息行）作为中间数据处理 */
            if (!prefix_matched && s_atcmd.cur_cmd != ATCMD_NUM)
            {
                T_ATCMD_QUEUE_NODE *node = os_queue_peek(&s_atcmd_queue, 0);
                if (node && node->cb)
                {
                    node->cb(s_atcmd.cur_cmd, line);
                }
            }

            parser_ofs = end_ofs;
            os_mem_free(line);
        }
        else if (start_ofs < s_atcmd.rx_cnt)
        {
            if (parser_ofs)
            {
                uint16_t remain = s_atcmd.rx_cnt - start_ofs;
                memmove(s_atcmd.rx_buf, &s_atcmd.rx_buf[start_ofs], remain);
                s_atcmd.rx_cnt = remain;
                if (remain + start_ofs == ATCMD_RX_BUF_SIZE)
                {
                    goto cmdbuf_read;
                }
            }
            if (s_atcmd.rx_cnt == ATCMD_RX_BUF_SIZE)
            {
                s_atcmd.rx_cnt = 0;
                goto cmdbuf_read;
            }
            break;
        }
        else
        {
            s_atcmd.rx_cnt = 0;
            break;
        }
    }
}

void wifi_atcmd_flow_ctrl_handler(void)
{
    if (s_atcmd.cur_cmd != ATCMD_NUM)
    {
        printf("[atcmd] flow ctrl: cmd %d still pending\n", s_atcmd.cur_cmd);
        return;
    }

    T_ATCMD_QUEUE_NODE *node = os_queue_peek(&s_atcmd_queue, 0);
    if (!node)
    {
        return;
    }
    if (node->cmd >= ATCMD_NUM)
    {
        printf("[atcmd] invalid queued cmd %d\n", node->cmd);
        T_ATCMD_QUEUE_NODE *bad = os_queue_out(&s_atcmd_queue);
        os_mem_free(bad);
        return;
    }

    const char *param = (node->param[0] != '\0') ? node->param : NULL;
    atcmd_send_raw(node->cmd, param);

    if (at_cmd_table[node->cmd].rsp_func)
    {
        s_atcmd.cur_cmd = node->cmd;
        s_atcmd.rx_cnt  = 0;
        app_start_timer(&s_timer_handle, "atcmd_t",
                        s_timer_module_id, 0, 0, false, ATCMD_TIMEOUT_MS);
    }
    else
    {
        s_atcmd.cur_cmd = ATCMD_NUM;
        T_ATCMD_QUEUE_NODE *done = os_queue_out(&s_atcmd_queue);
        if (done)
        {
            os_mem_free(done);
        }
        atcmd_trigger_next();
    }
}
