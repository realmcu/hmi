/**
 * @file    ebadge_port_tcp.c
 * @brief   TCP byte-stream porting stub.
 *
 * TODO(port): a real transport is needed here (lwIP tcp_new/listen, or an
 * AT-command bridge to an off-chip Wi-Fi module).  See the delivery contract
 * in ebadge_port_tcp.h -- on_data must deliver >= 4KB batches to keep the
 * l2_task msg queue from starving.
 *
 * The stub logs and returns success without doing any real work.  Combined
 * with the softap stub, this means the WAIT_STA / RECV timeouts will trip
 * during end-to-end tests -- exactly what we want for the BLE-side smoke
 * tests until the data plane is wired.
 */
#include <stddef.h>
#include "ebadge_port_tcp.h"
#include "../ebadge_log.h"

static ebadge_tcp_listen_t s_listener;
static bool                s_armed;

int ebadge_port_tcp_listen(const ebadge_tcp_listen_t *cfg)
{
    if (cfg == NULL)
    {
        return -1;
    }
    s_listener = *cfg;
    s_armed    = true;
    EBADGE_WARN1("port_tcp: listen STUB port=%d", (int)cfg->port);
    /* TODO(port): really open the socket / spin up the transport bridge. */
    return 0;
}

int ebadge_port_tcp_send(const uint8_t *data, uint16_t len)
{
    (void)data;
    EBADGE_WARN1("port_tcp: send STUB len=%d", (int)len);
    /* TODO(port): write the bytes onto the underlying connection.        */
    return 0;
}

int ebadge_port_tcp_close(void)
{
    if (s_armed)
    {
        EBADGE_LOG("port_tcp: close STUB");
    }
    s_armed = false;
    return 0;
}
