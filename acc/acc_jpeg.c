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

/**
 * @file acc_jpeg.c
 * @brief Hardware JPEG decoder adapter for HoneyGUI on AmebaGreen2 platform.
 *
 * This module uses the AmebaGreen2 hardware JPEG decoder (JpegDec) combined
 * with the Post-Processor (PP) to decode JPEG images into RGB565 pixel data.
 * The output includes a gui_rgb_data_head_t header compatible with HoneyGUI's
 * draw_img pipeline.
 */

#include <string.h>
#include "gui_api.h"
#include "draw_img.h"
#include "ameba_soc.h"
#include "jpegdecapi.h"
#include "ppapi.h"

/* Translate JpegDec output format to PP input format.
 * The two APIs use different enum values for the same formats. */
static u32 jpegdec_fmt_to_pp_fmt(u32 jpegdec_fmt)
{
    switch (jpegdec_fmt)
    {
    case JPEGDEC_YCbCr400:
        return PP_PIX_FMT_YCBCR_4_0_0;
    case JPEGDEC_YCbCr420_SEMIPLANAR:
        return PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
    case JPEGDEC_YCbCr422_SEMIPLANAR:
        return PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR;
    case JPEGDEC_YCbCr440:
        return PP_PIX_FMT_YCBCR_4_4_0;
    case JPEGDEC_YCbCr411_SEMIPLANAR:
        return PP_PIX_FMT_YCBCR_4_1_1_SEMIPLANAR;
    case JPEGDEC_YCbCr444_SEMIPLANAR:
        return PP_PIX_FMT_YCBCR_4_4_4_SEMIPLANAR;
    default:
        return 0;
    }
}

void *gui_hw_jpeg_load(void *input, int len, int *w, int *h, int *channel)
{
    JpegDecInst jpeg_inst = NULL;
    PPInst pp_inst = NULL;
    JpegDecInput jpeg_in;
    JpegDecOutput jpeg_out;
    JpegDecImageInfo image_info;
    PPConfig pp_conf;
    void *output = NULL;
    int pixel_bytes;

    memset(&jpeg_in, 0, sizeof(jpeg_in));
    memset(&jpeg_out, 0, sizeof(jpeg_out));
    memset(&image_info, 0, sizeof(image_info));
    memset(&pp_conf, 0, sizeof(pp_conf));

    /* Step 1: Initialize hardware JPEG decoder */
    if (JpegDecInit(&jpeg_inst) != JPEGDEC_OK)
    {
        return NULL;
    }

    /* Step 2: Set up input stream */
    jpeg_in.streamBuffer.pVirtualAddress = (u32 *)input;
    jpeg_in.streamBuffer.busAddress = (u32)input;
    jpeg_in.streamLength = len;
    DCache_Clean((u32)input, len);

    /* Step 3: Get image info (width, height, output format) */
    {
        JpegDecRet info_ret = JpegDecGetImageInfo(jpeg_inst, &jpeg_in, &image_info);
        if (info_ret != JPEGDEC_OK)
        {
            goto release_jpeg;
        }
    }

    *w = (int)image_info.outputWidth;
    *h = (int)image_info.outputHeight;
    *channel = 3; /* RGB */

    /* Step 4: Initialize Post-Processor for YCbCr-to-RGB conversion */
    if (PPInit(&pp_inst) != PP_OK)
    {
        goto release_jpeg;
    }

    /* Step 5: Enable combined decode + PP pipeline */
    if (PPDecCombinedModeEnable(pp_inst, jpeg_inst, PP_PIPELINED_DEC_TYPE_JPEG) != PP_OK)
    {
        goto release_pp;
    }

    /* Step 6: Get PP default config and customize */
    if (PPGetConfig(pp_inst, &pp_conf) != PP_OK)
    {
        goto disable_combined;
    }

    /*
     * Output format: RGB565 (2 bytes per pixel).
     * PP does not support 24-bit RGB888 output on this platform,
     * so we use RGB565 which is memory-efficient and directly
     * compatible with typical embedded LCD displays.
     */
    pixel_bytes = 2;

    /*
     * Buffer layout (PP requires 64-byte aligned pixel buffer):
     *
     *   raw_ptr ──► [raw (4/8B)] [padding] [gui_rgb_data_head_t] [RGB565 pixels 64B-aligned]
     *                                      ↑ output (= head)      ↑ out_pixel_buf
     *
     * - Header sits right before pixels so draw_img's fixed offset
     *   (sizeof(head)) still reaches the pixel data correctly.
     * - Raw malloc pointer is stored before the header for gui_free().
     */
    #define PP_JPEG_ALIGN 64
    int head_size = sizeof(gui_rgb_data_head_t);
    int pixel_size = (*w) * (*h) * pixel_bytes;
    int ptr_size  = sizeof(void *);
    int total     = ptr_size + head_size + PP_JPEG_ALIGN + pixel_size;
    uint8_t *raw  = gui_malloc(total);
    if (!raw)
    {
        goto disable_combined;
    }

    /* Align pixel region to PP_JPEG_ALIGN, place header right before it */
    uint8_t *pixels = (uint8_t *)(((uintptr_t)(raw + ptr_size + head_size + PP_JPEG_ALIGN - 1))
                                   & ~(PP_JPEG_ALIGN - 1));
    gui_rgb_data_head_t *head = (gui_rgb_data_head_t *)(pixels - head_size);
    output = head;

    /* Stash raw allocation pointer for gui_hw_jpeg_free() */
    *(void **)((uint8_t *)head - ptr_size) = raw;

    /* Fill the header so HoneyGUI's draw_img can interpret the data */
    memset(head, 0, sizeof(gui_rgb_data_head_t));
    head->type = RGB565;
    head->w = (short)(*w);
    head->h = (short)(*h);
    head->compress = false;
    head->jpeg = true;

    /* Configure PP input (derived from JPEG decoder output) */
    u32 pp_in_fmt = jpegdec_fmt_to_pp_fmt(image_info.outputFormat);
    pp_conf.ppInImg.width = image_info.outputWidth;
    pp_conf.ppInImg.height = image_info.outputHeight;
    pp_conf.ppInImg.pixFormat = pp_in_fmt;
    pp_conf.ppInImg.videoRange = 1;

    /* Configure PP output (RGB565) */
    pp_conf.ppOutImg.width = image_info.outputWidth;
    pp_conf.ppOutImg.height = image_info.outputHeight;
    pp_conf.ppOutImg.pixFormat = PP_PIX_FMT_RGB16_5_6_5;
    pp_conf.ppOutImg.bufferBusAddr = (g1_addr_t)(uintptr_t)pixels;

    /* BT.709 color space conversion */
    pp_conf.ppOutRgb.rgbTransform = PP_YCBCR2RGB_TRANSFORM_BT_709;

    if (PPSetConfig(pp_inst, &pp_conf) != PP_OK)
    {
        goto free_output;
    }

    /* Step 7: Decode (HW combined pipeline: JpegDec → PP → RGB565) */
    DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    {
        JpegDecRet dec_ret = JpegDecDecode(jpeg_inst, &jpeg_in, &jpeg_out);
        if (dec_ret != JPEGDEC_FRAME_READY)
        {
            goto free_output;
        }
    }

    /* Step 8: Clean up and return */
    PPDecCombinedModeDisable(pp_inst, jpeg_inst);
    PPRelease(pp_inst);
    JpegDecRelease(jpeg_inst);

    return output;

free_output:
    gui_free(raw);
    output = NULL;
disable_combined:
    PPDecCombinedModeDisable(pp_inst, jpeg_inst);
release_pp:
    PPRelease(pp_inst);
release_jpeg:
    JpegDecRelease(jpeg_inst);
    return NULL;
}

void gui_hw_jpeg_free(void *decode_image)
{
    if (decode_image)
    {
        /* Recover the original allocation pointer stored before the header */
        void *raw = *(void **)((uint8_t *)decode_image - sizeof(void *));
        gui_free(raw);
    }
}
