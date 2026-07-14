/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */
#ifndef __APP_KEY_BUTTON_H
#define __APP_KEY_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化板级物理按键（ADC_2 -> Power，P3_5 -> Home）。
 *
 * 将两个引脚配置为数字输入 + 电平中断 + 软件消抖，按下/松开时更新
 * GUI keyboard 输入设备（gui_kb_create）所订阅的状态与时间戳全局变量。
 */
void app_key_button_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_KEY_BUTTON_H */
