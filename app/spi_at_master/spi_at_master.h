/**
 * @file    spi_at_master.h
 * @brief   AT-over-SPI toward the RTL8711 -- WE ARE THE MASTER.
 *
 * SCOPE: the TRANSPORT only -- slots on the wire, the two sync lines, and the
 * bytes each way.  It has no idea what an AT command means.  The Wi-Fi state
 * machine, the SoftAP and the file/JPEG ingress live above it, in at_parser ->
 * at_client -> at_wifi / at_socket and then app/protocol; this file's callers
 * are those layers plus the `atm` shell.
 *
 * The protocol and the flow are taken from the vendor reference
 *   rtl8711faf/ameba-rtos/example/atcmd_host/atcmd_host_spi
 * rather than invented here, so that a disagreement with the 8711 side can be
 * settled by reading one file on each end.
 *
 * ---------------------------------------------------------------------------
 * ROLE, AND WHY THE CONTROLLER HAD TO CHANGE
 * ---------------------------------------------------------------------------
 * We drive SCLK and CS now; the 8711 does not.  That forced a different SPI
 * controller, which is worth knowing before wondering why the node name moved:
 *
 *   spi0_slave (the old role) carries `is-slave` and has no max-bus-freq at all
 *   -- the 8711 supplied the clock, so there was nothing to rate.
 *   spi0 / spi1 / spi2 are rated max-bus-freq = 20 MHz in rtl87x3g.dtsi.
 *   spi1_hs is rated 50 MHz.
 *
 * 25 MHz therefore has exactly one home on this SoC: spi1_hs.  Asking for
 * 25 MHz on any of the 20 MHz nodes is not a soft limit -- it is a hard -EINVAL
 * from spi_rtl87x3g_configure() (spi_rtl87x3g.c:506).
 *
 * The four bus PADS are unchanged (P4_2..P4_5).  Only the pinmux GROUP changes,
 * from the ordinary SPI_*_SLAVE functions to the HS_FUNC1 group, which is where
 * SPI1 master lives.  So "same wires, different controller" is the accurate
 * summary -- no board rework is implied by this module.
 *
 * ---------------------------------------------------------------------------
 * THE TWO SYNC LINES ARE REVERSED FROM THE SLAVE BUILD
 * ---------------------------------------------------------------------------
 * This is the single easiest thing to get backwards, because the pads kept
 * their numbers and swapped their directions:
 *
 *                  slave build (deleted)         THIS module
 *   P4_1  GPIOB8   B2W, our OUTPUT               S2M, our INPUT
 *   P2_7  GPIOA22  W2B, our INPUT                M2S, our OUTPUT
 *
 * The slave build is gone, but the hazard is not: its overlay is still one `git
 * show` away and still compiles perfectly against these pads while driving the
 * wrong pin in the wrong direction.  Nothing catches that at build time; the
 * names below are chosen to be relative-to-master (m2s/s2m) precisely so the
 * two cannot be confused in code.
 *
 * Idle levels, per the 8711 side and the reference:
 *
 *   M2S (out) idles HIGH, asserted LOW  = "prepare to receive"
 *   S2M (in)  idles LOW,  asserted HIGH = "ready" / "I want to send"
 *
 * In devicetree that is expressed as GPIO_ACTIVE_LOW on m2s-gpios, which is why
 * GPIO_OUTPUT_INACTIVE at init produces a HIGH pad and gpio_pin_set_dt(.., 1)
 * pulls it low.  Reading the raw numbers instead of the logical ones is the
 * usual way this gets misread.
 *
 * ---------------------------------------------------------------------------
 * TRANSACTION SHAPE -- S2M IS THE GO-AHEAD FOR *BOTH* DIRECTIONS
 * ---------------------------------------------------------------------------
 * The non-obvious part of the reference, and the part that makes the whole
 * thing work: the master's loop blocks on the S2M rising edge before EVERY
 * transfer, uplink or downlink.  M2S-low is only a REQUEST; it never starts a
 * transfer by itself.
 *
 *   uplink    we stage a frame -> assert M2S low -> 8711 arms and raises S2M
 *             -> we clock SPI_AT_SLOT_SIZE both ways -> release M2S high
 *   downlink  8711 raises S2M on its own -> we clock the same fixed slot,
 *             sending an invalid-magic dummy on MOSI -> release M2S high
 *
 * Both cases are the same code path with the same fixed-size full-duplex
 * transfer, which is why there is one transaction function and not two.
 *
 * ---------------------------------------------------------------------------
 * WHY THE TRANSFER IS SYNCHRONOUS HERE, UNLIKE THE SLAVE BUILD
 * ---------------------------------------------------------------------------
 * The slave transport had to split ISR and thread, because on this driver the
 * completion callback runs in the DMA ISR while the context lock is still held
 * (spi_context.h:206 releases it only after the callback returns), so re-arming
 * from the callback would k_sem_take(K_FOREVER) inside an ISR.
 *
 * As master that constraint evaporates: nothing has to be armed in advance, so
 * the thread can simply call the blocking spi_transceive() and the whole
 * re-arm hazard disappears.  This is the one place where being master is
 * genuinely simpler, and it is worth taking.
 *
 * ---------------------------------------------------------------------------
 * NEVER PASS A NULL BUFFER TO THIS DRIVER
 * ---------------------------------------------------------------------------
 * spi_rtl87x3g.c:324/335 k_malloc()s a whole chunk-sized bounce buffer whenever
 * tx_buf or rx_buf is NULL, and frees it after.  At a 16 KiB slot that is a
 * 16 KiB malloc per transfer against a 2 KiB k_malloc arena -- it fails with
 * -ENOMEM rather than degrading.  So both directions always point at real
 * storage, and a downlink-only slot sends a dummy frame instead of a NULL TX.
 *
 * ---------------------------------------------------------------------------
 * THIS WAS A SECOND MODULE ONCE.  NOW IT IS THE ONLY ONE
 * ---------------------------------------------------------------------------
 * It started beside app/wifi_8711 rather than inside it: that stack was working
 * code with its own protocol (ATMC/JPGS/EBFS, 4096 B slots, CRC32, sequence
 * de-dup), and converting it in place would have left neither role intact while
 * the new one was unproven.  The two were mutually exclusive by construction --
 * `depends on !WIFI_8711`, both claiming P4_2..P4_5 against the BUILD_ASSERT at
 * spi_rtl87x3g.c:961 that spi1 and spi1_hs are not both okay, and one snippet
 * each so you passed one -S or the other.
 *
 * The master role is proven now, so the slave stack and both snippets were
 * deleted and the exclusivity went with them: CONFIG_WIFI_8711 `select`s
 * SPI_AT_MASTER instead of forbidding it, and there is one snippet.  Read those
 * three mechanisms as history if a git bisect lands before the deletion.
 *
 * What survived is the reason it was done by Kconfig rather than by deleting
 * call sites: every caller of the AT layer in app/protocol (port_softap,
 * port_tcp, jpgs_ingress, ebfs_ingress, cmd_debug) is wrapped in
 * #if defined(CONFIG_WIFI_8711) with a working #else stub, so building without
 * the wifi snippet still parks all of them on their stubs with ZERO edits to
 * protocol code.  That is how "pause the Wi-Fi features" is implemented.
 */
#ifndef _SPI_AT_MASTER_H_
#define _SPI_AT_MASTER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*
 *  Frame geometry  (reference: example_atcmd_host_spi.c:18-27, :330-338)
 *
 *  Every transfer is exactly SPI_AT_SLOT_SIZE bytes on the wire in both
 *  directions regardless of payload -- the length field bounds what the peer
 *  READS, not what gets clocked.  Trailing bytes are don't-care.
 *
 *      offset 0   0x41 'A'
 *      offset 1   0x54 'T'
 *      offset 2   payload length, uint16 LITTLE-endian
 *      offset 4   payload
 *----------------------------------------------------------------------------*/
#define SPI_AT_SLOT_SIZE        16384U      /* == reference ATCMD_SPI_DMA_SIZE */
#define SPI_AT_HDR_SIZE         4U

/** The reference reserves 8 bytes, not 4: 4 for the header above plus 4 for a
 *  trailing checksum that its CHECKSUM_EN is 0 for.  The reservation is kept
 *  byte-for-byte so both ends agree on the maximum payload even though neither
 *  computes the checksum today -- shrinking it here would silently let us send
 *  a frame the 8711 rejects on length the day someone enables it there. */
#define SPI_AT_CHKSUM_RSVD      4U
#define SPI_AT_PAYLOAD_MAX      (SPI_AT_SLOT_SIZE - SPI_AT_HDR_SIZE - \
                                 SPI_AT_CHKSUM_RSVD)     /* 16376 */

#define SPI_AT_MAGIC_0          0x41U       /* 'A' */
#define SPI_AT_MAGIC_1          0x54U       /* 'T' */

/** First byte of a downlink-only dummy frame.  Any value that is not 'A' works;
 *  the point is that the 8711 must not mistake an empty uplink for a command.
 *  Only byte 0 is stamped -- the peer bounds its read by the length field, so
 *  zeroing 16 KiB per idle slot would cost real time for no effect. */
#define SPI_AT_DUMMY_MAGIC      0xFFU

/** SPI mode, as the reference configures it (SPI_MODE 0 = CPOL 0, CPHA 0).
 *  The old slave build ran mode 3.  Both ends must agree; a mismatch shows up
 *  as an all-0x00/all-0xFF RX rather than as an error, so this is the first
 *  thing to check when nothing decodes. */
#define SPI_AT_SPI_MODE_CPOL    0
#define SPI_AT_SPI_MODE_CPHA    0

/** Bus clock.  25 MHz per the hardware brief; the reference asks for 20 MHz.
 *  Only spi1_hs can carry this -- see the controller note in the file header. */
#define SPI_AT_SCLK_HZ          25000000U

/** How long we wait for the 8711 to answer an M2S assert with an S2M rise.
 *  1000 ms matches the peer's own READY budget, so a timeout here means the
 *  8711 really is not participating rather than merely being slow. */
#define SPI_AT_READY_TIMEOUT_MS 1000U

/*----------------------------------------------------------------------------*
 *  Counters
 *
 *  Split by failure mode rather than merged into one error count, because each
 *  line points at a different half of the link.
 *----------------------------------------------------------------------------*/
typedef struct
{
    uint32_t tx_frames;      /**< uplink frames actually clocked out          */
    uint32_t rx_frames;      /**< slots received with a valid header          */
    uint32_t rx_empty;       /**< valid header, length 0 (a pure ack slot)    */
    uint32_t rx_bad_magic;   /**< first two bytes were not 'A','T'            */
    uint32_t rx_bad_len;     /**< header ok, length past SPI_AT_PAYLOAD_MAX   */
    uint32_t xfer_err;       /**< spi_transceive() returned negative          */
    uint32_t ready_timeout;  /**< asserted M2S, no S2M rise within the budget */
    uint32_t s2m_edges;      /**< S2M rising edges seen by the ISR            */
    uint32_t slots;          /**< transactions completed, both directions     */
    int32_t  last_result;    /**< raw return of the last spi_transceive()     */
} spi_at_master_stats_t;

/**
 * @brief  Sink for a received payload, called on the transport thread.
 *
 * @param  payload  points into the RX slot -- valid only for this call
 * @param  len      0..SPI_AT_PAYLOAD_MAX
 *
 * Not an ISR context, so logging and locks are fine.  It must still return
 * promptly: M2S stays asserted for its duration, which back-pressures the peer.
 */
typedef void (*spi_at_master_rx_cb_t)(const uint8_t *payload, uint16_t len);

/**
 * @brief  Resolve the DT nodes, configure the bus and both sync lines, start
 *         the transport thread.
 *
 * Leaves M2S released (HIGH) and the S2M rising-edge interrupt armed.  If S2M
 * is ALREADY high at this point -- the 8711 booted first and is waiting -- the
 * edge is gone forever, so init synthesises one rather than deadlocking on it.
 *
 * @retval 0        ready
 * @retval -ENODEV  bus or a GPIO port did not come up
 * @retval <0       driver errno from gpio/spi configuration
 */
int spi_at_master_init(void);

/** True once spi_at_master_init() has succeeded. */
bool spi_at_master_ready(void);

/**
 * @brief  Send one AT payload: stage it, then assert M2S to request the slot.
 *
 * Returns as soon as the frame is staged -- it goes out when the 8711 answers
 * with S2M.  Use spi_at_master_wait_rx() to wait for the reply.
 *
 * If the 8711 never answers, the request is abandoned after
 * SPI_AT_READY_TIMEOUT_MS: M2S is released, the staged frame is dropped and
 * ready_timeout is bumped.  So -EBUSY is always transient -- it never latches
 * the harness into a state only a reboot clears.
 *
 * Only one frame may be in flight; this is a bring-up harness and a queue would
 * hide ordering bugs rather than fix them.
 *
 * @param  payload  bytes to place after the header (CRLF included if the peer
 *                  wants it -- this layer appends nothing)
 * @param  len      1..SPI_AT_PAYLOAD_MAX
 * @retval 0          staged, M2S asserted
 * @retval -ENODEV    not initialised
 * @retval -EINVAL    NULL payload or len 0
 * @retval -EMSGSIZE  len > SPI_AT_PAYLOAD_MAX
 * @retval -EBUSY     a frame is already staged and not yet sent
 */
int spi_at_master_send(const uint8_t *payload, uint16_t len);

/**
 * @brief  spi_at_master_send() for a text command, appending CRLF if absent.
 *
 * The 8711's line parser terminates on CRLF, so a command typed without one
 * would be accepted here and then sit unparsed at the far end -- a silent
 * failure that looks like a dead link.  Hence the fix-up lives in the sender.
 */
int spi_at_master_send_text(const char *text);

/**
 * @brief  Install the payload sink.  NULL only counts.
 *
 * The hexdump is independent of this and stays on regardless, because during
 * bring-up the bytes are the answer and a parser is not required to see them.
 */
void spi_at_master_set_rx_cb(spi_at_master_rx_cb_t cb);

/**
 * @brief  Block until a slot with a valid header arrives.
 *
 * @retval 0        a slot arrived
 * @retval -EAGAIN  nothing in time -- the expected result against a peer whose
 *                  AT firmware is not running
 */
int spi_at_master_wait_rx(uint32_t timeout_ms);

/**
 * @brief  Hexdump control, runtime rather than compile-time.
 *
 * A dump runs on the transport thread with M2S still asserted, so it throttles
 * the peer.  Harmless while typing single commands, not harmless under a burst
 * -- being able to stop it without a reflash is the point.
 */
void spi_at_master_set_dump(bool tx_on, bool rx_on);
void spi_at_master_get_dump(bool *tx_on, bool *rx_on);

/** Dump size threshold, both directions.  At or below it a payload is dumped
 *  WHOLE; above it nothing is dumped but the length (see ebadge_log_hexdump --
 *  `max` is a threshold there, not a truncation point).
 *
 *  One number for both directions now, where there used to be two.  The old pair
 *  existed because dumps were head-truncated, so the outbound side wanted a
 *  smaller head to keep the pre-transceive delay short.  Under an all-or-nothing
 *  rule that reason is gone: the only payloads we ever send are AT command lines
 *  of tens of bytes, which land far under this and so now print whole instead of
 *  clipped at 256 -- strictly more of the direction than before.
 *
 *  1024 is chosen for what it EXCLUDES.  It keeps an AT reply intact while
 *  silencing bulk traffic: a JPEG frame or a file body from port 9000
 *  crosses back-to-back 16 KiB slots, and dumping those is both useless (image
 *  middles) and destructive (it scrolls the AT lines away).  A 1024 B dump is 64
 *  rows, ~5 KB of console, about 27 ms at the board's 2 Mbaud console (uart2).
 *  The dump runs on the transport thread with M2S still asserted (see
 *  spi_at_master_set_dump), so that delay throttles the peer -- 27 ms is well
 *  inside the 1000 ms READY budget, whereas a whole 16 KiB slot would be ~430 ms
 *  and start eating into it.  That is the ceiling this number stays under.
 *
 *  It was 1536.  Lowered because nothing we actually read is anywhere near either
 *  figure -- the longest AT reply is a WLSTATE block of a few hundred bytes -- so
 *  the headroom was buying console time rather than visibility.
 *
 *  NOT a buffer size anywhere: ebadge_log_hexdump() formats one 16-byte row at a
 *  time into an 80-byte stack buffer, so this costs console time only. */
#define SPI_AT_DUMP_MAX         1024U

/** Longest command line the text helpers accept.
 *
 *  A stack-buffer size in spi_at_master_send_text(), which runs on the CALLER's
 *  thread -- usually the shell.  Sizing it for a 16 KiB payload would push that
 *  limit onto whoever calls in, so the cap lives here.  Uplink is AT command
 *  lines of tens of bytes, so 256 is already generous.
 *
 *  This used to be SPI_AT_DUMP_MAX doing double duty as both a dump cap and this
 *  buffer size.  Two unrelated constraints on one number: separated when the
 *  dump rule changed, because otherwise raising the dump threshold silently
 *  grows a stack frame on somebody else's thread. */
#define SPI_AT_TEXT_MAX         256U

void spi_at_master_get_stats(spi_at_master_stats_t *out);
void spi_at_master_reset_stats(void);

/** Logical levels of the two sync lines, for `atm info`.  Negative = not ready.
 *  m2s reads back what WE have asserted, not the pad -- the question a caller
 *  asks is "what have we told the 8711", which a loaded pad would not answer. */
int spi_at_master_get_m2s(void);
int spi_at_master_get_s2m(void);

#ifdef __cplusplus
}
#endif

#endif /* _SPI_AT_MASTER_H_ */
