/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include <draw_img.h>
#include "gui_port.h"
#include "gui_api.h"


extern void sw_acc_init(void);
extern void sw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void hw_acc_init(void);
extern void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
static struct acc_engine acc =
{
    .blit = sw_acc_blit
};


void gui_port_acc_init(void)
{
#if 1
    hw_acc_init();
    acc.blit = hw_acc_blit;
#else
    sw_acc_init();
    acc.blit = sw_acc_blit;
#endif
    gui_acc_info_register(&acc);
}
