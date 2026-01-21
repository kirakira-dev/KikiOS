/* KikiOS arpa/inet.h stub for TLSe */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <stdint.h>

// ARM is little-endian, network is big-endian
static inline uint16_t htons(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t x) {
    return htons(x);
}

static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

static inline uint32_t ntohl(uint32_t x) {
    return htonl(x);
}

#endif
