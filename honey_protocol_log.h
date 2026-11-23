#ifndef _HONEY_PROTOCOL_LOG_H_
#define _HONEY_PROTOCOL_LOG_H_

#include <stdio.h>

/* HONEY_PROTOCOL_LOG_ENABLE  1 / 0  (default 1) */
#ifndef HONEY_PROTOCOL_LOG_ENABLE
#define HONEY_PROTOCOL_LOG_ENABLE  1
#endif

#if HONEY_PROTOCOL_LOG_ENABLE
#define HONEY_PROTOCOL_LOG(fmt, ...)  printf("[HONEY_PROTOCOL] " fmt "\r\n", ##__VA_ARGS__)

/* Dump up to HONEY_PROTOCOL_LOG_HEX_MAX bytes of a buffer, hex, space-separated.
 * Truncates with a trailing "..." when longer. */
#ifndef HONEY_PROTOCOL_LOG_HEX_MAX
#define HONEY_PROTOCOL_LOG_HEX_MAX  32
#endif
#define HONEY_PROTOCOL_LOG_HEX(prefix, buf, buflen) do {                             \
        const uint8_t *_b = (const uint8_t *)(buf);                         \
        uint16_t _n = (uint16_t)(buflen);                                   \
        uint16_t _cap = (_n > HONEY_PROTOCOL_LOG_HEX_MAX) ? HONEY_PROTOCOL_LOG_HEX_MAX : _n;  \
        printf("[HONEY_PROTOCOL] " prefix " len=%u data=", (unsigned)_n);            \
        for (uint16_t _i = 0; _i < _cap; _i++) { printf("%02X ", _b[_i]); } \
        printf("%s\r\n", (_n > HONEY_PROTOCOL_LOG_HEX_MAX) ? "..." : "");            \
    } while (0)
#else
#define HONEY_PROTOCOL_LOG(fmt, ...)             do {} while(0)
#define HONEY_PROTOCOL_LOG_HEX(prefix, buf, len) do {} while(0)
#endif

#endif /* _HONEY_PROTOCOL_LOG_H_ */
