/**
 * @file    ebadge_port_tcp.c
 * @brief   Data-plane endpoint -- inbound is JPGS slots, outbound is the
 *          reserved AT tunnel.  There is no socket on this chip.
 *
 * The protocol stack is written against a TCP listener (see ebadge_port_tcp.h),
 * but the phone's connection terminates on the 8711:
 *
 *     phone --Wi-Fi/TCP:5004--> 8711FA --SPI--> 8773G (us)
 *
 * So the two directions are asymmetric, and this file is thin in both:
 *
 *  - INBOUND never comes through here.  The 8711 forwards payload as JPGS slots
 *    on the SPI link, and wifi_xfer/jpgs_ingress.c reassembles them and calls
 *    the session entry points directly.  listen() therefore only records the
 *    port for logging; the on_data callback it is handed can never fire.  It is
 *    kept rather than deleted because it is what the sessions use to detect a
 *    failed bring-up, and because a future firmware with a real socket would
 *    fill it in without touching the sessions.
 *
 *  - OUTBOUND goes over the reserved AT data tunnel (wifi_8711_at_data.h).  The
 *    only thing the stack ever sends is the 8-byte EBXR result frame after a
 *    file verifies, which fits the tunnel's 56-byte budget with room to spare.
 *    That command does not exist in the 8711 firmware yet, so the send is
 *    expected to be refused with -ENOTSUP; see the note on send() below.
 */
#include <stddef.h>
#include <errno.h>
#include "ebadge_port_tcp.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "wifi_8711_at_data.h"
#endif

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
    /* Nothing to open: the 8711 is already listening on this port -- it is the
     * port it reported to us, not one we chose.  Inbound bytes arrive via
     * jpgs_ingress, so s_listener.on_data stays unused by design. */
    EBADGE_LOG1("port_tcp: armed for port=%d (8711 owns the socket)",
                (int)cfg->port);
    return 0;
}

#if defined(CONFIG_WIFI_8711)
/** Fires on the transport thread once the 8711 has answered. */
static void ack_sent(bool ok, void *user)
{
    (void)user;
    if (!ok)
    {
        EBADGE_WARN("port_tcp: 8711 rejected the ack send");
    }
}
#endif

int ebadge_port_tcp_send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U)
    {
        return -1;
    }

#if defined(CONFIG_WIFI_8711)
    /* Asynchronous: 0 means the command was staged, not that the phone has it.
     * The stack does not wait for this -- the authoritative completion signal
     * for the App is the BLE 0x15 DONE notify, which goes out regardless. */
    int rc = wifi_8711_at_data_send(data, len, ack_sent, NULL);
    if (rc == -ENOTSUP)
    {
        /* The expected answer against current 8711 firmware: AT+WLSEND= is a
         * reservation, not an implemented command (see wifi_8711_at_data.h).
         * Deliberately not an error -- the transfer itself succeeded, and
         * failing it here would throw away a correctly stored file over an
         * optional data-plane courtesy.  Logged once per call because it is
         * one line per transfer, not per slot. */
        EBADGE_LOG1("port_tcp: ack (%d B) not sent -- AT+WLSEND unimplemented",
                    (int)len);
        return 0;
    }
    if (rc != 0)
    {
        /* -EBUSY here means port_softap's AP query holds the single-flight AT
         * slot.  Same reasoning as above: worth seeing, not worth failing. */
        EBADGE_WARN2("port_tcp: ack send failed rc=%d len=%d", rc, (int)len);
        return 0;
    }
    EBADGE_LOG1("port_tcp: ack queued (%d B)", (int)len);
    return 0;
#else
    EBADGE_LOG1("port_tcp: no 8711 link, ack (%d B) dropped", (int)len);
    return 0;
#endif
}

int ebadge_port_tcp_close(void)
{
    if (s_armed)
    {
        /* Nothing to tear down on this side -- the 8711 owns the connection and
         * closes it when the phone does.  Clearing the flag keeps a late
         * callback from being attributed to a session that has ended. */
        EBADGE_LOG("port_tcp: disarmed");
    }
    s_armed = false;
    return 0;
}
