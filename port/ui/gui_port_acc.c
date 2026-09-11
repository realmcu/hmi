#include "gui_api.h"
#include "section.h"

#define GUI_PPE_ROI_STAGING_SIZE (40U * 1024U)

DSP_RAM_BSS_SECTION static uint8_t __attribute__((aligned(32)))
ppe_roi_staging_buffer[GUI_PPE_ROI_STAGING_SIZE];

extern void sw_acc_init(void);
extern void sw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void hw_acc_init(void);
extern void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);

extern void hw_jpeg_init(void);
extern void *hw_jpeg_load(void *input, int len, int *w, int *h, int *channel);
extern void hw_jpeg_free(void *ptr);
static struct acc_engine acc =
{
    .blit = hw_acc_blit,
    .jpeg_load = hw_jpeg_load,
    .jpeg_free = hw_jpeg_free,
    .enable_async = false,
    .hw_acc_cache_mem = ppe_roi_staging_buffer,
    .hw_acc_cache_size = sizeof(ppe_roi_staging_buffer),
};


void gui_port_acc_init(void)
{
    hw_acc_init();
    hw_jpeg_init();
    gui_acc_info_register(&acc);
}
