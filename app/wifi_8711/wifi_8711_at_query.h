/**
 * @file    wifi_8711_at_query.h
 * @brief   Bring-up hook: send one AT command to the 8711 and log the reply.
 *
 * This is the smallest thing that can answer "is the SPI link to the 8711
 * alive, and does its SoftAP exist?" from a single BLE debug command, without
 * the JPGS/ATMC slot engine or the port_softap layer being written yet.
 *
 * WHY IT CANNOT BE SYNCHRONOUS -- the one thing to understand before calling:
 *
 *   The 8711 is the SPI master.  It owns SCLK and CS and polls roughly every
 *   2 s.  We cannot transmit; we can only *stage* a slot and wait to be
 *   clocked.  A command therefore needs one poll to go out and another to
 *   bring the reply back, so the reply is 2..4 s behind the call and arrives
 *   on the transport thread, not on the caller's.
 *
 *   Hence: these functions return as soon as the command is staged.  A 0 means
 *   "queued", NOT "the 8711 answered".  The answer shows up in the log from
 *   the slot sink installed here.
 *
 * The sink is installed unconditionally by these calls, which would displace a
 * production sink if one existed.  That is fine today (there is none) and is
 * the reason this file is scoped as a debug hook rather than an API.
 */
#ifndef _WIFI_8711_AT_QUERY_H_
#define _WIFI_8711_AT_QUERY_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Ask the 8711 for its SoftAP state ("AT+WLSTATE"), log the reply.
 *
 * Starts the slot transport if it is not running yet -- nothing in main()
 * starts it, so a debug command that assumed otherwise would stage bytes into
 * a transport that never clocks them and report success.
 *
 * The reply is plain line-oriented text (SSID / PASSWD / IP / PORT / CLIENTS,
 * terminated by "[+WLSTATE]:OK"), dumped to the log when it arrives.
 *
 * @retval 0        staged; watch the log for the reply 2..4 s later
 * @retval -ENODEV  wifi_8711_init() never succeeded (no 8711 on this build)
 * @retval <0       transport start or staging error
 */
int wifi_8711_at_query_ap_info(void);

/**
 * @brief  Ask the 8711 to start / read back its SoftAP ("AT+WLSTARTAP").
 *
 * Idempotent on the 8711 side, and its SoftAP already self-starts at boot, so
 * this exists for the case where wifi_8711_at_query_ap_info() comes back
 * saying the AP is not running.
 *
 * Same asynchronous contract as above.
 */
int wifi_8711_at_start_ap(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_AT_QUERY_H_ */
