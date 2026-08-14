/**
 * @file    ebadge_port_softap.c
 * @brief   SoftAP porting stub.
 *
 * TODO(port): backing Wi-Fi driver has not been decided.  Options include:
 *   - Realtek in-tree WPA supplicant + lwIP net stack (native)
 *   - AT-command bridge over UART to an external Wi-Fi chip
 *   - SPI-tethered host with a socket abstraction
 *
 * For now every entry point logs a warning and returns success without
 * doing any real work; the xfer_session state machine will still advance
 * so BLE-side flows are testable end-to-end without the data plane.
 */
#include "ebadge_port_softap.h"
#include "../ebadge_log.h"

static bool                          s_running;
static ebadge_softap_sta_joined_cb_t s_joined_cb;

int ebadge_port_softap_start(const ebadge_softap_info_t *info,
                             ebadge_softap_sta_joined_cb_t joined_cb)
{
    /* TODO(port): drive the real Wi-Fi driver here. */
    (void)info;
    s_joined_cb = joined_cb;
    s_running   = true;
    EBADGE_WARN("port_softap: start STUB -- no real AP raised");
    /* The App will never actually join, so the WAIT_STA timeout in
     * xfer_session will trip after 60s.  That is the intended behaviour
     * until this shim is implemented. */
    return 0;
}

int ebadge_port_softap_stop(void)
{
    if (s_running)
    {
        EBADGE_LOG("port_softap: stop STUB");
    }
    s_running   = false;
    s_joined_cb = 0;
    return 0;
}

bool ebadge_port_softap_running(void)
{
    return s_running;
}
