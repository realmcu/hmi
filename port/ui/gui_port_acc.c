/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <draw_img.h>
#include "gui_api.h"


extern void sw_acc_init(void);
extern void sw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void sw_acc_deinit(void);

extern void sw_arm_2d_blur(uint8_t *buffer, uint16_t buffer_stride, uint16_t bit_depth,
                           const gui_rect_t *buffer_rect, const gui_rect_t *valid_rect,
                           const gui_rect_t *target_rect, uint8_t blur_degree, void *cache_mem);
extern void sw_arm_2d_create(void **mem);
extern void sw_arm_2d_depose(void **mem);

extern void *gui_acc_decode(void *in);

extern void hw_acc_init(void);
extern void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void hw_acc_deinit(void);

extern void hw_jpeg_init(void);
extern void *hw_jpeg_load(void *input, int len, int *w, int *h, int *channel);
extern void hw_jpeg_free(void *ptr);

extern void *hw_acc_idu_decode(void *input);
extern void hw_acc_idu_free(void *p);

extern gui_jpeg_ops_t hw_jpeg_ops;
extern gui_idu_ops_t hw_acc_idu_ops;
extern gui_blit_ops_t hw_acc_blit_ops;
extern gui_blur_ops_t mve_arm2d_blur_ops;

// static gui_blit_ops_t sw_acc_blit_ops =
// {
//     .init = sw_acc_init,
//     .process = sw_acc_blit,
//     .deinit = sw_acc_deinit,
// };

static acc_engine_t acc =
{
    .blit = &hw_acc_blit_ops,
    .blur = &mve_arm2d_blur_ops,
    .idu = &hw_acc_idu_ops,
    .jpeg = &hw_jpeg_ops,
};

void gui_port_acc_init(void)
{
    gui_acc_info_register(&acc);
}

void gui_port_acc_deinit(void)
{
    gui_acc_info_register(NULL);
}
