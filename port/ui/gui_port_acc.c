#include "gui_api.h"
#include "section.h"

#define GUI_PPE_ROI_STAGING_SIZE (40U * 1024U)

DSP_RAM_BSS_SECTION static uint8_t __attribute__((aligned(32)))
ppe_roi_staging_buffer[GUI_PPE_ROI_STAGING_SIZE];

extern void hw_acc_init(void);
extern void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, struct gui_rect *rect);
extern void *hw_acc_idu_decode(void *input);
extern void hw_acc_idu_free(void *ptr);
extern void hw_jpeg_init(void);
extern void *hw_jpeg_load(void *input, int len, int *w, int *h, int *channel);
extern void hw_jpeg_free(void *ptr);
extern gui_blur_ops_t sw_arm2d_blur_ops;

static const gui_blit_ops_t blit_ops =
{
    .init = hw_acc_init,
    .process = hw_acc_blit,
};
static const gui_jpeg_ops_t jpeg_ops =
{
    .init = hw_jpeg_init,
    .load = hw_jpeg_load,
    .release = hw_jpeg_free,
};
static const gui_idu_ops_t idu_ops =
{
    .load = hw_acc_idu_decode,
    .release = hw_acc_idu_free,
};
static struct acc_engine acc =
{
    .blit = &blit_ops,
    .blur = &sw_arm2d_blur_ops,
    .jpeg = &jpeg_ops,
    .idu = &idu_ops,
    .enable_thread_sync = false,
    .cache_mem = ppe_roi_staging_buffer,
    .cache_size = sizeof(ppe_roi_staging_buffer),
};

void gui_port_acc_init(void)
{
    gui_acc_info_register(&acc);
}
