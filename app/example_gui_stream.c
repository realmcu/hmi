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

/*============================================================================*
 *                           Per-stream runtime
 *============================================================================*/



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



#define ENABLE_STREAM

#ifdef ENABLE_STREAM  // streaming
#include "stream_transport.h"   /* stp_config_t + stp_instance_create() */


/* Stream geometry / codec — fixed contract with the wifi RX producer. */
#define STREAM_X            0
#define STREAM_Y            0
#define STREAM_W            400
#define STREAM_H            496
#define STREAM_CODEC        GUI_STREAM_CODEC_JPEG
#define STREAM_INTERVAL_MS  10

/* Transport sizing.  The frame pool is allocated INTERNALLY by
 * stp_instance_create() (gui_malloc -> gui lower-mem heap); there is no
 * external pool address any more.  One size class of STREAM_BUF_COUNT buffers,
 * STREAM_MAX_FRAME bytes each -> heap budget = STREAM_MAX_FRAME *
 * STREAM_BUF_COUNT (+ alignment slack). */
#define STREAM_MAX_FRAME    (25u * 1024u)
#define STREAM_BUF_COUNT    16u

static const stp_class_cfg_t s_stream_classes[] =
{
    { .buf_size = STREAM_MAX_FRAME, .buf_count = STREAM_BUF_COUNT },
};

/* The single transport this app owns.  Created here (consumer side) and shared
 * with the wifi RX producer (wifi_data.c) through app_stream_transport_get(). */
static stp_transport_t *s_stream_tp = NULL;

stp_transport_t *app_stream_transport_get(void)
{
    return s_stream_tp;
}
#endif

static int app_init_stream(void)
{
    gui_log("GUI Stream Widget Example Start\n");
    extern void gui_set_keep_active_time(uint32_t active_time);
    gui_set_keep_active_time(1000000);

#ifdef ENABLE_STREAM  // streaming
    /* Create the one transport the app owns; the pool is allocated internally
     * from the size classes above.  JPEG is intra-only -> UNCONDITIONAL drop:
     * the consumer always advances to the newest frame, recycling older ones. */
    stp_config_t cfg;
    stp_config_default(&cfg);
    cfg.align       = 8;
    cfg.classes     = s_stream_classes;
    cfg.class_count = 1;
    cfg.drop_mode   = STP_DROP_UNCONDITIONAL;

    s_stream_tp = stp_instance_create(&cfg);
    if (!s_stream_tp)
    {
        gui_log("stream demo: stp_instance_create failed\n");
        return 0;
    }

    gui_stream_t *st = gui_stream_create(gui_obj_get_root(), NULL, STREAM_CODEC, s_stream_tp,
                                         STREAM_X, STREAM_Y, STREAM_W, STREAM_H);
    if (!st)
    {
        gui_log("stream demo:  gui_stream_create failed\n");
        stp_instance_destroy(s_stream_tp);
        s_stream_tp = NULL;
        return 0;
    }

    gui_stream_set_update_interval(st, STREAM_INTERVAL_MS);
    gui_stream_set_drop_mode(st, GUI_STREAM_DROP_UNCONDITIONAL);
    // gui_obj_add_event_cb(st, stream_click_cb, GUI_EVENT_TOUCH_CLICKED, NULL);
#endif
    return 0;
}

#if 0
#include "gui_video.h"
#include "410502_jpg.c"
static int app_init(void)
{
    gui_log("GUI video  Start\n");
    gui_set_keep_active_time(1000000);

#if 1

    extern const unsigned char _ac410502_jpg[];
    memcpy((STREAM_DB), _ac410502_jpg, _ac410502_jpg_len);
    gui_video_t *st = gui_video_create_from_mem(gui_obj_get_root(), NULL, STREAM_DB, 0,
                                                0, 480, 400);

#endif

    gui_video_set_frame_rate(st, 1);
    gui_video_set_repeat_count(st, -1);
    gui_video_set_state(st, GUI_VIDEO_STATE_PLAYING);



}
#endif


// GUI_INIT_APP_EXPORT(app_init_stream);
