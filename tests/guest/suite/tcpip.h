/* tcpip.h — MacTCP for the network sections, as MacPerl's GUSI used it:
 * the .IPP driver and the resolver (the 'dnrp' code resource MacTCP or
 * Open Transport installs). Calls are synchronous; timeouts in seconds;
 * addresses as 32-bit numbers (a.b.c.d is a << 24 | ...). */

#ifndef TCPIP_H
#define TCPIP_H

#include <MacTCP.h>

OSErr ip_open(void);
short ip_refnum(void); /* .IPP's, after ip_open */

/* The section's echo service: net-bridge's (10.0.2.1:7) on the ROM
 * machines, or the one Host:net_echo.txt names ("a.b.c.d port"); named:
 * 1 when the file named one (it must answer). */
void echo_service(unsigned long *host, unsigned short *port, int *named);

OSErr dns_open(void);
void dns_close(void);
OSErr dns_resolve(char *name, unsigned long *addr);
/* AddrToName (cname, up to 255 bytes), AddrToStr; HInfo and MXInfo: the
 * first record's text ("cpu/os", "preference exchange"). Lookups that ask
 * a server are waited for (timeout seconds). */
OSErr dns_addr_to_name(unsigned long addr, char *name, int timeout);
OSErr dns_addr_to_str(unsigned long addr, char *str);
OSErr dns_hinfo(char *name, char *out, int timeout);
OSErr dns_mx(char *name, char *out, int timeout);

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
