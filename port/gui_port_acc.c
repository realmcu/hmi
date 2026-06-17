/*
 * Copyright (c) 2026 Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * Licensed under the Realtek License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License from Realtek
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gui_port.h"
#include "gui_api.h"
#include "draw_img.h"
#include "ameba_soc.h"
#include "ameba_rcc.h"

// Forward declarations for software acceleration functions
extern void sw_acc_init(void);
extern void sw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void hw_acc_init(void);
extern void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void *hw_acc_idu_decode(void *input);

// Hardware JPEG decoder (acc_jpeg.c)
extern void *gui_hw_jpeg_load(void *input, int len, int *w, int *h, int *channel);
extern void gui_hw_jpeg_free(void *decode_image);
extern void hx170dec_init(void);

// Image decode function
extern void *gui_acc_decode(void *in);

static acc_engine_t acc =
{
    .blit = hw_acc_blit,
    .jpeg_load = gui_hw_jpeg_load,
    .jpeg_free = gui_hw_jpeg_free,
    .enable_async = false,
};

void gui_port_acc_init(void)
{
    //hw_acc_init();
    RCC_PeriphClockCmd(APBPeriph_PPE, APBPeriph_PPE_CLOCK, ENABLE);
    RCC_PeriphClockCmd(APBPeriph_MJPEG, APBPeriph_MJPEG_CLOCK, ENABLE);
    hx170dec_init();
    // sw_acc_init();
    gui_acc_info_register(&acc);
}

void gui_clean_cache(void)
{
    DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
}