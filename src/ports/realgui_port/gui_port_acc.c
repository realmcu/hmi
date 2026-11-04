/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "gui_api.h"
#include "section.h"

extern gui_blit_ops_t hw_acc_blit_ops;

#include "acc_sw_entry.h"   /* sw_acc_init(), sw_acc_blit() */
extern void sw_acc_deinit(void);
static gui_blit_ops_t sw_acc_blit_ops =
{
    .init = sw_acc_init,
    .process = sw_acc_blit,
    .deinit = sw_acc_deinit,
};

extern gui_blur_ops_t sw_arm2d_blur_ops;


static struct acc_engine acc =
{
    .blit = &hw_acc_blit_ops,
    .blur = &sw_arm2d_blur_ops,
    .enable_thread_sync = false,
};

void gui_port_acc_init(void)
{
    gui_acc_info_register(&acc);
}

void gui_port_acc_deinit(void)
{
    gui_acc_info_register(NULL);
}
