
#include <stdio.h>
#include <stdint.h>
#include "rtl_ppe.h"
#include <gui_matrix.h>
#include "math.h"
#include "def_file.h"
#include "draw_img.h"
#include "acc_sw_rle.h"

#ifndef BIT0
#define BIT0    0x0001
#endif
#ifndef BIT1
#define BIT1    0x0002
#endif
#ifndef BIT2
#define BIT2    0x0004
#endif
#ifndef BIT3
#define BIT3    0x0008
#endif
#ifndef BIT4
#define BIT4    0x0010
#endif
#include "ameba_soc.h"

#define PPE_DISP_ACC_MIN_OPA 0

static void decode_RLE_16bit(imdc_file_t* file, gui_rect_t* range, uint8_t* output)
{
    uint32_t stride = (range->x2 - range->x1 + 1) * 2;
    for(int i = range->y1; i <= range->y2; i++)
    {
        uncompressed_rle_rgb565(file, i, output);
        output += stride;
    }
}

static void decode_RLE_24bit(imdc_file_t* file, gui_rect_t* range, uint8_t* output)
{
    uint32_t stride = (range->x2 - range->x1 + 1) * 3;
    for(int i = range->y1; i <= range->y2; i++)
    {
        uncompressed_rle_rgb888(file, i, output);
        output += stride;
    }
}

static void decode_RLE_32bit(imdc_file_t* file, gui_rect_t* range, uint8_t* output)
{
    uint32_t stride = (range->x2 - range->x1 + 1) * 4;
    for(int i = range->y1; i <= range->y2; i++)
    {
        uncompressed_rle_argb8888(file, i, output);
        output += stride;
    }
}
#if 1
static void decode_RLE_16bit_rect(imdc_file_t* file, gui_rect_t* range, uint8_t* output, uint32_t stride)
{
    uint32_t width = (range->x2 - range->x1 + 1) * 2;
    uint16_t* middle = gui_malloc(file->header.raw_pic_width * 2);
    for(int i = range->y1; i <= range->y2; i++)
    {
        uncompressed_rle_rgb565(file, i, (uint8_t*)middle);
        memcpy(output, middle + range->x1, width);
        output += stride;
    }
    gui_free(middle);
}
#else
static void decode_RLE_16bit_rect(imdc_file_t* file, gui_rect_t* range, uint8_t* output, uint32_t stride)
{
    // uint32_t width = (range->x2 - range->x1 + 1) * 2;
    for(int i = range->y1; i <= range->y2; i++)
    {
        uint32_t start = (uint32_t)(uintptr_t)file + file->compressed_addr[i];
        uint32_t end = (uint32_t)(uintptr_t)file + file->compressed_addr[i + 1];
        uint16_t *linebuf = (uint16_t *)output;
        int16_t pixel_cnt = 0;
        // gui_log("file->compressed_addr[%d] %d\n", line, file->compressed_addr[line]);
        for (uint32_t addr = start; addr < end;)
        {
            imdc_rgb565_node_t *node = (imdc_rgb565_node_t *)(uintptr_t)addr;
            uint8_t node_len = node->len;
            int16_t pixel_cnt_end = pixel_cnt + node_len - 1;
            if(range->x1 <= pixel_cnt && range->x2 >= pixel_cnt_end)
            {
                gui_memset16(linebuf, node->pixel16, node_len);
                linebuf = linebuf + node_len;
            }    
            else if (range->x1 >= pixel_cnt && range->x1 <= pixel_cnt_end)
            {
                uint8_t skip_pixel = range->x1 - pixel_cnt;
                node_len -= skip_pixel;
                linebuf += skip_pixel;
                gui_memset16(linebuf, node->pixel16, node->len);
                linebuf = linebuf + node_len;
            }
            else if (range->x2 >= pixel_cnt && range->x2 <= pixel_cnt_end)
            {
                uint8_t skip_pixel = pixel_cnt_end - range->x2;
                node_len -= skip_pixel;
                gui_memset16(linebuf, node->pixel16, node_len);
                linebuf = linebuf + node->len;
            }
            else
            {
                linebuf = linebuf + node_len;
            }
            // gui_log("%d 0x%x\n", node->len, node->pixel16);
            addr = addr + sizeof(imdc_rgb565_node_t);
            pixel_cnt = pixel_cnt_end + 1;
            
        }
        output += stride;
    }
}
#endif

static void decode_RLE_24bit_rect(imdc_file_t* file, gui_rect_t* range, uint8_t* output, uint32_t stride)
{
    // uint32_t stride = (range->x2 - range->x1 + 1) * 3;
    for(int i = range->y1; i <= range->y2; i++)
    {
        uint32_t start = (uint32_t)(uintptr_t)file + file->compressed_addr[i];
        uint32_t end = (uint32_t)(uintptr_t)file + file->compressed_addr[i + 1];
        uint16_t *linebuf = (uint16_t *)output;
        int16_t pixel_cnt = 0;
        // gui_log("file->compressed_addr[%d] %d\n", line, file->compressed_addr[line]);
        for (uint32_t addr = start; addr < end;)
        {
            imdc_rgb565_node_t *node = (imdc_rgb565_node_t *)(uintptr_t)addr;
            uint8_t node_len = node->len;
            int16_t pixel_cnt_end = pixel_cnt + node_len - 1;
            if(range->x1 <= pixel_cnt && range->x2 >= pixel_cnt_end)
            {
                ;
            }    
            else if (range->x1 >= pixel_cnt && range->x1 <= pixel_cnt_end)
            {
                uint8_t skip_pixel = range->x1 - pixel_cnt;
                node_len -= skip_pixel;
                linebuf += skip_pixel;
            }
            else if (range->x2 >= pixel_cnt && range->x2 <= pixel_cnt_end)
            {
                uint8_t skip_pixel = pixel_cnt_end;
                node_len -= skip_pixel;

            }
            // gui_log("%d 0x%x\n", node->len, node->pixel16);
            gui_memset16(linebuf, node->pixel16, node->len);

            addr = addr + sizeof(imdc_rgb565_node_t);
            linebuf = linebuf + node->len;
            pixel_cnt = pixel_cnt_end + 1;
        }
        output += stride;
    }
}

static void decode_RLE_32bit_rect(imdc_file_t* file, gui_rect_t* range, uint8_t* output, uint32_t stride)
{
    // uint32_t stride = (range->x2 - range->x1 + 1) * 4;
    for(int i = range->y1; i <= range->y2; i++)
    {
        uint32_t start = (uint32_t)(uintptr_t)file + file->compressed_addr[i];
        uint32_t end = (uint32_t)(uintptr_t)file + file->compressed_addr[i + 1];
        uint16_t *linebuf = (uint16_t *)output;
        int16_t pixel_cnt = 0;
        // gui_log("file->compressed_addr[%d] %d\n", line, file->compressed_addr[line]);
        for (uint32_t addr = start; addr < end;)
        {
            imdc_rgb565_node_t *node = (imdc_rgb565_node_t *)(uintptr_t)addr;
            uint8_t node_len = node->len;
            int16_t pixel_cnt_end = pixel_cnt + node_len - 1;
            if(range->x1 <= pixel_cnt && range->x2 >= pixel_cnt_end)
            {
                ;
            }    
            else if (range->x1 >= pixel_cnt && range->x1 <= pixel_cnt_end)
            {
                uint8_t skip_pixel = range->x1 - pixel_cnt;
                node_len -= skip_pixel;
                linebuf += skip_pixel;
            }
            else if (range->x2 >= pixel_cnt && range->x2 <= pixel_cnt_end)
            {
                uint8_t skip_pixel = pixel_cnt_end;
                node_len -= skip_pixel;

            }
            // gui_log("%d 0x%x\n", node->len, node->pixel16);
            gui_memset16(linebuf, node->pixel16, node->len);

            addr = addr + sizeof(imdc_rgb565_node_t);
            linebuf = linebuf + node->len;
        }
        output += stride;
    }
}

static void get_area(gui_matrix_t *mat, gui_rect_t *rect, gui_rect_t *result)
{
    gui_point3f_t pox = {.x = 0.0f, .y = 0.0f, .z = 1.0f};
    float x_min = 0.0f;
    float x_max = 0.0f;
    float y_min = 0.0f;
    float y_max = 0.0f;

    float x1 = rect->x1;
    float y1 = rect->y1;
    float x2 = rect->x2;
    float y2 = rect->y2;

    pox.x = x1;
    pox.y = y1;
    pox.z = 1.0f;
    matrix_multiply_point(mat, &pox);
    x_min = pox.x;
    x_max = pox.x;
    y_min = pox.y;
    y_max = pox.y;

    pox.x = x2;
    pox.y = y1;
    pox.z = 1.0f;
    matrix_multiply_point(mat, &pox);
    if (x_min > pox.x)
    {
        x_min = pox.x;
    }
    if (x_max < pox.x)
    {
        x_max = pox.x;
    }
    if (y_min > pox.y)
    {
        y_min = pox.y;
    }
    if (y_max < pox.y)
    {
        y_max = pox.y;
    }

    pox.x = x2;
    pox.y = y2;
    pox.z = 1.0f;
    matrix_multiply_point(mat, &pox);
    if (x_min > pox.x)
    {
        x_min = pox.x;
    }
    if (x_max < pox.x)
    {
        x_max = pox.x;
    }
    if (y_min > pox.y)
    {
        y_min = pox.y;
    }
    if (y_max < pox.y)
    {
        y_max = pox.y;
    }

    pox.x = x1;
    pox.y = y2;
    pox.z = 1.0f;
    matrix_multiply_point(mat, &pox);
    if (x_min > pox.x)
    {
        x_min = pox.x;
    }
    if (x_max < pox.x)
    {
        x_max = pox.x;
    }
    if (y_min > pox.y)
    {
        y_min = pox.y;
    }
    if (y_max < pox.y)
    {
        y_max = pox.y;
    }
    result->x1 = x_min;
    result->y1 = y_min;
    result->x2 = x_max;
    result->y2 = y_max;
}

void hw_acc_clear(uint8_t *addr, gui_color_t color, uint32_t len/*pixel count*/)
{
    (void)len;
    (void)addr;
    ppe_buffer_t target;
    gui_dispdev_t *dc = gui_get_dc();
    memset(&target, 0, sizeof(ppe_buffer_t));
    target.address = (uint32_t)addr;
    target.width = dc->fb_width;
    target.height = dc->fb_height;
    target.stride = dc->fb_width;
    switch (dc->bit_depth)
    {
    case 16:
        target.format = PPE_DISP_RGB565;
        break;
    case 24:
        target.format = PPE_DISP_RGB888;
        break;
    case 32:
        target.format = PPE_DISP_ARGB8888;
        break;
    default:
        gui_log("return here\n");
        return;
    }
    target.const_color = 0xFF000000;
    target.win_x_min = 0;
    target.win_x_max = target.width - 1;
    target.win_y_min = 0;
    target.win_y_max = target.height - 1;
    ppe_rect_t rect1 = {.x = 0, .y = 0, .w = target.width, .h = target.height};
    uint32_t ppe_color = color.color.rgba.a << 24 | color.color.rgba.b << 16 | color.color.rgba.g << 8 | color.color.rgba.r << 8;
    PPE_DISP_err err = PPE_DISP_Mask(&target, ppe_color, &rect1);
    if(err != PPE_DISP_SUCCESS)
    {
        gui_log("return %d err %d\n", __LINE__, err);
        return;
    }
}

void hw_ppe_read_test(struct gui_dispdev *dc)
{
    ppe_buffer_t target, source;
    memset(&target, 0, sizeof(ppe_buffer_t));
    memset(&source, 0, sizeof(ppe_buffer_t));
    target.address = (uint32_t)dc->frame_buf;
    target.width = dc->fb_width;
    target.height = dc->fb_height;
    target.stride = dc->fb_width;
    switch (dc->bit_depth)
    {
    case 16:
        target.format = PPE_DISP_RGB565;
        break;
    case 24:
        target.format = PPE_DISP_RGB888;
        break;
    case 32:
        target.format = PPE_DISP_ARGB8888;
        break;
    default:
        gui_log("return here\n");
        return;
    }
    target.const_color = 0xFF000000;
    target.win_x_min = 0;
    target.win_x_max = target.width - 1;
    target.win_y_min = 0;
    target.win_y_max = target.height - 1;

    source.opacity = 0xFF;
    source.stride = target.width;
    source.const_color = 0xFFFFFFFF;
    source.format = target.format;
    source.win_x_min = 0;
    source.win_x_max = target.width - 1;
    source.win_y_min = 0;
    source.win_y_max = target.height - 1;
    source.width = target.width;
    source.height = target.height;
    source.address = (uint32_t)0x60000020;
    source.high_quality = false;
    ppe_rect_t rect1 = {.x = 0, .y = 0, .w = dc->fb_width, .h = dc->fb_height};
    ppe_matrix_t inv;
    ppe_get_identity(&inv);
    PPE_DISP_err err = PPE_DISP_Blit_Inverse(&target, &source, &inv, &rect1, PPE_DISP_BYPASS_MODE);
    PPE_DISP_Finish();
    if(err != PPE_DISP_SUCCESS)
    {
        gui_log("return %d err %d\n", __LINE__, err);
        return;
    }
}

extern void sw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
void hw_acc_blit_decode(draw_img_t *image, struct gui_dispdev *dc, struct gui_rect *rect)
{
    if (image->opacity_value <= PPE_DISP_ACC_MIN_OPA)
    {
        return;
    }
    gui_rect_t draw_area = { .x2 = (image->img_target_w + image->img_target_x - 1),
     .y2 = (image->img_target_h + image->img_target_y - 1),
     .x1 = image->img_target_x,
     .y1 = image->img_target_y};
    gui_rect_t constraint_area;
    if(!rect_intersect(&constraint_area, &dc->section, &draw_area))
    {
        return;
    }
    ppe_buffer_t target, source;
    memset(&target, 0, sizeof(ppe_buffer_t));
    memset(&source, 0, sizeof(ppe_buffer_t));
    ppe_matrix_t inverse;
    memcpy(&inverse, &image->inverse, sizeof(float) * 9);
    gui_rect_t decode_area;
    get_area(&image->inverse, &constraint_area, &decode_area);
    gui_rect_t image_area = {.x1 = 0, .x2 = image->img_w - 1,
                                .y1 = 0, .y2 = image->img_h - 1};
    if(!rect_intersect(&decode_area, &decode_area, &image_area))
    {
        gui_log("return %d\n", __LINE__);
        return;
    }
    if(rect != NULL)
    {
        if(!rect_intersect(&decode_area, &decode_area, &image_area))
        {
            gui_log("return %d\n", __LINE__);
            return;
        }
    }
    decode_area.x1 = 0;
    decode_area.x2 = image->img_w - 1;
    PPE_DISP_BLEND_MODE mode = PPE_DISP_SRC_OVER_MODE;

    switch (dc->bit_depth)
    {
    case 16:
        target.format = PPE_DISP_RGB565;
        break;
    case 24:
        target.format = PPE_DISP_RGB888;
        break;
    case 32:
        target.format = PPE_DISP_ARGB8888;
        break;
    default:
        return;
    }
    target.address = (uint32_t)dc->frame_buf;
    target.width = dc->fb_width;
    target.height = dc->fb_height;
    target.stride = dc->fb_width;
    target.const_color = 0;
    target.win_x_min = 0;
    target.win_x_max = target.width - 1;
    target.win_y_min = 0;
    target.win_y_max = target.height - 1;

    source.opacity = image->opacity_value;
    source.stride = image->img_w;
    source.const_color = 0xFFFFFFFF;
    struct gui_rgb_data_head *head = image->data;
    switch (head->type)
    {
    case RGB565:
        source.format = PPE_DISP_RGB565;
        break;
    case RGB888:
        source.format = PPE_DISP_RGB888;
        break;
    case ARGB8888:
        source.format = PPE_DISP_ARGB8888;
        break;
    case ARGB8565:
        source.format = PPE_DISP_ARGB8565;
        break;
    case XRGB8888:
        source.format = PPE_DISP_XRGB8888;
        break;
    default:
        return;
    }
    source.win_x_min = 0;
    source.win_x_max = target.width - 1;
    source.win_y_min = 0;
    source.win_y_max = target.height - 1;
    source.width = image->img_w;
    source.height = decode_area.y2 - decode_area.y1 + 1;
    uint32_t decode_size = source.width * source.height * PPE_DISP_Get_Pixel_Size(source.format);
    uint8_t* decoded_data = gui_malloc(decode_size);
    source.address = (uint32_t)decoded_data;
    if(PPE_DISP_Get_Pixel_Size(source.format) == 2)
    {
        decode_RLE_16bit((imdc_file_t*)((uintptr_t)image->data + 8), &decode_area, decoded_data);
    }
    else if(PPE_DISP_Get_Pixel_Size(source.format) == 3)
    {
        decode_RLE_24bit((imdc_file_t*)((uintptr_t)image->data + 8), &decode_area, decoded_data);
    }
    else if(PPE_DISP_Get_Pixel_Size(source.format) == 4)
    {
        decode_RLE_32bit((imdc_file_t*)((uintptr_t)image->data + 8), &decode_area, decoded_data);
    }
    else
    {
        gui_free(decoded_data);
        return;
    };
    if(decode_size> 16384)
    {
        DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    }
    else
    {
        DCache_CleanInvalidate((uint32_t)decoded_data, decode_size);
    }

    extern void gui_clean_cache(void);
    gui_clean_cache();
    ppe_matrix_t pre_trans;
    ppe_get_identity(&pre_trans);
    if(rect != NULL)
    {
        source.address = (uint32_t)source.address + sizeof(struct gui_rgb_data_head) + rect->x1 * PPE_DISP_Get_Pixel_Size(source.format);
        source.width = rect->x2 - rect->x1 + 1;
        source.height = decode_area.y2 - decode_area.y1 + 1;
        pre_trans.m[0][2] = rect->x1 * -1.0f;
    }
    pre_trans.m[1][2] = decode_area.y1 * -1.0f;
    ppe_mat_multiply(&pre_trans, &inverse);
    ppe_translate(0, dc->section.y1, &pre_trans);

    ppe_rect_t constraint = (ppe_rect_t) {.x = constraint_area.x1 - dc->section.x1, 
                                            .y = constraint_area.y1 - dc->section.y1, 
                                            .w = constraint_area.x2 - constraint_area.x1 + 1, 
                                            .h = constraint_area.y2 - constraint_area.y1 + 1};

    source.high_quality = true;
    if (image->blend_mode == IMG_FILTER_BLACK)
    {
        source.color_key_enable = PPE_DISP_COLOR_KEY_INSIDE;
        source.color_key_min = 0;
        source.color_key_max = 0;
    }
    if (!ppe_matrix_is_complex(&inverse))
    {
        source.high_quality = false;
        if ((source.format == PPE_DISP_RGB565 || source.format == PPE_DISP_RGB888) 
            && image->blend_mode != IMG_FILTER_BLACK)
        {
            mode = PPE_DISP_BYPASS_MODE;
        }
    }
    PPE_DISP_Finish();
    PPE_DISP_err err = PPE_DISP_Blit_Inverse(&target, &source, &pre_trans, &constraint,
                                   mode);
    if(err != PPE_DISP_SUCCESS)
    {
        gui_free(decoded_data);
        return;
    }
    if (!gui_get_acc()->enable_async)
    {
        PPE_DISP_Finish();
    }
    gui_free(decoded_data);
    return;
}

void hw_acc_blit_direct(draw_img_t *image, struct gui_dispdev *dc, struct gui_rect *rect)
{
    int32_t x_max = (image->img_target_w + image->img_target_x - 1);
    int32_t y_max = (image->img_target_h + image->img_target_y - 1);
    int32_t x_min = image->img_target_x;
    int32_t y_min = image->img_target_y;
    if (dc->section.y2 < y_min || dc->section.y1 > y_max || dc->section.x2 < x_min ||
        dc->section.x1 > x_max)
    {
        return;
    }
    if (image->opacity_value <= PPE_DISP_ACC_MIN_OPA)
    {
        return;
    }
    ppe_buffer_t target, source;
    memset(&target, 0, sizeof(ppe_buffer_t));
    memset(&source, 0, sizeof(ppe_buffer_t));

    ppe_matrix_t inverse;
    memcpy(&inverse, &image->inverse, sizeof(float) * 9);
    PPE_DISP_BLEND_MODE mode = PPE_DISP_SRC_OVER_MODE;

    switch (dc->bit_depth)
    {
    case 16:
        target.format = PPE_DISP_RGB565;
        break;
    case 24:
        target.format = PPE_DISP_RGB888;
        break;
    case 32:
        target.format = PPE_DISP_ARGB8888;
        break;
    default:
        return;
    }
    target.address = (uint32_t)dc->frame_buf;
    target.width = dc->fb_width;
    target.height = dc->fb_height;
    target.stride = dc->fb_width;
    target.const_color = 0;
    target.win_x_min = 0;
    target.win_x_max = target.width - 1;
    target.win_y_min = 0;
    target.win_y_max = target.height - 1;

    source.opacity = image->opacity_value;
    source.stride = image->img_w;
    source.const_color = 0xFFFFFFFF;
    struct gui_rgb_data_head *head = image->data;
    switch (head->type)
    {
    case RGB565:
        source.format = PPE_DISP_RGB565;
        break;
    case RGB888:
        source.format = PPE_DISP_RGB888;
        break;
    case ARGB8888:
        source.format = PPE_DISP_ARGB8888;
        break;
    case ARGB8565:
        source.format = PPE_DISP_ARGB8565;
        break;
    case XRGB8888:
        source.format = PPE_DISP_XRGB8888;
        break;
    default:
        return;
    }
    source.win_x_min = 0;
    source.win_x_max = target.width - 1;
    source.win_y_min = 0;
    source.win_y_max = target.height - 1;
    if(rect != NULL)
    {
        source.address = (uint32_t)image->data + sizeof(struct gui_rgb_data_head) + (rect->y1 * source.stride + rect->x1) * PPE_DISP_Get_Pixel_Size(source.format);
        source.width = rect->x2 - rect->x1 + 1;
        source.height = rect->y2 - rect->y1 + 1;
        ppe_matrix_t pre_trans;
        ppe_get_identity(&pre_trans);
        pre_trans.m[0][2] = rect->x1 * -1.0f;
        pre_trans.m[1][2] = rect->y1 * -1.0f;
        ppe_mat_multiply(&pre_trans, &inverse);
        memcpy(&inverse, &pre_trans, sizeof(float) * 9);
    }
    else
    {
        source.address = (uint32_t)image->data + sizeof(struct gui_rgb_data_head);
        source.width = image->img_w;
        source.height = image->img_h;
    }
    int32_t x1 = x_min - dc->section.x1;
    int32_t y1 = y_min - dc->section.y1;
    int32_t x2 = dc->fb_width - 1;
    int32_t y2 = dc->fb_height - 1;
    if (x1 < 0)
    {
        x1 = 0;
    }
    if (y1 < 0)
    {
        y1 = 0;
    }
    if (x_max >= dc->section.x1 + dc->fb_width)
    {
        x2 = dc->section.x1 + dc->fb_width - 1;
    }
    else
    {
        x2 = x_max - dc->section.x1;
    }
    if (y_max >= dc->section.y1 + dc->fb_height)
    {
        y2 = dc->fb_height - 1;
    }
    else
    {
        y2 = y_max - dc->section.y1;
    }
    ppe_rect_t constraint = (ppe_rect_t) {.x = x1, .y = y1, .w = x2 - x1 + 1, .h = y2 - y1 + 1};

    ppe_translate(0, dc->section.y1, &inverse);
    source.high_quality = true;
    if (image->blend_mode == IMG_FILTER_BLACK)
    {
        source.color_key_enable = PPE_DISP_COLOR_KEY_INSIDE;
        source.color_key_min = 0;
        source.color_key_max = 0;
    }
    if (!ppe_matrix_is_complex(&inverse))
    {
        source.high_quality = false;
        if ((source.format == PPE_DISP_RGB565 || source.format == PPE_DISP_RGB888) \
            && image->blend_mode != IMG_FILTER_BLACK)
        {
            mode = PPE_DISP_BYPASS_MODE;
        }
    }
    PPE_DISP_Finish();
    DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    //uint32_t time1 = rtos_time_get_current_system_time_us();
    PPE_DISP_err err = PPE_DISP_Blit_Inverse(&target, &source, &inverse, &constraint,
                                   mode);
    PPE_DISP_Finish();
    if(err != PPE_DISP_SUCCESS)
    {
        return;
    }
    if (!gui_get_acc()->enable_async)
    {
        PPE_DISP_Finish();
    }
    //uint32_t time2 = rtos_time_get_current_system_time_us();
    //gui_log("PPE blend src %x tgt %x time = %d us \n", source.address, target.address, time2 - time1);
    return;
}
void hw_acc_blit_cover(draw_img_t *image, struct gui_dispdev *dc, struct gui_rect *rect)
{
    gui_rect_t draw_area = { .x2 = (image->img_target_w + image->img_target_x - 1),
     .y2 = (image->img_target_h + image->img_target_y - 1),
     .x1 = image->img_target_x,
     .y1 = image->img_target_y};
    gui_rect_t image_area = {.x1 = 0, .y1 = 0, .x2 = image->img_w - 1, .y2 = image->img_h - 1};
    if(!rect_intersect(&draw_area, &draw_area, &dc->section))
    {
        return;
    }
    gui_rect_t image_source_area;
    rect_move(&image_source_area, &draw_area, -image->img_target_x, -image->img_target_y);
    if(!rect_intersect(&image_source_area, &image_area, &image_source_area))
    {
        return;
    }
    if(rect != NULL)
    {
        if(!rect_intersect(&image_source_area, rect, &image_source_area))
        {
            return;
        }
    }
    uint8_t dc_byte_depth = (dc->bit_depth >> 3);
    uint32_t x_offset = image_source_area.x1 + image->img_target_x - dc->section.x1;
    uint32_t y_offset = image_source_area.y1 + image->img_target_y - dc->section.y1;
    uint8_t* p_target_offset = dc->frame_buf + (y_offset * dc->fb_width + x_offset) * dc_byte_depth;
    if(dc_byte_depth == 2)
    {
        decode_RLE_16bit_rect((imdc_file_t*)((uintptr_t)image->data + 8), &image_source_area, p_target_offset, dc->fb_width * 2);
    }
    else if(dc_byte_depth == 3)
    {
        decode_RLE_24bit_rect((imdc_file_t*)((uintptr_t)image->data + 8), &image_source_area, p_target_offset, dc->fb_width * 2);
    }
    else if(dc_byte_depth == 4)
    {
        decode_RLE_32bit_rect((imdc_file_t*)((uintptr_t)image->data + 8), &image_source_area, p_target_offset, dc->fb_width * 2);
    }
    uint32_t fb_size = dc->fb_width * dc->fb_height * dc_byte_depth;
    if(fb_size > 16384)
    {
        DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    }
    else
    {
        DCache_CleanInvalidate((uint32_t)dc->frame_buf, fb_size);
    }
}

void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, struct gui_rect *rect)
{
    gui_rgb_data_head_t *head = (gui_rgb_data_head_t*)image->data;
    if(image->inverse.m[0][1] != 0 || image->inverse.m[1][0] != 0)
    {
        gui_rect_t draw_area = { .x2 = (image->img_target_w + image->img_target_x - 1),
                .y2 = (image->img_target_h + image->img_target_y - 1),
                .x1 = image->img_target_x,
                .y1 = image->img_target_y};
        if(rect_intersect(&draw_area, &draw_area, &dc->section))
        {
            sw_acc_blit(image,dc,rect);
            DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
        }
        return;
    }
    else if (image->blend_mode != IMG_FILTER_BLACK && \
        image->matrix.m[0][0] == 1 && image->matrix.m[1][1] == 1 && \
        ((dc->bit_depth == 16 && head->type == RGB565) || \
         (dc->bit_depth == 24 && head->type == RGB888) || \
         (dc->bit_depth == 32 && head->type == ARGB8888 && image->blend_mode == PPE_DISP_BYPASS_MODE)) && \
        image->opacity_value == 0xFF && head->compress)
    {
        hw_acc_blit_cover(image, dc, rect);
    }
    else
    {
        if(head->compress)
        {
            hw_acc_blit_decode(image, dc, rect);
        }
        else
        {
            hw_acc_blit_direct(image, dc, rect);
        }
    }
}

void *hw_acc_idu_decode(void *input)
{
    if (input == NULL)
    {
        GUI_ASSERT(input != NULL);
        return NULL;
    }
    uint8_t pixel_size = 0;
    const gui_rgb_data_head_t *head = (gui_rgb_data_head_t *)input;
    switch (head->type)
    {
    case RGB565:
        pixel_size = 2;
        break;
    case RGB888:
        pixel_size = 3;
        break;
    case ARGB8888:
        pixel_size = 4;
        break;
    case ARGB8565:
        pixel_size = 3;
        break;
    default:
        return NULL;
    }
    imdc_file_t *idu_info = (imdc_file_t *)((uint32_t)input + sizeof(
                                                              gui_rgb_data_head_t));
    if (idu_info->header.raw_pic_width == 0 || idu_info->header.raw_pic_height == 0)
    {
        return NULL;
    }
    uint32_t decoded_size = sizeof(gui_rgb_data_head_t) + 4 + idu_info->header.raw_pic_width
                                              * idu_info->header.raw_pic_height * pixel_size + 8;
    uint8_t *decode_img_data = gui_malloc(decoded_size);
    gui_rgb_data_head_t *output_header = (gui_rgb_data_head_t *)decode_img_data;
    memcpy(output_header, head, sizeof(gui_rgb_data_head_t));
    output_header->compress = 0;
    output_header->idu = 1;

    gui_rect_t decode_rect = {.x1 = 0, .y1 = 0, .x2 = head->w - 1, .y2 = head->h - 1};
    if(pixel_size == 2)
    {
        decode_RLE_16bit(idu_info, &decode_rect, decode_img_data + sizeof(gui_rgb_data_head_t));
    }
    else if(pixel_size == 3)
    {
        decode_RLE_24bit(idu_info, &decode_rect, decode_img_data + sizeof(gui_rgb_data_head_t));
    }
    else if(pixel_size == 4)
    {
        decode_RLE_32bit(idu_info, &decode_rect, decode_img_data + sizeof(gui_rgb_data_head_t));
    }
    if(decoded_size> 16384)
    {
        DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
    }
    else
    {
        DCache_CleanInvalidate((uint32_t)decode_img_data, decoded_size);
    }
    return decode_img_data;
}

void hw_acc_init(void)
{
    //PPE_DISP clock init
}
