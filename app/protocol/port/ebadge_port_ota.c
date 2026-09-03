/**
 * @file    ebadge_port_ota.c
 * @brief   at_ota bound to the protocol stack, as described in ebadge_port_ota.h.
 *
 * Almost all of this file is the thread hop.  at_ota reports on at_client's
 * threads; the session it reports to lives on l2_task and has no locks.  So the
 * completion is parked in a static, a trampoline is posted, and l2_task delivers
 * it -- the same shape ebadge_port_tcp.c uses for its own driver callbacks, and
 * for the same reason.
 *
 * The image pointer never appears here.  It is handed straight to at_ota, which
 * hands it to at_client, which reads 16 KiB out of it per slot; nothing along the
 * way copies it and nothing needs to, since it is already in PSRAM and the caller
 * has promised to keep it there.
 */
#include <errno.h>
#include <stdint.h>

#include "ebadge_port_ota.h"
#include "../ebadge_task.h"
#include "../ebadge_log.h"

#if defined(CONFIG_WIFI_8711)
#include "at_ota.h"
#endif

/*----------------------------------------------------------------------------*
 *  State
 *
 *  s_done is written on l2_task (at submit) and read on l2_task (in the
 *  trampoline), so it needs no protection.  The at_ota callback that sits between
 *  them only writes s_res_* -- and only ever once per push, before the post that
 *  makes them readable.
 *----------------------------------------------------------------------------*/
static bool                 s_inited;
static ebadge_ota_done_cb_t s_done;
static bool                 s_res_ok;
static int                  s_res_error_no;

#if defined(CONFIG_WIFI_8711)

/* Bytes on the wire, updated from at_ota's progress callback.
 *
 * Written on at_client's thread, read wherever ebadge_port_ota_sent() is called.
 * A plain uint32_t deliberately: the read is atomic on this core, a torn value is
 * not representable, and the consumer is a progress notify that is allowed to be
 * one chunk stale.  A mutex here would put l2_task behind the transport thread on
 * every tick for nothing. */
static volatile uint32_t s_sent;

/*----------------------------------------------------------------------------*
 *  Completion, marshalled to l2_task
 *----------------------------------------------------------------------------*/
static void done_on_l2(void *arg)
{
    (void)arg;

    ebadge_ota_done_cb_t cb = s_done;

    s_done = NULL;

    if (cb != NULL)
    {
        cb(s_res_ok, s_res_error_no);
    }
}

static void ota_progress_from_driver(uint32_t sent, uint32_t total, void *user)
{
    (void)total;
    (void)user;

    s_sent = sent;
}

static void ota_done_from_driver(bool ok, int error_no, void *user)
{
    (void)user;

    s_res_ok       = ok;
    s_res_error_no = error_no;

    if (ebadge_task_post_call(done_on_l2, NULL) != 0)
    {
        /* The queue is full or the task is gone.  Worth being loud about: the
         * session that started this push is now waiting on a callback that will
         * never come, and its only remaining exit is a tick deadline. */
        EBADGE_ERR("port_ota: cannot post completion to l2_task");
    }
}

#endif /* CONFIG_WIFI_8711 */

/*----------------------------------------------------------------------------*
 *  Public API
 *----------------------------------------------------------------------------*/
void ebadge_port_ota_init(void)
{
    if (s_inited)
    {
        return;
    }
    s_inited = true;

#if defined(CONFIG_WIFI_8711)
    (void)at_ota_init();
    EBADGE_LOG("port_ota: ready (AT+OTAUSER over SPI)");
#else
    /* Not an error at init time -- plenty of builds have no Wi-Fi module at all.
     * It becomes an error when something tries to push. */
    EBADGE_LOG("port_ota: CONFIG_WIFI_8711=n -- no module to update");
#endif
}

int ebadge_port_ota_push(const uint8_t *image, uint32_t len,
                         ebadge_ota_done_cb_t done)
{
    if (image == NULL || len == 0U)
    {
        return -EINVAL;
    }

#if defined(CONFIG_WIFI_8711)
    if (s_done != NULL)
    {
        /* A completion is still outstanding.  at_ota would refuse too, but
         * refusing here keeps s_done from being overwritten -- which would strand
         * the first caller rather than the second. */
        return -EALREADY;
    }

    s_sent         = 0;
    s_res_ok       = false;
    s_res_error_no = 0;
    s_done         = done;

    int rc = at_ota_push(image, len, ota_progress_from_driver,
                         ota_done_from_driver, NULL);

    if (rc != 0)
    {
        s_done = NULL;
        EBADGE_ERR1("port_ota: push refused (%d)", rc);
        return rc;
    }

    EBADGE_LOG1("port_ota: pushing %u B to the Wi-Fi module", (unsigned)len);
    return 0;
#else
    (void)done;
    EBADGE_WARN("port_ota: push with CONFIG_WIFI_8711=n -> ENOTSUP");
    return -ENOTSUP;
#endif
}

int ebadge_port_ota_restart(void)
{
#if defined(CONFIG_WIFI_8711)
    /* No callback: the outcome we want is the module going away, and at_ota
     * already logs both the OK and the "ATCMD READY" that follows. */
    int rc = at_ota_reset_module(NULL, NULL);

    if (rc != 0)
    {
        EBADGE_ERR1("port_ota: restart refused (%d)", rc);
    }
    return rc;
#else
    return -ENOTSUP;
#endif
}

bool ebadge_port_ota_busy(void)
{
#if defined(CONFIG_WIFI_8711)
    /* s_done rather than at_ota_busy(): this covers the window between at_ota's
     * own completion and the trampoline reaching l2_task, during which the push
     * is over but the session has not been told. */
    return s_done != NULL;
#else
    return false;
#endif
}

uint32_t ebadge_port_ota_sent(void)
{
#if defined(CONFIG_WIFI_8711)
    return s_sent;
#else
    return 0;
#endif
}
