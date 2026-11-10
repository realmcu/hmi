/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#include "gui_api.h"
#include "section.h"

#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_PPE)
extern gui_blit_ops_t hw_acc_blit_ops;
extern gui_idu_ops_t hw_acc_idu_ops;
#else
#include "acc_sw_entry.h"   /* sw_acc_init(), sw_acc_blit() */
extern void sw_acc_deinit(void);
static gui_blit_ops_t sw_acc_blit_ops =
{
    .init = sw_acc_init,
    .process = sw_acc_blit,
    .deinit = sw_acc_deinit,
};
#endif
#if defined(__ARM_FEATURE_MVE) && __ARM_FEATURE_MVE
extern gui_blur_ops_t mve_arm2d_blur_ops;
#else
extern gui_blur_ops_t sw_arm2d_blur_ops;
#endif
#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_PPE)
extern bool hw_acc_blit_scale(uint8_t *target_buffer, const uint8_t *source_buffer,
                              uint16_t source_width, uint16_t source_height,
                              uint16_t source_stride, uint16_t target_stride,
                              uint8_t factor, uint16_t target_width, uint16_t target_height,
                              uint8_t bytes_per_pixel, bool scale_up);
#endif
#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_JPU)
extern gui_jpeg_ops_t hw_jpeg_ops;
#endif

DSP_RAM_BSS_SECTION uint8_t cache[40 * 1024];

static struct acc_engine acc =
{
#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_PPE)
    .blit = &hw_acc_blit_ops,
#else
    .blit = &sw_acc_blit_ops,
#endif
#if defined(__ARM_FEATURE_MVE) && __ARM_FEATURE_MVE
    .blur = &mve_arm2d_blur_ops,
#else
    .blur = &sw_arm2d_blur_ops,
#endif
#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_JPU)
    .jpeg = &hw_jpeg_ops,
#endif
#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_IDU) && defined(CONFIG_REALTEK_DISPLAY_ENABLE_PPE)
    .idu = &hw_acc_idu_ops,
#endif
    .enable_thread_sync = true,
    .cache_mem = cache,
    .cache_size = sizeof(cache),
};


void gui_port_acc_init(void)
{
#if defined(CONFIG_REALTEK_DISPLAY_ENABLE_PPE)
    acc.blur->scale = hw_acc_blit_scale;
#endif
    gui_acc_info_register(&acc);
}

void gui_port_acc_deinit(void)
{
    gui_acc_info_register(NULL);
}
