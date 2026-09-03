/**
 * @file    at_ota.h
 * @brief   The AT+OTA* commands eBadge uses to reflash the 8711 over the SPI
 *          link.  The third semantic layer over at_client, next to at_wifi and
 *          at_socket.
 *
 * ---------------------------------------------------------------------------
 * ONE COMMAND, AND IT IS A TRANSPARENT-MODE PUSH
 * ---------------------------------------------------------------------------
 * AT+OTAUSER=<sysrst>,<length> declares the whole firmware length up front,
 * answers ">>>", then swallows exactly <length> bytes and prints OK.  Identical
 * transparent-mode semantics to AT+SKTSENDRAW -- the byte count ends the mode,
 * nothing is escaped, and the three '<' frames abort -- so it rides
 * at_client_submit_stream() rather than growing its own transport.
 *
 * The image is BORROWED, never copied: it is over a megabyte and it already sits
 * in PSRAM where the receive path put it.  See at_stream_req_t for the lifetime
 * rule; here it means the caller must keep the buffer intact until the done
 * callback fires.
 *
 * The other command in the doc, AT+OTAHTTP, is deliberately not wrapped.  It has
 * the 8711 fetch the image itself over HTTP, which needs the 8711 joined to a
 * network with a route to a server -- the opposite of this product's topology,
 * where the 8711 is a SoftAP and the phone is the only thing on it.
 *
 * ---------------------------------------------------------------------------
 * TWO PRECONDITIONS THAT ARE NOT OURS TO SATISFY
 * ---------------------------------------------------------------------------
 *   - The OTA command set is DISABLED by default in the 8711's build.  It only
 *     exists if "Enable OTA" was ticked under CONFIG SHELL in that project's
 *     menuconfig.  Without it, AT+OTAUSER comes back as a plain unknown-command
 *     ERROR and there is nothing this side can do about it.
 *   - The doc restricts AT+OTAUSER to 主控控制模式 (host-controlled mode), which
 *     is what the SPI link is -- so this is satisfied here, but it is the reason
 *     the command is unavailable over a plain UART console without flow control.
 *
 * ---------------------------------------------------------------------------
 * WHY THE REBOOT IS A SEPARATE COMMAND
 * ---------------------------------------------------------------------------
 * <sysrst> can ask the 8711 to restart itself the moment the image lands, and
 * this module always passes 0 -- never 1.  With sysrst=1 the ordering between the
 * closing "OK" and the reboot is not specified anywhere, so the OK that ends the
 * push may or may not arrive, and the caller cannot tell "the image was rejected"
 * from "the image was accepted and the module went down before answering".
 *
 * With sysrst=0 the push has an unambiguous verdict, and the restart becomes
 * at_ota_reset_module() -- issued when the caller is ready for the link to drop,
 * i.e. after the SoftAP is down and the BLE report has gone out.  The 8711's
 * "ATCMD READY" on the way back up is already handled: at_client fans it out to
 * the ready sinks, which is how at_wifi and at_socket learn their state is gone.
 *
 * CONTEXT: submit from any thread.  Callbacks arrive on at_client's threads --
 * the SPI transport thread or the system workqueue, never l2_task.
 */
#ifndef _AT_OTA_H_
#define _AT_OTA_H_

#include <stdint.h>
#include <stdbool.h>

#include "at_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Budgets
 *----------------------------------------------------------------------------*/

/** Command line out -> ">>>".  AT+OTAUSER allocates its receive buffer and runs
 *  the OTA init (which reads the 8711's own flash to pick the target partition)
 *  before it prompts, and error codes 2 and 3 are exactly those two steps
 *  failing.  10 s is at_client's stream default; named here so the number has a
 *  reason on this command rather than only a definition. */
#define AT_OTA_PROMPT_MS        AT_CLIENT_STREAM_PROMPT_MS

/** Last byte in -> OK.  The 8711 finalises and checksums the image here, and a
 *  megabyte of NOR programming is not fast.  at_client's 60 s default. */
#define AT_OTA_TERM_MS          AT_CLIENT_STREAM_TERM_MS

/** Budget for AT+RST.  It prints OK and reboots; nothing to wait for beyond the
 *  reply itself, and if the reply is lost the reboot happened anyway. */
#define AT_OTA_RESET_MS         3000U

/*----------------------------------------------------------------------------*
 *  Callbacks
 *----------------------------------------------------------------------------*/

/** @brief  Bytes handed to the wire so far.  Fires at most once per ~16 KiB.
 *
 *  "Handed to the wire", not "acknowledged": the 8711 acknowledges nothing until
 *  the closing OK, so this is a send-side counter and cannot be read as progress
 *  through the flash write. */
typedef void (*at_ota_progress_cb_t)(uint32_t sent, uint32_t total, void *user);

/**
 * @brief  Outcome of one push or one reset.
 *
 * @param  ok        the command answered OK.  For a push this means the 8711
 *                   accepted the whole image; with sysrst=0 it is now sitting on
 *                   a staged image waiting to be restarted.
 * @param  error_no  the <n> from "ERROR: <n>", else 0.  Worth logging and worth
 *                   reporting upwards -- see at_ota_err_str(), the codes
 *                   distinguish "you sent me nonsense" from "my flash failed".
 */
typedef void (*at_ota_done_cb_t)(bool ok, int error_no, void *user);

/*----------------------------------------------------------------------------*
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/** @brief  Initialise the module and the AT client under it.  Idempotent. */
int at_ota_init(void);

/**
 * @brief  Push a whole firmware image to the 8711 with AT+OTAUSER.
 *
 * Sends AT+OTAUSER=0,<len>, waits for ">>>", then streams @p image in 16 KiB
 * slots until @p len bytes are out.  sysrst is always 0 -- see the header note.
 *
 * @param  image     BORROWED.  Must stay valid and unmodified until @p done
 *                   fires; nothing is copied.
 * @param  len       image length in bytes, and the <length> the command declares.
 *                   The two cannot disagree: transparent mode ends on the count.
 * @param  progress  may be NULL
 * @param  done      called exactly once, possibly a minute later
 *
 * @retval 0         queued
 * @retval -EINVAL   NULL image or len == 0
 * @retval -EALREADY a push is already in flight
 * @retval other     as at_client_submit_stream()
 */
int at_ota_push(const uint8_t *image, uint32_t len,
                at_ota_progress_cb_t progress,
                at_ota_done_cb_t done, void *user);

/**
 * @brief  Restart the 8711 with AT+RST, so a staged image becomes the running
 *         one.
 *
 * Everything the 8711 was told is gone afterwards: the SoftAP, any TCP server,
 * any socket.  Callers must expect that and must not issue this while a transfer
 * they care about is open.  at_client's ready sinks report the module coming
 * back, which is where at_wifi and at_socket drop their cached state.
 */
int at_ota_reset_module(at_ota_done_cb_t done, void *user);

/** @brief  True between at_ota_push() returning 0 and its done callback. */
bool at_ota_busy(void);

/**
 * @brief  Name for an AT+OTAUSER "ERROR: <n>".
 *
 * The doc's table, verbatim, because the codes are genuinely different faults and
 * a bare number in a log is the difference between a five-minute diagnosis and an
 * afternoon: 1/2/3 are all "the command never started" (bad length, no memory, no
 * usable OTA partition), 4 is "one is already running", 5 is a transparent-mode
 * fault on the link itself, and 6 is the only one that means bytes were taken and
 * the flash write went wrong.
 *
 * @return a static string; "?" for anything not in the table.
 */
const char *at_ota_err_str(int error_no);

#ifdef __cplusplus
}
#endif

#endif /* _AT_OTA_H_ */
