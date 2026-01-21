/*
 * FTP Server for KikiOS
 * 
 * Implements FTP protocol (RFC 959) server
 * Supports file upload/download, directory listing
 */

#ifndef _FTP_H
#define _FTP_H

#include <stdint.h>

// FTP server functions
void ftp_init(void);
void ftp_start(uint16_t port);
void ftp_stop(void);
int ftp_is_running(void);
void ftp_poll(void);  // Call from main loop to process connections

#endif // _FTP_H
