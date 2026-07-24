#ifndef _PROTO_LOG_H_
#define _PROTO_LOG_H_

#include <stdio.h>

/* PROTO_LOG_ENABLE  1 / 0  (default 1) */
#ifndef PROTO_LOG_ENABLE
#define PROTO_LOG_ENABLE  1
#endif

#if PROTO_LOG_ENABLE
#define PROTO_LOG(fmt, ...)  printf("[PROTO] " fmt "\r\n", ##__VA_ARGS__)

/* Dump up to PROTO_LOG_HEX_MAX bytes of a buffer, hex, space-separated.
 * Truncates with a trailing "..." when longer. */
#ifndef PROTO_LOG_HEX_MAX
#define PROTO_LOG_HEX_MAX  32
#endif
#define PROTO_LOG_HEX(prefix, buf, buflen) do {                             \
        const uint8_t *_b = (const uint8_t *)(buf);                         \
        uint16_t _n = (uint16_t)(buflen);                                   \
        uint16_t _cap = (_n > PROTO_LOG_HEX_MAX) ? PROTO_LOG_HEX_MAX : _n;  \
        printf("[PROTO] " prefix " len=%u data=", (unsigned)_n);            \
        for (uint16_t _i = 0; _i < _cap; _i++) { printf("%02X ", _b[_i]); } \
        printf("%s\r\n", (_n > PROTO_LOG_HEX_MAX) ? "..." : "");            \
    } while (0)
#else
#define PROTO_LOG(fmt, ...)             do {} while(0)
#define PROTO_LOG_HEX(prefix, buf, len) do {} while(0)
#endif

#endif /* _PROTO_LOG_H_ */
