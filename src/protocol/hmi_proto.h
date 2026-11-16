/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _HMI_PROTO_H_
#define _HMI_PROTO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*============================================================================*
 *                      HoneyProtocol L1 -- framing layer
 *
 *  Spec: https://realmcu.github.io/HoneyProtocol/  section 2.3
 *
 *  This module owns the only L1 implementation in the firmware: it turns the
 *  byte stream arriving on 0xFFC1 into validated L2 frames, and it serialises
 *  outbound L2 frames into the 8-byte L1 header.
 *
 *  It deliberately contains no task, no queue and no heap allocation: frames
 *  are reassembled into one static buffer and handed to the L2 router as a
 *  borrowed pointer, so the streaming path keeps its single copy into the
 *  transport buffer.
 *============================================================================*/

#define PROTO_L1_MAGIC          0xABu
#define PROTO_L1_VERSION        0x00u
#define PROTO_L1_HDR_LEN        8u
#define PROTO_L1_MAX_PAYLOAD    248u
#define PROTO_L1_MAX_FRAME      (PROTO_L1_HDR_LEN + PROTO_L1_MAX_PAYLOAD)

/* Control byte: [7] reserved  [6] ack request  [5] error  [4] ack  [3:0] ver */
#define PROTO_CTRL_DATA         0x00u
#define PROTO_CTRL_ACK          0x10u
#define PROTO_CTRL_ERROR        0x30u
#define PROTO_CTRL_BIT_ERR      0x20u
#define PROTO_CTRL_BIT_ACK      0x10u
#define PROTO_CTRL_ACK_REQ      0x40u

/* L2 header: cmd(1) ver/rsv(1) key(1) len(2, BE) value(n) */
#define PROTO_L2_HDR_LEN        5u
#define PROTO_L2_MAX_VALUE      (PROTO_L1_MAX_PAYLOAD - PROTO_L2_HDR_LEN)

/** Receives one complete L2 frame. The pointer is borrowed: it stays valid
 *  only for the duration of the call. */
typedef void (*proto_l2_sink_t)(const uint8_t *l2, uint16_t len);

/** Puts raw bytes on the air (0xFFC2 notify). Best effort: a notification that
 *  the stack rejects is simply lost, exactly like a dropped ATT packet. */
typedef void (*proto_tx_fn_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Bind the transport sink and the L2 router. Call once at start-up.
 */
void proto_init(proto_tx_fn_t tx, proto_l2_sink_t sink);

/**
 * @brief Feed a raw byte stream as received on 0xFFC1.
 *
 * Accepts a partial frame, a whole frame, or several frames back to back, and
 * answers ACKs for peers that requested one. Never blocks.
 */
void proto_rx_feed(const uint8_t *data, uint16_t len);

/**
 * @brief Serialise an L2 frame (header + payload) inside one L1 frame.
 *
 * @param cmd/key/value  L2 frame to send.
 * @param out/out_cap    Destination buffer, at least PROTO_L1_MAX_FRAME.
 * @return Frame length, or 0 when the arguments do not fit.
 */
uint16_t proto_l2_build(uint8_t cmd, uint8_t version, uint8_t key,
                        const uint8_t *value, uint16_t len,
                        uint8_t *out, uint16_t out_cap);

/** Build and send one L2 frame. Returns false when it does not fit or the
 *  transport is not bound. */
bool proto_send_l2(uint8_t cmd, uint8_t version, uint8_t key,
                   const uint8_t *value, uint16_t len);

/** CRC-16/ARC (poly 0x8005 reflected as 0xA001, init 0). Covers the payload
 *  only; "123456789" must yield 0xBB3D. */
uint16_t proto_crc16(const uint8_t *data, uint16_t len);

/** @brief Verify the CRC implementation against the spec's check value. */
bool proto_crc_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_PROTO_H_ */
