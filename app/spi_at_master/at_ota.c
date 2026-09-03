/**
 * @file    at_ota.c
 * @brief   AT+OTAUSER / AT+RST wrappers, as described in at_ota.h.
 *
 * Thin on purpose.  The hard part of pushing a megabyte through a 16 KiB
 * full-duplex slot is at_client_submit_stream()'s, and the hard part of deciding
 * WHETHER to push is the session's; what is left here is one command line, a busy
 * flag, and the error table.
 *
 * The busy flag is not redundant with at_client_busy().  at_client is busy for
 * every AT command from anywhere, and an OTA push is the one thing in this system
 * that must not be started twice: the second AT+OTAUSER would come back as error
 * 4 (OTA in progress) with the first one's bytes still streaming, and the first
 * would then fail its own count.  So the refusal happens here, before anything
 * reaches the queue.
 */
#if defined(CONFIG_SPI_AT_MASTER)

#include <zephyr/kernel.h>
#include <errno.h>
#include <stdio.h>

#include "at_ota.h"
#include "../protocol/ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  State
 *
 *  Written from at_client's callback threads and read from l2_task (through
 *  at_ota_busy()), so the flag is a plain bool with a single writer per
 *  transition: set before the submit, cleared in the completion.  No mutex,
 *  because there is nothing to keep consistent WITH it -- unlike at_wifi's AP
 *  struct, a caller that reads a stale `busy` gets refused or gets a queue slot,
 *  and both are safe.
 *----------------------------------------------------------------------------*/
static bool                 s_inited;
static bool                 s_pushing;

/*  The push's caller.  Held here rather than in at_client's user pointer because
 *  s_pushing has to be cleared before the callback runs: a done handler that
 *  immediately queues the AT+RST would otherwise be refused by its own push. */
static at_ota_done_cb_t     s_push_done;
static at_ota_progress_cb_t s_push_progress;
static void                *s_push_user;

/*  Same for AT+RST.  A static rather than at_client's user pointer because
 *  casting a function pointer through void * is not something standard C
 *  promises, and this is not a place worth spending an extension on. */
static at_ota_done_cb_t     s_reset_done;
static void                *s_reset_user;

/*----------------------------------------------------------------------------*
 *  Error table  (doc: OTA AT 命令集, AT+OTAUSER 错误码)
 *----------------------------------------------------------------------------*/
const char *at_ota_err_str(int error_no)
{
    /* Negative is not a module code and never will be: those are errnos from our
     * own side, handed on by at_client for a command that never got a reply to
     * have a code IN.  The table used to answer "?" for them, which reads as "the
     * module said something we do not recognise" and points an investigation at
     * the wrong end of the wire -- so they are named here instead. */
    if (error_no < 0)
    {
        return "not a module code: the push failed on our side of the SPI link";
    }

    switch (error_no)
    {
    case 0:  return "no code (bare ERROR, or nothing came back at all)";
    case 1:  return "param";
    case 2:  return "nomem";
    case 3:  return "ota init";
    case 4:  return "ota in progress";
    case 5:  return "tt mode";
    case 6:  return "update failed";
    default: return "?";
    }
}

/*----------------------------------------------------------------------------*
 *  AT+OTAUSER
 *----------------------------------------------------------------------------*/

static void push_progress(uint32_t sent, uint32_t total, void *user)
{
    ARG_UNUSED(user);

    if (s_push_progress != NULL)
    {
        s_push_progress(sent, total, s_push_user);
    }
}

static void push_done(at_result_t res, int error_no, void *user)
{
    ARG_UNUSED(user);

    bool ok = (res == AT_RES_OK);

    at_ota_done_cb_t cb  = s_push_done;
    void            *cbu = s_push_user;

    /* Cleared first -- see the note on s_push_done. */
    s_pushing       = false;
    s_push_done     = NULL;
    s_push_progress = NULL;
    s_push_user     = NULL;

    if (ok)
    {
        EBADGE_LOG("ota: 8711 accepted the image (not restarted yet)");
    }
    else if (res == AT_RES_ERROR)
    {
        /* The one case where error_no is the module's own verdict: it answered
         * "ERROR: <n>", so it saw the command and turned it down. */
        EBADGE_ERR1("ota: the 8711 refused the image -- ERROR %d", error_no);
        EBADGE_ERR2("ota: error %d means \"%s\"", error_no,
                    at_ota_err_str(error_no));
    }
    else
    {
        /* Everything else never got a verdict: a timeout, a reset, or a frame we
         * could not put on the wire.  Saying "the module refused" here sends the
         * investigation to the 8711 when the evidence is all on this side. */
        EBADGE_ERR2("ota: push never reached a verdict -- %s (%d); the image was "
                    "not written", at_result_str(res), error_no);
    }

    if (cb != NULL)
    {
        cb(ok, error_no, cbu);
    }
}

int at_ota_push(const uint8_t *image, uint32_t len,
                at_ota_progress_cb_t progress,
                at_ota_done_cb_t done, void *user)
{
    if (image == NULL || len == 0U)
    {
        return -EINVAL;
    }
    if (s_pushing)
    {
        return -EALREADY;
    }

    int rc = at_ota_init();

    if (rc != 0)
    {
        return rc;
    }

    char cmd[48];

    /* sysrst = 0 always.  The reboot is at_ota_reset_module()'s job -- see the
     * header note on why the two are not combined. */
    (void)snprintf(cmd, sizeof(cmd), "AT+OTAUSER=0,%u", (unsigned)len);

    s_pushing       = true;
    s_push_done     = done;
    s_push_progress = progress;
    s_push_user     = user;

    at_stream_req_t req =
    {
        .data        = image,
        .len         = len,
        .on_progress = (progress != NULL) ? push_progress : NULL,
        .done        = push_done,
        .user        = NULL,
        .prompt_ms   = AT_OTA_PROMPT_MS,
        .chunk_ms    = 0,               /* at_client's per-chunk default */
        .term_ms     = AT_OTA_TERM_MS,
    };

    rc = at_client_submit_stream(cmd, &req);
    if (rc != 0)
    {
        /* Nothing was queued, so nothing will call push_done().  Undo the flag
         * here or the next attempt is refused for a push that never started. */
        s_pushing       = false;
        s_push_done     = NULL;
        s_push_progress = NULL;
        s_push_user     = NULL;
        EBADGE_ERR1("ota: submit failed (%d)", rc);
        return rc;
    }

    EBADGE_LOG1("ota: pushing %u B to the 8711 (AT+OTAUSER, sysrst=0)",
                (unsigned)len);
    return 0;
}

bool at_ota_busy(void)
{
    return s_pushing;
}

/*----------------------------------------------------------------------------*
 *  AT+RST
 *----------------------------------------------------------------------------*/

static void reset_done(at_result_t res, int error_no, void *user)
{
    ARG_UNUSED(user);

    at_ota_done_cb_t cb  = s_reset_done;
    void            *cbu = s_reset_user;

    s_reset_done = NULL;
    s_reset_user = NULL;

    /* AT_RES_RESET counts as success, and this is the one command where that is
     * not a contradiction: it means "ATCMD READY" arrived while the command was
     * still in flight, i.e. the 8711 rebooted before its own OK reached us --
     * which is precisely what was asked for.  Reporting that as a failure would
     * have callers retry a reset that already happened. */
    bool ok = (res == AT_RES_OK) || (res == AT_RES_RESET);

    if (ok)
    {
        EBADGE_LOG("ota: AT+RST accepted, 8711 restarting");
    }
    else
    {
        EBADGE_ERR2("ota: AT+RST failed -- %s (%d)", at_result_str(res),
                    error_no);
    }

    if (cb != NULL)
    {
        cb(ok, error_no, cbu);
    }
}

int at_ota_reset_module(at_ota_done_cb_t done, void *user)
{
    int rc = at_ota_init();

    if (rc != 0)
    {
        return rc;
    }

    /* Not single-flight: a second reset before the first answers simply replaces
     * the callback, and both callers wanted the same thing.  Which is why this
     * pair of statics needs no guard, unlike the push's. */
    s_reset_done = done;
    s_reset_user = user;

    rc = at_client_submit("AT+RST", NULL, reset_done, NULL, AT_OTA_RESET_MS);
    if (rc != 0)
    {
        s_reset_done = NULL;
        s_reset_user = NULL;
    }
    return rc;
}

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/
int at_ota_init(void)
{
    if (s_inited)
    {
        return 0;
    }

    int rc = at_client_init();

    if (rc != 0)
    {
        return rc;
    }

    s_inited = true;
    return 0;
}

#endif /* CONFIG_SPI_AT_MASTER */
