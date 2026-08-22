/**
 * @file    wifi_8711.h
 * @brief   RTL8711FA link over SPI -- WE ARE THE SLAVE.  Mounting skeleton.
 *
 * SCOPE: hardware bring-up only.  This proves the devicetree, Kconfig and pad
 * plumbing resolves (SPI slave controller + B2W/W2B handshake GPIOs + the
 * non-cacheable DMA staging region) and gives the eBadge port layer something
 * to call.  The slot engine -- JPGS reassembly, ATMC command/response, JPU
 * decode and the LCDC handoff -- is NOT here yet.
 *
 * Role, since it is the one thing worth repeating: per the v2.0 protocol
 * (note/refer/spi-at-command-protocol 1.md sec.1) the RTL8711FA is the SPI
 * master.  It drives SCLK and CS; the 8773G cannot generate a clock at all.
 * Every transaction is one fixed 4096-byte full-duplex slot that the 8711
 * initiates: we park a TX slot in DMA, arm RX, raise B2W, and wait to be
 * clocked.  There is no master variant of this transport.
 *
 * Architecture, for whoever lands the rest:
 *
 *   eBadge protocol (l2_task)  /  JPU decode + LCDC
 *        |  port_softap / port_tcp are AT commands, not sockets
 *   JPGS + ATMC slot parser        <- to be written (sec.4..6)
 *        |  4096 B slots, Magic at offset 0 selects the parser
 *   SPI slave DMA + B2W/W2B        <- partially here (pins + IRQ only)
 *        |  spi0_slave, mode 3, 8 bit, ~18.5 MHz supplied by the 8711
 *   8711FA (owns the SoftAP and the TCP/IP stack in its own firmware)
 *
 * Note the last line: there is no lwIP and no Zephyr net_if on this SoC.
 * The AP and the socket both live in the 8711, reachable only through ATMC
 * packets on the MISO half of a slot.
 */
#ifndef _WIFI_8711_H_
#define _WIFI_8711_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Protocol constants (sec.10).  Here rather than in a private header because
 *  the parser, the transport and the shell all need the same slot geometry.
 *----------------------------------------------------------------------------*/
#define WIFI_8711_SLOT_SIZE        4096U   /* every transfer, exactly       */

/* Header size is PER SLOT TYPE, not global: JPGS and ATMC both use 32 bytes,
 * EBFS uses 64.  Anything parsing a slot must pick by the magic at offset 0
 * before it computes a payload pointer -- see WIFI_8711_FILE_HEADER_SIZE. */
#define WIFI_8711_HEADER_SIZE      32U     /* JPGS and ATMC only            */

#define WIFI_8711_JPG_MAGIC        0x5347504AU  /* "JPGS", wire 4A 50 47 53 */
#define WIFI_8711_JPG_VERSION      1U
#define WIFI_8711_JPG_FLAG_START   0x01U
#define WIFI_8711_JPG_FLAG_END     0x02U
#define WIFI_8711_JPG_PAYLOAD_MAX  (WIFI_8711_SLOT_SIZE - WIFI_8711_HEADER_SIZE)

/*----------------------------------------------------------------------------*
 *  EBFS -- the file slot (protocol v2.2 sec.6), third magic on the same link.
 *
 *  A separate slot type exists because a file is not a preview frame.  JPGS
 *  carries per-frame geometry only, so a file crossing it would have to get its
 *  identity from the BLE offer alone; EBFS restates the identity the phone put
 *  in its EBXF header (session, name, type, total size, whole-file CRC32) in
 *  EVERY slot, which is what lets the 8711 forward a 2 MiB file without ever
 *  buffering it and lets us cross-check the two planes agree.
 *
 *  The 8711 does not interpret File Type and does not filter on it (sec.6): it
 *  copies the EBXF byte through verbatim, so accepting or refusing the type is
 *  entirely our decision.
 *----------------------------------------------------------------------------*/
#define WIFI_8711_FILE_MAGIC       0x53464245U  /* "EBFS", wire 45 42 46 53 */
#define WIFI_8711_FILE_VERSION     1U
#define WIFI_8711_FILE_FLAG_START  0x01U
#define WIFI_8711_FILE_FLAG_END    0x02U
#define WIFI_8711_FILE_HEADER_SIZE 64U
#define WIFI_8711_FILE_PAYLOAD_MAX (WIFI_8711_SLOT_SIZE - WIFI_8711_FILE_HEADER_SIZE)
#define WIFI_8711_FILE_NAME_LEN    24U     /* field width; 1..23 used       */

/* Largest single file the 8711's port-9000 entry accepts (sec.6).  Distinct
 * from WIFI_8711_JPEG_FRAME_MAX below, and 34x larger: that one is a *frame*
 * cap on the preview port, this one is a *file* cap on the upload port.
 * Conflating them is why a normal-sized wallpaper used to be refused. */
#define WIFI_8711_FILE_SIZE_MAX    (2U * 1024U * 1024U)

#define WIFI_8711_AT_MAGIC         0x434D5441U  /* "ATMC", wire 41 54 4D 43 */
#define WIFI_8711_AT_VERSION       1U
#define WIFI_8711_AT_TYPE_COMMAND  1U      /* 8773 -> 8711, on MISO         */
#define WIFI_8711_AT_TYPE_RESPONSE 2U      /* 8711 -> 8773, on MOSI         */
#define WIFI_8711_AT_TYPE_POLL     3U      /* 8711 -> 8773 heartbeat        */
#define WIFI_8711_AT_PAYLOAD_MAX   (WIFI_8711_SLOT_SIZE - WIFI_8711_HEADER_SIZE)

/* The 8711's command parse buffer is 128 bytes; a longer COMMAND payload is
 * silently dropped (sec.9).  Not a slot limit -- a peer firmware limit. */
#define WIFI_8711_AT_COMMAND_MAX   128U

/* The 8711's *preview* TCP entry caps a frame at 61440 B (sec.5.2); our
 * reassembly buffer must be at least this large.  Not a file-size limit -- see
 * WIFI_8711_FILE_SIZE_MAX for the upload port. */
#define WIFI_8711_JPEG_FRAME_MAX   61440U

/**
 * @brief  Probe the devicetree resources and configure the SPI slave pads.
 *
 * Verifies spi0_slave came up, applies the slave SPI configuration (mode 3,
 * 8 bit, MSB first), parks B2W low and arms the W2B edge interrupt.
 *
 * Does NOT arm a transfer, so the 8711 will see B2W stay low and time out its
 * READY wait (1000 ms, sec.3.2) -- expected until the slot engine exists.
 *
 * @retval 0        resources present, pads configured, W2B interrupt armed
 * @retval -ENODEV  SPI controller or a GPIO port is not ready
 * @retval <0       gpio/spi configuration failed (errno from the driver)
 */
int wifi_8711_init(void);

/** True once wifi_8711_init() has succeeded. */
bool wifi_8711_ready(void);

/**
 * @brief  Drive B2W / READY.
 *
 * @param  ready  true raises the line (we are armed), false drops it.
 *
 * Callers must respect the four-phase rule (sec.3.1): raise only after BOTH
 * RX and TX DMA are armed, drop from the RX-done ISR.  Never pulse it -- the
 * 8711 may be blocked on either edge.
 */
int wifi_8711_set_ready(bool ready);

/** Current logical level of B2W / READY, as last driven by us. */
int wifi_8711_get_ready(void);

/** Current logical level of W2B / REQUEST (1 = the 8711 wants a slot). */
int wifi_8711_get_request(void);

/*----------------------------------------------------------------------------*
 *  Bus handles for the transport layer.
 *
 *  The slave configuration is validated once during wifi_8711_init(), so the
 *  transport re-uses the very same struct rather than building a second one
 *  that could drift out of agreement with it -- the driver compares the config
 *  pointer when deciding whether to reconfigure.
 *----------------------------------------------------------------------------*/
struct spi_config;
struct device;

/** SPI slave controller, or NULL before a successful wifi_8711_init(). */
const struct device *wifi_8711_spi_dev(void);

/** The validated slave-mode config (mode 3, 8 bit, MSB first). */
const struct spi_config *wifi_8711_spi_cfg(void);

/**
 * @brief  W2B edge counters, for bring-up.
 *
 * @param  rising   out: rising edges seen (8711 announced a slot)
 * @param  falling  out: falling edges seen (8711 released after a transfer)
 *
 * Non-zero counters prove the pad, the pull-down and the interrupt path all
 * work even before a single byte has been clocked.
 */
void wifi_8711_get_w2b_stats(uint32_t *rising, uint32_t *falling);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_8711_H_ */
