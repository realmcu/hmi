/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef __WIFI_DESC_H__
#define __WIFI_DESC_H__

#include "rtl876x.h"

// define transmit packat type
#define TX_PACKET_802_3         (0x83)
#define TX_PACKET_802_11        (0x81)
#define TX_H2C_CMD              (0x11)
#define TX_MEM_READ             (0x51)
#define TX_MEM_WRITE            (0x53)
#define TX_MEM_SET              (0x55)
#define TX_FM_FREETOGO          (0x61)
#define TX_PACKET_USER          (0x41)

//define receive packet type
#define RX_PACKET_802_3         (0x82)
#define RX_PACKET_802_11        (0x80)
#define RX_C2H_CMD              (0x10)
#define RX_MEM_READ             (0x50)
#define RX_MEM_WRITE            (0x52)
#define RX_MEM_SET              (0x54)
#define RX_FM_FREETOGO          (0x60)
#define RX_PACKET_USER          (0x40)

typedef struct
{
    uint32_t    ip_addr;
    uint16_t    port;
    uint8_t     seq;
    uint8_t     rsvd1;
    uint32_t    rsvd2;
    uint32_t    rsvd3;
} EXTDESC;

typedef struct
{
    // u4Byte 0
    uint32_t    txpktsize: 16;      // bit[15:0]
    uint32_t    offset: 8;          // bit[23:16], store the sizeof(TX_DESC)
    uint32_t    bus_agg_num: 8;     // bit[31:24], the bus aggregation number

    // u4Byte 1
    uint32_t type: 8;            // bit[7:0], the packet type
    uint32_t data: 8;            // bit[8:15], the value to be written to the memory
    uint32_t reply: 1;           // bit[16], request to send a reply message
    uint32_t rsvd0: 15;

    // u4Byte 2-4
    uint32_t    rsvd1;
    uint32_t    rsvd2;
    uint32_t    rsvd3;

    // u4Byte 5
    uint32_t    seq: 8;
    uint32_t    md: 8;
    uint32_t    rsvd5: 16;

    EXTDESC     ext_desc;

} TXDESC, *PTXDESC;

#define SIZE_TX_DESC    (sizeof(TXDESC))

typedef struct
{
    // u4Byte 0
    uint32_t    pkt_len: 16;    // bit[15:0], the packet size
    uint32_t    offset: 8; // bit[23:16], the offset from the packet start to the buf start
    uint32_t    rsvd0: 6;           // bit[29:24]
    uint32_t    icv: 1;         //icv error
    uint32_t    crc: 1;         // crc error

    // u4Byte 1
    /************************************************/
    /*****************receive packet type*********************/
    /*  0x82:   802.3 packet                              */
    /*  0x80:   802.11 packet                             */
    /*  0x10:   C2H command                       */
    /*  0x50:   Memory Read                           */
    /*  0x52:   Memory Write                              */
    /*  0x54:   Memory Set                            */
    /*  0x60:   Indicate the firmware is started              */
    uint32_t    type: 8;        // bit[7:0], the type of this packet
    uint32_t    rsvd1: 24;      // bit[31:8]

    // u4Byte 2-4
    uint32_t    rsvd2;
    uint32_t    rsvd3;
    uint32_t    rsvd4;

    // u4Byte 5
    uint32_t    seq: 8;
    uint32_t    rsvd5: 24;

    EXTDESC     ext_desc;
} RXDESC, *PRXDESC;

#define SIZE_RX_DESC    (sizeof(RXDESC))

#endif //__WIFI_DESC_H__
