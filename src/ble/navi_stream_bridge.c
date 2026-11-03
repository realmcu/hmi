/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*
 * Bridge between the navigation projection service and the HoneyGUI stream
 * widget.
 *
 * The service validates a whole JPEG frame in its PSRAM receive slots and wakes
 * the GUI server thread through gui_send_msg_to_server(); the callback below is
 * therefore already running in the GUI thread, the same thread the gui_stream
 * widget renders from. That makes the copy into the transport race free without
 * a further hand-off.
 *
 * The service hands over the frame together with the 16-byte HoneyGUI image
 * header it writes in navi_check_frame_complete(), while jpeg_len covers the
 * JPEG payload only, so the transport length is jpeg_len + 16.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include "trace.h"
#include "gui_view.h"
#include "navi_projection_service.h"
#include "navi_stream_bridge.h"
#include <stream_transport.h>

/* Created by the HoneyGUI app_init() hook in ui_design/src/user/DashboardMain_user.c. */
extern stp_transport_t *gui_stream_transport_get(void);

/* gui_rgb_data_head_t(8) + size(4) + dummy(4), see navi_check_frame_complete(). */
#define NAVI_GUI_HEADER_LEN 16

#define NAVI_PROJECTION_VIEW "carplay_view"

/* Only the first frame of a session pulls the panel over to the map view. */
static bool s_show_view_pending = true;
static bool s_switch_reported;

static void navi_stream_ensure_projection_view(void)
{
    gui_view_t *current = gui_view_get_current();

    if (current == NULL)
    {
        return;
    }

    if (strcmp(current->base.name, NAVI_PROJECTION_VIEW) == 0)
    {
        s_show_view_pending = false;
        return;
    }

    /* The switch can be refused while another transition is running, so the
     * request stays pending and the next frame retries it. Log it once. */
    if (!s_switch_reported)
    {
        s_switch_reported = true;
        APP_PRINT_INFO0("navi_stream: switching to the projection view");
    }
    gui_view_switch_direct(current,
                           NAVI_PROJECTION_VIEW,
                           SWITCH_OUT_NONE_ANIMATION,
                           SWITCH_IN_NONE_ANIMATION);
}

static bool navi_stream_push(const uint8_t *p_gui_src, uint32_t jpeg_len, uint16_t frame_seq)
{
    stp_transport_t *tp = gui_stream_transport_get();

    if (tp == NULL || p_gui_src == NULL || jpeg_len == 0u)
    {
        APP_PRINT_WARN1("navi_stream: transport not ready, frame %u dropped", frame_seq);
        return false;
    }

    uint32_t frame_len = jpeg_len + NAVI_GUI_HEADER_LEN;
    stp_frame_t frame;

    if (!stp_acquire_free(tp, frame_len, &frame))
    {
        /* Pool exhausted, or the frame is larger than the biggest size class. */
        APP_PRINT_WARN2("navi_stream: no buffer for frame %u (%u bytes)", frame_seq, frame_len);
        return false;
    }

    memcpy(frame.addr, p_gui_src, frame_len);
    if (!stp_commit(tp, &frame, frame_len, true))
    {
        APP_PRINT_ERROR1("navi_stream: commit failed for frame %u", frame_seq);
        return false;
    }

    if (s_show_view_pending)
    {
        navi_stream_ensure_projection_view();
    }

    return true;
}

void navi_stream_bridge_init(void)
{
    navi_projection_set_gui_frame_cb(navi_stream_push);
    APP_PRINT_INFO0("navi_stream: GUI frame consumer installed");
}

void navi_stream_bridge_on_disconnect(void)
{
    s_show_view_pending = true;
    s_switch_reported = false;
}
