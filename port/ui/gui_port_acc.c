#include "gui_api.h"

extern void sw_acc_init(void);
extern void sw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);
extern void hw_acc_init(void);
extern void hw_acc_blit(draw_img_t *image, struct gui_dispdev *dc, gui_rect_t *rect);

extern void hw_jpeg_init(void);
extern void *hw_jpeg_load(void *input, int len, int *w, int *h, int *channel);
extern void hw_jpeg_free(void *ptr);

extern void *hw_acc_idu_decode(void *input);
extern void hw_acc_idu_free(void *p);
static struct acc_engine acc =
{
    .blit = hw_acc_blit,
    .jpeg_load = hw_jpeg_load,
    .jpeg_free = hw_jpeg_free,
    .idu_load = hw_acc_idu_decode,
    .idu_free = hw_acc_idu_free,
    .enable_async = false
};


void gui_port_acc_init(void)
{
    hw_acc_init();
    hw_jpeg_init();
    gui_acc_info_register(&acc);
}
