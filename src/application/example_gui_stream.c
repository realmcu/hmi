/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include "guidef.h"
#include "gui_obj.h"
#include "gui_api.h"
#include "gui_components_init.h"
#include "gui_obj_event.h"
#include "gui_stream.h"

/*============================================================================*
 *                            Demo configuration
 *============================================================================*/

#define DEMO_SCREEN_H   360u    /* matches DRV_LCD_HEIGHT in the SConscript */
#define DEMO_GAP        8        /* horizontal gap between the two videos    */

#define MAX_FRAME       (50u * 1024u)   /* per-buffer cap (>> any real frame) */
#define POOL_BUFS       4u              /* FIFO depth per stream              */

/*============================================================================*
 *                           Per-stream runtime
 *============================================================================*/

typedef struct
{
    stp_transport_t *tp;
    uint8_t         *pool;
    uint32_t         pool_size;
    uint32_t         interval_ms;
    const char      *label;
    volatile bool    running;
} demo_stream_t;

demo_stream_t s_stream_bt;

#if 0

/*============================================================================*
 *                            Event callbacks
 *============================================================================*/

static void stream_click_cb(void *obj, gui_event_t *e)
{
    GUI_UNUSED(e);
    gui_stream_t *st = (gui_stream_t *)obj;

    if (gui_stream_get_state(st) == GUI_VIDEO_STATE_PLAYING)
    {
        gui_stream_set_state(st, GUI_VIDEO_STATE_PAUSE);
    }
    else
    {
        gui_stream_set_state(st, GUI_VIDEO_STATE_PLAYING);
    }
}

#endif
/*============================================================================*
 *                          Application entry point
 *============================================================================*/

static int app_init(void)
{
    gui_log("GUI Stream Widget Example Start\n");
#if 0
    int16_t x = (int16_t)50;
    int16_t y = (int16_t)50;
    gui_stream_codec_t codec = GUI_STREAM_CODEC_MSV1;

    gui_stream_t *st = gui_stream_create(gui_obj_get_root(), NULL, codec, s_stream_bt.tp,
                                         x, y,
                                         (int16_t)240, (int16_t)240);
#else
    int16_t x = (int16_t)0;
    int16_t y = (int16_t)0;
    // gui_stream_codec_t codec = GUI_STREAM_CODEC_MSV1;
    gui_stream_codec_t codec = GUI_STREAM_CODEC_JPEG;

    gui_stream_t *st = gui_stream_create(gui_obj_get_root(), NULL, codec, s_stream_bt.tp,
                                         x, y,
                                         (int16_t)360, (int16_t)360);
#endif


    if (!st)
    {
        gui_log("stream demo:  gui_stream_create failed\n");
        stp_destroy(s_stream_bt.tp);
        s_stream_bt.tp = NULL;
        return NULL;
    }

    gui_stream_set_update_interval(st, s_stream_bt.interval_ms);
    gui_stream_set_drop_mode(st, GUI_STREAM_DROP_NONE);
    // gui_obj_add_event_cb(st, stream_click_cb, GUI_EVENT_TOUCH_CLICKED, NULL);
    return 0;
}


GUI_INIT_APP_EXPORT(app_init);
