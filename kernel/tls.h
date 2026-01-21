/*
 * KikiOS TLS
 *
 * Simple TLS client API wrapping TLSe
 */

#ifndef TLS_H
#define TLS_H

#include <stdint.h>
#include <stddef.h>

// Initialize TLS library
void tls_init_lib(void);

// Connect to a TLS server
// ip: destination IP address (network byte order handled internally)
// port: destination port (443 for HTTPS)
// hostname: server name for SNI (can be NULL)
// Returns: socket handle (>=0) or -1 on error
int tls_connect(uint32_t ip, uint16_t port, const char *hostname);

// Send data over TLS connection
// Returns: bytes sent, or -1 on error
int tls_send(int sock, const void *data, uint32_t len);

// Receive data over TLS connection
// Returns: bytes received, 0 if no data yet, -1 on error/closed
int tls_recv(int sock, void *buf, uint32_t maxlen);

// Close TLS connection
void tls_close(int sock);

// Check if TLS socket is connected
int tls_is_connected(int sock);

#endif
