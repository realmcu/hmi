#ifndef _PROTO_LOG_H_
#define _PROTO_LOG_H_

#include <stdio.h>

/* PROTO_LOG_ENABLE  1 / 0  (default 1) */
#ifndef PROTO_LOG_ENABLE
#define PROTO_LOG_ENABLE  1
#endif

#if PROTO_LOG_ENABLE
#define PROTO_LOG(fmt, ...)  printf("[PROTO] " fmt "\r\n", ##__VA_ARGS__)
#else
#define PROTO_LOG(fmt, ...)  do {} while(0)
#endif

#endif /* _PROTO_LOG_H_ */
