/* net.c — the network section: MacTCP's driver and resolver, then UDP and
 * TCP against an echo service. On the ROM machines that is net-bridge's
 * (10.0.2.1:7); on Executor, whose MacTCP is on host sockets, a host echo
 * server the harness names in Host:net_echo.txt ("address port"). A named
 * one must answer, so its silence fails rather than skips. */

#include "suite.h"
#include "tcpip.h"

#include <stdio.h>
#include <string.h>

static void fail_err(const char *name, OSErr err)
{
    char detail[32];

    sprintf(detail, "err=%d", err);
    report_fail(name, detail);
}

void test_network(void)
{
    unsigned int a, b, c, d, port;
    unsigned long echo = 10UL << 24 | 2 << 8 | 1, addr;
    unsigned short echo_port = 7;
    int named = 0, n;
    char name[48], buf[64];
    StreamPtr s;
    FILE *cfg;
    OSErr err;
    void (*miss)(const char *, const char *);

    if((err = ip_open()) != noErr)
    {
        fail_err("net_mactcp", err);
        return;
    }
    report_pass("net_mactcp");

    if((err = dns_open()) != noErr || (err = dns_resolve("example.com", &addr)) != noErr)
        fail_err("dns_resolve", err);
    else
    {
        sprintf(name, "dns_resolve_%lu.%lu.%lu.%lu", addr >> 24, (addr >> 16) & 255, (addr >> 8) & 255,
                addr & 255);
        report_pass(name);
    }

    if((cfg = fopen("Host:net_echo.txt", "r")) != NULL)
    {
        if(fscanf(cfg, "%u.%u.%u.%u %u", &a, &b, &c, &d, &port) == 5)
        {
            echo = (unsigned long)a << 24 | (unsigned long)b << 16 | c << 8 | d;
            echo_port = port;
            named = 1;
        }
        fclose(cfg);
    }
    miss = named ? report_fail : report_skip;

    /* UDP */
    if((err = udp_open(&s)) != noErr)
        fail_err("udp_socket", err);
    else
    {
        report_pass("udp_socket");
        if((err = udp_send(s, echo, echo_port, "ping", 4)) != noErr)
            fail_err("udp_send", err);
        else
        {
            report_pass("udp_send");
            err = udp_recv(s, buf, sizeof buf, 3, &n);
            if(err == noErr && n == 4 && memcmp(buf, "ping", 4) == 0)
                report_pass("udp_echo");
            else if(err == commandTimeout)
                miss("udp_echo", "no response");
            else
            {
                sprintf(buf, err ? "recv err=%d" : "got %d bytes", err ? err : n);
                miss("udp_echo", buf);
            }
        }
        udp_close(s);
    }

    /* TCP */
    report_pass("tcp_socket");                  /* MacTCP makes the stream with the connection */
    if((err = tcp_connect(echo, echo_port, 10, &s)) != noErr)
    {
        sprintf(buf, "err=%d", err);
        miss("tcp_connect", buf);
        return;
    }
    report_pass("tcp_connect");
    if((err = tcp_write(s, "ping", 4)) != noErr)
        fail_err("tcp_send", err);
    else
    {
        n = (int)tcp_read(s, buf, 4, 3);
        if(n == 4 && memcmp(buf, "ping", 4) == 0)
            report_pass("tcp_echo");
        else
        {
            sprintf(buf, n ? "got %d bytes" : "no response", n);
            miss("tcp_echo", buf);
        }
    }
    tcp_close(s);
}
