/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file  example_gui_stream.c
 * @brief gui_stream widget demo: two live streams decoded from AVI input.
 *
 * Two background "capture" threads play the producer role.  Each one demuxes an
 * AVI container in memory (the same clips used by the gui_lite_video demo),
 * pushes every video chunk -- one encoded frame per buffer -- through its own
 * ::stp_transport_t, and the matching gui_stream widget pulls and decodes them:
 *
 *   * left  : duck.avi   -> Microsoft Video 1 (MSV1)  272x272
 *   * right : cat_00.avi -> Cinepak (CVID)            360x360
 *
 * This exercises the MSV1 and Cinepak decode paths of gui_stream end to end.
 * Both streams run in GUI_STREAM_DROP_NONE mode: every frame is delivered in
 * order and never dropped, which is required because these codecs use
 * inter-frame (delta) compression -- skipping a frame would corrupt decoding.
 *
 * The producer only demuxes; it performs no decoding.  In a real product the
 * producer would be a camera or network-receive task handing encoded frames to
 * the UI task -- exactly the split this widget is designed for.
 *
 * Interaction:
 *   * Tap a video -- toggle its Play / Pause (pausing freezes the last frame
 *     and back-pressures its producer, which simply waits).
 */

#include <string.h>
#include "guidef.h"
#include "gui_obj.h"
#include "gui_api.h"
#include "gui_components_init.h"
#include "gui_obj_event.h"
#include "gui_stream.h"

/*============================================================================*
 *                       AVI binary resource binding
 *============================================================================*/
#ifdef _HONEYGUI_SIMULATOR_
extern const unsigned char _binary_duck_avi_start[];     /* MSV1    */
extern const unsigned char _binary_cat_00_avi_start[];   /* Cinepak */
#else
/* Replace with the actual Flash base addresses for the target board. */
#define STREAM_DEMO_FLASH_DUCK  0x240F400UL
#define STREAM_DEMO_FLASH_CAT   0x24FF400UL
#endif

/*============================================================================*
 *                            Demo configuration
 *============================================================================*/

#define DEMO_SCREEN_H   360u    /* matches DRV_LCD_HEIGHT in the SConscript */
#define DEMO_GAP        8        /* horizontal gap between the two videos    */

#define MAX_FRAME       (50u * 1024u)   /* per-buffer cap (>> any real frame) */
#define POOL_BUFS       4u              /* FIFO depth per stream              */

/* FourCC helpers (little-endian, matching the byte order in the file). */
#define FOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a)        | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

#define CC_RIFF   FOURCC('R', 'I', 'F', 'F')
#define CC_AVI    FOURCC('A', 'V', 'I', ' ')
#define CC_LIST   FOURCC('L', 'I', 'S', 'T')
#define CC_hdrl   FOURCC('h', 'd', 'r', 'l')
#define CC_movi   FOURCC('m', 'o', 'v', 'i')
#define CC_strh   FOURCC('s', 't', 'r', 'h')
#define CC_strf   FOURCC('s', 't', 'r', 'f')
#define CC_vids   FOURCC('v', 'i', 'd', 's')
#define CC_00dc   FOURCC('0', '0', 'd', 'c')

/* Codec FourCCs (see gui_lite_video). */
#define CC_CRAM   FOURCC('C', 'R', 'A', 'M')
#define CC_MSVC   FOURCC('M', 'S', 'V', 'C')
#define CC_wmsv   FOURCC('w', 'm', 's', 'v')
#define CC_CVID   FOURCC('C', 'V', 'I', 'D')
#define CC_cvid   FOURCC('c', 'v', 'i', 'd')

/*============================================================================*
 *                            AVI demux helpers
 *============================================================================*/
#if 0
/* Unaligned little-endian 32-bit read (the blob has no alignment guarantee). */
static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;   /* host is little-endian on every supported target */
}

typedef struct
{
    const uint8_t *movi;       /* first chunk inside the movi list  */
    uint32_t       movi_size;  /* byte span of the chunk region      */
    uint16_t       w;          /* frame width  (biWidth)             */
    uint16_t       h;          /* frame height (biHeight)            */
    uint32_t       fourcc;     /* biCompression                      */
    uint32_t       fps_num;    /* dwRate                             */
    uint32_t       fps_den;    /* dwScale                            */
} avi_info_t;

/* Parse just enough of the AVI header to locate the video stream format and
 * the movi chunk region.  Returns false if the file is not a usable AVI. */
static bool avi_parse(const uint8_t *data, avi_info_t *out)
{
    if (rd32(data) != CC_RIFF || rd32(data + 8) != CC_AVI)
    {
        return false;
    }

    uint32_t file_size = rd32(data + 4) + 8u;
    uint32_t strh_off = 0, strf_off = 0, movi_data = 0, movi_size = 0;
    uint32_t pos = 12;

    while (pos + 8u <= file_size)
    {
        uint32_t cc = rd32(data + pos);
        uint32_t sz = rd32(data + pos + 4);

        if (cc == CC_LIST)
        {
            uint32_t lt = rd32(data + pos + 8);

            if (lt == CC_movi)
            {
                movi_data = pos + 12u;
                movi_size = (sz >= 4u) ? (sz - 4u) : 0u;
            }
            else if (lt == CC_hdrl)
            {
                uint32_t hdrl_end = pos + 8u + sz;
                uint32_t q = pos + 12u;

                while (q + 8u <= hdrl_end && q + 8u <= file_size)
                {
                    uint32_t c2 = rd32(data + q);
                    uint32_t s2 = rd32(data + q + 4);

                    if (c2 == CC_LIST)
                    {
                        q += 12u;       /* descend into the strl list */
                        continue;
                    }
                    if (c2 == CC_strh && strh_off == 0 &&
                        rd32(data + q + 8) == CC_vids)
                    {
                        strh_off = q + 8u;
                    }
                    else if (c2 == CC_strf && strf_off == 0 && strh_off != 0)
                    {
                        strf_off = q + 8u;
                    }
                    q += 8u + s2 + (s2 & 1u);
                }
            }
        }
        pos += 8u + sz + (sz & 1u);
    }

    if (movi_data == 0 || strf_off == 0)
    {
        return false;
    }

    out->movi      = data + movi_data;
    out->movi_size = movi_size;
    out->w         = (uint16_t)rd32(data + strf_off + 4);    /* biWidth       */
    out->h         = (uint16_t)rd32(data + strf_off + 8);    /* biHeight      */
    out->fourcc    = rd32(data + strf_off + 16);             /* biCompression */

    if (strh_off != 0)
    {
        uint32_t scale = rd32(data + strh_off + 20);         /* dwScale */
        uint32_t rate  = rd32(data + strh_off + 24);         /* dwRate  */
        out->fps_num = (rate  != 0) ? rate  : 30u;
        out->fps_den = (scale != 0) ? scale : 1u;
    }
    else
    {
        out->fps_num = 30u;
        out->fps_den = 1u;
    }
    return true;
}

static gui_stream_codec_t avi_codec(uint32_t fourcc)
{
    if (fourcc == CC_MSVC || fourcc == CC_CRAM || fourcc == CC_wmsv)
    {
        return GUI_STREAM_CODEC_MSV1;
    }
    if (fourcc == CC_cvid || fourcc == CC_CVID)
    {
        return GUI_STREAM_CODEC_CINEPAK;
    }
    return GUI_STREAM_CODEC_RAW;   /* sentinel: unsupported here */
}
#endif
/*============================================================================*
 *                           Per-stream runtime
 *============================================================================*/

typedef struct
{
    // avi_info_t       info;
    stp_transport_t *tp;
    uint8_t         *pool;
    uint32_t         pool_size;
    uint32_t         interval_ms;
    const char      *label;
    volatile bool    running;
} demo_stream_t;

// static uint8_t       s_pool_duck[MAX_FRAME * POOL_BUFS + 64u];
// static uint8_t       s_pool_cat[MAX_FRAME * POOL_BUFS + 64u];
// static demo_stream_t s_duck;
// static demo_stream_t s_cat;


demo_stream_t s_stream_bt;

#if 0
/* Producer: demux the movi region and feed one encoded frame per buffer.
 * Blocks (never drops) when the FIFO is full so delta-frame order is kept. */
static void stream_producer_entry(void *param)
{
    demo_stream_t *s = (demo_stream_t *)param;
    const uint8_t *movi = s->info.movi;
    const uint8_t *end  = movi + s->info.movi_size;
    const uint8_t *p    = movi;
    bool keyframe = true;   /* first frame of every loop pass */

    while (s->running)
    {
        if (p + 8u > end)        /* end of clip -> loop back to the start */
        {
            p = movi;
            keyframe = true;
            continue;
        }

        uint32_t cc = rd32(p);
        uint32_t sz = rd32(p + 4);

        if (cc == CC_LIST)       /* 'rec ' grouping: step into it */
        {
            p += 12u;
            continue;
        }

        if (cc == CC_00dc && sz > 0u && sz <= MAX_FRAME)
        {
            const uint8_t *payload = p + 8u;

            /* No-drop: keep retrying the same frame until a buffer frees up. */
            stp_frame_t f;
            bool sent = false;
            while (s->running && !sent)
            {
                if (stp_acquire_free(s->tp, sz, &f))
                {
                    memcpy(f.addr, payload, sz);
                    stp_commit(s->tp, &f, sz, keyframe);
                    sent = true;
                }
                else
                {
                    gui_thread_mdelay(s->interval_ms);   /* FIFO full, wait */
                }
            }
            keyframe = false;
            gui_thread_mdelay(s->interval_ms);           /* frame pacing */
        }

        p += 8u + sz + (sz & 1u);
    }
}

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

/*============================================================================*
 *                           Stream bring-up
 *============================================================================*/

/* Parse @p avi, build a transport over @p pool, create a gui_stream widget at
 * (@p x, centred vertically) and launch the producer thread.  Returns the
 * widget, or NULL on any failure (all partial resources are released). */
static gui_stream_t *stream_start(demo_stream_t *s, const uint8_t *avi,
                                  uint8_t *pool, uint32_t pool_size,
                                  const char *name, int16_t x)
{
    // if (!avi_parse(avi, &s->info))
    // {
    //     gui_log("stream demo: %s parse failed\n", name);
    //     return NULL;
    // }

    // gui_stream_codec_t codec = avi_codec(s->info.fourcc);
    // if (codec == GUI_STREAM_CODEC_RAW)   /* sentinel for unsupported */
    // {
    //     gui_log("stream demo: %s unsupported codec 0x%08X\n",
    //             name, (unsigned)s->info.fourcc);
    //     return NULL;
    // }

    gui_stream_codec_t codec = GUI_STREAM_CODEC_MSV1;
    s->tp          = NULL;
    s->pool        = pool;
    s->pool_size   = pool_size;
    s->label       = name;
    // s->interval_ms = (1000u * s->info.fps_den) / s->info.fps_num;
    s->interval_ms = 46;
    if (s->interval_ms == 0u) { s->interval_ms = 1u; }

    static const stp_class_cfg_t classes[] =
    {
        { .buf_size = MAX_FRAME, .buf_count = POOL_BUFS },
    };
    stp_config_t cfg;
    stp_config_default(&cfg);
    cfg.pool        = pool;
    cfg.pool_size   = pool_size;
    cfg.align       = 4;
    cfg.classes     = classes;
    cfg.class_count = 1;
    cfg.drop_mode   = STP_DROP_NONE;

    s->tp = stp_create(&cfg);
    if (!s->tp)
    {
        gui_log("stream demo: %s stp_create failed\n", name);
        return NULL;
    }

    // int16_t y = (int16_t)((DEMO_SCREEN_H > s->info.h)
    //                       ? (DEMO_SCREEN_H - s->info.h) / 2u : 0u);
    int16_t y = (int16_t)50;

    gui_stream_t *st = gui_stream_create(gui_obj_get_root(), name, GUI_STREAM_CODEC_MSV1, s->tp,
                                         x, y,
                                         (int16_t)240, (int16_t)240);
    if (!st)
    {
        gui_log("stream demo: %s gui_stream_create failed\n", name);
        stp_destroy(s->tp);
        s->tp = NULL;
        return NULL;
    }

    gui_stream_set_update_interval(st, s->interval_ms);
    gui_stream_set_drop_mode(st, GUI_STREAM_DROP_NONE);
    gui_obj_add_event_cb(st, stream_click_cb, GUI_EVENT_TOUCH_CLICKED, NULL);

    s->running = true;
    // if (!gui_thread_create(name, stream_producer_entry, s, 1024 * 8, 5))
    // {
    //     gui_log("stream demo: %s producer thread create failed\n", name);
    // }

    gui_log("stream demo: %s %ux%u @ %u/%u fps, codec %d\n",
            name, (unsigned)s->info.w, (unsigned)s->info.h,
            (unsigned)s->info.fps_num, (unsigned)s->info.fps_den, (int)codec);
    return st;
}



#endif
/*============================================================================*
 *                          Application entry point
 *============================================================================*/

static int app_init(void)
{
    gui_log("GUI Stream Widget Example Start\n");




    // int16_t y = (int16_t)((DEMO_SCREEN_H > s->info.h)
    //                       ? (DEMO_SCREEN_H - s->info.h) / 2u : 0u);
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

// static int app_init(void)
// {
//     gui_log("GUI Stream Widget Example Start\n");

// #ifdef _HONEYGUI_SIMULATOR_
//     const uint8_t *duck_avi = (const uint8_t *)_binary_duck_avi_start;
//     const uint8_t *cat_avi  = (const uint8_t *)_binary_cat_00_avi_start;
// #else
//     // const uint8_t *duck_avi = (const uint8_t *)STREAM_DEMO_FLASH_DUCK;
//     // const uint8_t *cat_avi  = (const uint8_t *)STREAM_DEMO_FLASH_CAT;
// #endif

//     /* Left: MSV1 (duck.avi). */
//     gui_stream_t *left = stream_start(&s_stream_bt, NULL,
//                                       s_pool_duck, sizeof(s_pool_duck),
//                                       "stream_msv1", 0);

//     // /* Right: Cinepak (cat_00.avi), placed just past the MSV1 frame. */
//     // int16_t right_x = (int16_t)((left ? s_duck.info.w : 0u) + DEMO_GAP);
//     // gui_stream_t *right = stream_start(&s_cat, cat_avi,
//     //                                    s_pool_cat, sizeof(s_pool_cat),
//     //                                    "stream_cvid", right_x);

//     // if (!left && !right)
//     // {
//     //     gui_log("stream demo: no stream could be started\n");
//     //     return -1;
//     // }
//     return 0;
// }


GUI_INIT_APP_EXPORT(app_init);
