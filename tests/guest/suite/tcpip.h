/* tcpip.h — MacTCP for the network sections, as MacPerl's GUSI used it:
 * the .IPP driver and the resolver (the 'dnrp' code resource MacTCP or
 * Open Transport installs). Calls are synchronous; timeouts in seconds;
 * addresses as 32-bit numbers (a.b.c.d is a << 24 | ...). */

#ifndef TCPIP_H
#define TCPIP_H

#include <MacTCP.h>

OSErr ip_open(void);

OSErr dns_open(void);
void dns_close(void);
OSErr dns_resolve(char *name, unsigned long *addr);

OSErr tcp_connect(unsigned long host, unsigned short port, int timeout, StreamPtr *stream);
OSErr tcp_write(StreamPtr stream, const void *data, unsigned short length);
/* Reads until EOF, max bytes or timeout seconds; the bytes read. */
long tcp_read(StreamPtr stream, char *buf, long max, int timeout);
void tcp_close(StreamPtr stream);

OSErr udp_open(StreamPtr *stream);
OSErr udp_send(StreamPtr stream, unsigned long host, unsigned short port, const void *data,
               unsigned short length);
OSErr udp_recv(StreamPtr stream, char *buf, int max, int timeout, int *length);
void udp_close(StreamPtr stream);

#endif
