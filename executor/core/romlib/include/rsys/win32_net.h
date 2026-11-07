#pragma once
/*
 * win32_net.h - the BSD socket API's types and names hostnet.h and its
 * users (MacTCP, Open Transport) are written against, on Windows
 * (MacPhoenix): Winsock's, and the POSIX ones Winsock lacks (iovec,
 * msghdr, ifaddrs, the resolver's record types). struct pollfd is POSIX's
 * (an int descriptor), not Winsock's WSAPOLLFD (a SOCKET): a Winsock
 * backend converts. The Windows build's backend is none for now
 * (EXECUTOR_NET=none): these only have to compile.
 */
#ifdef _WIN32

#define pollfd win32_wsapollfd
#include <winsock2.h>
#include <ws2tcpip.h>
#undef pollfd
#undef pascal  /* minwindef.h: a calling convention; a name in Executor */
#include <sys/types.h>

struct pollfd
{
    int fd;
    short events;
    short revents;
};

struct iovec
{
    void *iov_base;
    size_t iov_len;
};

struct msghdr
{
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

struct ifaddrs
{
    struct ifaddrs *ifa_next;
    char *ifa_name;
    unsigned int ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_dstaddr;
    void *ifa_data;
};

typedef unsigned long nfds_t;

#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#ifndef MSG_TRUNC
#define MSG_TRUNC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* <arpa/nameser.h>'s classes and record types (RFC 1035, RFC 3596) */
enum
{
    ns_c_in = 1
};
enum
{
    ns_t_a = 1,
    ns_t_ns = 2,
    ns_t_cname = 5,
    ns_t_soa = 6,
    ns_t_ptr = 12,
    ns_t_hinfo = 13,
    ns_t_mx = 15,
    ns_t_txt = 16,
    ns_t_aaaa = 28
};

#endif
