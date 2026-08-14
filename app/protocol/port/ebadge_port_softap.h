/**
 * @file    ebadge_port_softap.h
 * @brief   SoftAP control -- start / stop a temporary AP for the file xfer.
 *
 * Backing may be lwIP + on-chip WPA supplicant, or an external Wi-Fi chip
 * driven by AT / SPI.  The API is transport-agnostic.
 */
#ifndef _EBADGE_PORT_SOFTAP_H_
#define _EBADGE_PORT_SOFTAP_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback: STA has associated to our AP.  Fires on softap-driver thread.
 *  The impl MUST marshal this into l2_task via ebadge_task_post_call().     */
typedef void (*ebadge_softap_sta_joined_cb_t)(void);

typedef struct
{
    char     ssid[33];        /* nul-terminated                              */
    char     password[64];    /* nul-terminated; may be "" for open network  */
    uint8_t  channel;
    uint32_t ip;              /* AP-side IPv4 in host order, e.g. 0xC0A80401 */
} ebadge_softap_info_t;

/**
 * @brief  Bring up a SoftAP with the given creds.  Returns synchronously
 *         once the AP has been programmed into the radio (or fails).
 *         The caller registers a joined-cb to know when the STA (App) has
 *         associated -- only after that is TCP data expected.
 */
int  ebadge_port_softap_start(const ebadge_softap_info_t *info,
                              ebadge_softap_sta_joined_cb_t joined_cb);

/** Tear down the SoftAP; safe to call idempotently on cleanup paths. */
int  ebadge_port_softap_stop(void);

/** True while the AP is up (informational). */
bool ebadge_port_softap_running(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_PORT_SOFTAP_H_ */
