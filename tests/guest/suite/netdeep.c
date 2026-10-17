/* netdeep.c — the deeper network section (tests/test_net_suite.sh): the
 * data paths net-bridge's NAT stresses, beyond the gateway echo. External
 * HTTP with body-length integrity, 20 sequential connects, external UDP
 * (a DNS query to 8.8.8.8), 64 KB from the bridge's bulk server on
 * gateway:8 (byte i = i & 0xff: flow control against the Mac's receive
 * window), dead-host connect timing, and that the NAT still works after it. */

#include "suite.h"
#include "tcpip.h"

#include <Events.h>
#include <Memory.h>
#include <stdio.h>
#include <string.h>

#define CRLF "\015\012"                         /* bytes: MPW C swaps \r and \n */

enum { kGateway = 10UL << 24 | 2 << 8 | 1, kBulkPort = 8, kBulkBytes = 65536 };

static const char request[] = "GET / HTTP/1.0" CRLF "Host: example.com" CRLF "Connection: close" CRLF CRLF;

static unsigned long http_host;

static long seconds(void)
{
    return TickCount() / 60;
}

static void fail_err(const char *name, const char *what, OSErr err)
{
    char detail[48];

    sprintf(detail, "%s err=%d", what, err);
    report_fail(name, detail);
}

static void http_get_integrity(char *resp)
{
    char name[160], *sep, *line, *body;
    long got, i, body_len, cl = -1;
    int status;
    StreamPtr s;
    OSErr err;

    if((err = dns_open()) != noErr || (err = dns_resolve("example.com", &http_host)) != noErr)
    {
        fail_err("http_dns", "resolve", err);
        http_host = 0;
        return;
    }
    report_pass("http_dns");
    if((err = tcp_connect(http_host, 80, 20, &s)) != noErr)
    {
        fail_err("http_connect", "", err);
        return;
    }
    report_pass("http_connect");
    if((err = tcp_write(s, request, sizeof request - 1)) != noErr)
    {
        fail_err("http_write", "", err);
        tcp_close(s);
        return;
    }
    got = tcp_read(s, resp, 65536, 15);
    tcp_close(s);
    if(got == 0)
    {
        report_fail("http_read", "no data");
        return;
    }
    sprintf(name, "http_read_%ld_bytes", got);
    report_pass(name);

    strcpy(name, "http_tail_hex_");             /* where the stream ended */
    for(i = got > 60 ? got - 60 : 0; i < got; i++)
        sprintf(name + strlen(name), "%02x", (unsigned char)resp[i]);
    report_pass(name);

    resp[got] = 0;
    if((sep = strstr(resp, CRLF CRLF)) == NULL)
    {
        report_fail("http_headers", "no blank line separator");
        return;
    }
    *sep = 0;
    body = sep + 4;
    body_len = got - (body - resp);

    for(line = resp; line; line = strstr(line, CRLF) ? strstr(line, CRLF) + 2 : NULL)
        if(sscanf(line, "Content-Length: %ld", &cl) == 1 || sscanf(line, "content-length: %ld", &cl) == 1)
            break;
    if(cl < 0)
        report_skip("http_content_length", "header absent (chunked or close)");
    else if(cl == body_len)
    {
        sprintf(name, "http_content_length_%ld", cl);
        report_pass(name);
    }
    else
    {
        sprintf(name, "header=%ld actual=%ld delta=%ld", cl, body_len, body_len - cl);
        report_fail("http_content_length", name);
    }

    if(sscanf(resp, "HTTP/1.%*d %d", &status) == 1)
    {
        sprintf(name, status >= 200 && status < 400 ? "http_status_%d" : "got %d", status);
        if(status >= 200 && status < 400)
            report_pass(name);
        else
            report_fail("http_status", name);
    }
    else
        report_fail("http_status", "no status line");

    if(body_len >= 200)
    {
        sprintf(name, "http_body_%ld_bytes", body_len);
        report_pass(name);
    }
    else
    {
        sprintf(name, "only %ld bytes", body_len);
        report_fail("http_body_short", name);
    }
}

static void sequential_connects(void)
{
    enum { N = 20 };
    char first[48] = "", head[65], detail[96];
    long t0 = seconds();
    int i, ok = 0;
    long n;
    StreamPtr s;
    OSErr err;

    if(!http_host)
    {
        report_skip("seq_connects", "no DNS");
        return;
    }
    for(i = 0; i < N; i++)
    {
        if((err = tcp_connect(http_host, 80, 20, &s)) != noErr)
        {
            if(!first[0])
                sprintf(first, "#%d connect=%d", i, err);
            continue;
        }
        if((err = tcp_write(s, request, sizeof request - 1)) != noErr)
        {
            if(!first[0])
                sprintf(first, "#%d write=%d", i, err);
            tcp_close(s);
            continue;
        }
        n = tcp_read(s, head, 64, 5);
        tcp_close(s);
        if(n >= 5 && memcmp(head, "HTTP/", 5) == 0)
            ok++;
        else if(!first[0])
            sprintf(first, "#%d short-response", i);
    }
    if(ok == N)
    {
        sprintf(detail, "seq_connects_%d_of_%d_in_%lds", N, N, seconds() - t0);
        report_pass(detail);
    }
    else
    {
        sprintf(detail, "only %d/%d ok in %lds (first=%s)", ok, N, seconds() - t0, first);
        report_fail("seq_connects", detail);
    }
}

static void external_udp_dns(void)
{
    static const unsigned char query[] = {
        0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,         /* txid, RD, 1 question */
        7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0,
        0, 1, 0, 1                                              /* A, IN */
    };
    unsigned char reply[512];
    char name[48];
    int n, an;
    StreamPtr s;
    OSErr err;

    if((err = udp_open(&s)) != noErr)
    {
        fail_err("udp_ext_socket", "", err);
        return;
    }
    if((err = udp_send(s, 8UL << 24 | 8UL << 16 | 8 << 8 | 8, 53, query, sizeof query)) != noErr)
    {
        fail_err("udp_ext_send", "", err);
        udp_close(s);
        return;
    }
    report_pass("udp_ext_send");
    err = udp_recv(s, (char *)reply, sizeof reply, 5, &n);
    udp_close(s);
    if(err != noErr || n < 12)
    {
        report_fail("udp_ext_recv", err == commandTimeout ? "no reply within 5s" : "short or no reply");
        return;
    }
    sprintf(name, "udp_ext_recv_%d_bytes", n);
    report_pass(name);
    if(reply[0] != 0x12 || reply[1] != 0x34)
    {
        sprintf(name, "sent=0x1234 got=0x%02x%02x", reply[0], reply[1]);
        report_fail("udp_ext_txid", name);
        return;
    }
    report_pass("udp_ext_txid");
    an = reply[6] << 8 | reply[7];
    if(an > 0)
    {
        sprintf(name, "udp_ext_answers_%d", an);
        report_pass(name);
    }
    else
    {
        sprintf(name, "none (rcode=%d)", reply[3] & 15);
        report_fail("udp_ext_answers", name);
    }
}

static void bulk_tcp_transfer(char *data)
{
    char name[96];
    long got, i;
    StreamPtr s;
    OSErr err;

    if((err = tcp_connect(kGateway, kBulkPort, 20, &s)) != noErr)
    {
        fail_err("bulk_connect", "", err);
        return;
    }
    report_pass("bulk_connect");
    got = tcp_read(s, data, kBulkBytes + 4096, 30);
    tcp_close(s);
    if(got == 0)
    {
        report_fail("bulk_read", "no data");
        return;
    }
    sprintf(name, "bulk_received_%ld_bytes", got);
    report_pass(name);
    if(got != kBulkBytes)
    {
        sprintf(name, "expected %d got %ld (delta=%ld)", kBulkBytes, got, kBulkBytes - got);
        report_fail("bulk_length", name);
    }
    else
        report_pass("bulk_length_exactly_65536");
    for(i = 0; i < got; i++)
        if((unsigned char)data[i] != (i & 255))
            break;
    if(i == got)
        report_pass("bulk_pattern_intact");
    else
    {
        sprintf(name, "first mismatch at offset %ld (expected %ld, got %d)", i, i & 255,
                (unsigned char)data[i]);
        report_fail("bulk_pattern", name);
    }
}

/* TEST-NET-1 is not routable: the connect must fail, and how fast says
 * whether the NAT blocked its hot path. */
static void dead_host_timing(void)
{
    char name[64];
    long t0 = seconds(), elapsed;
    StreamPtr s;
    OSErr err;

    err = tcp_connect(192UL << 24 | 2 << 8 | 1, 80, 30, &s);
    elapsed = seconds() - t0;
    if(err == noErr)
    {
        tcp_close(s);
        sprintf(name, "connected in %lds", elapsed);
        report_fail("dead_unexpected_success", name);
        return;
    }
    if(elapsed <= 10)
    {
        sprintf(name, elapsed <= 2 ? "dead_connect_fast_%lds" : "dead_connect_ok_%lds", elapsed);
        report_pass(name);
    }
    else
    {
        sprintf(name, "%lds", elapsed);
        report_fail(elapsed <= 15 ? "dead_connect_slow" : "dead_connect_hung", name);
    }
}

static void post_stall_liveness(void)
{
    char name[64];
    long t0 = seconds();
    StreamPtr s;
    OSErr err;

    if(!http_host)
    {
        report_skip("post_stall", "no DNS");
        return;
    }
    if((err = tcp_connect(http_host, 80, 20, &s)) != noErr)
    {
        sprintf(name, "err=%d after %lds", err, seconds() - t0);
        report_fail("post_stall_connect", name);
        return;
    }
    tcp_close(s);
    sprintf(name, "post_stall_connect_%lds", seconds() - t0);
    report_pass(name);
}

void test_network_deep(void)
{
    Ptr buf;
    OSErr err;

    if((err = ip_open()) != noErr)
    {
        fail_err("net_mactcp", "OpenDriver .IPP", err);
        return;
    }
    if((buf = NewPtr(kBulkBytes + 4096 + 1)) == NULL)
    {
        report_fail("net_memory", "no buffer");
        return;
    }
    http_get_integrity(buf);
    sequential_connects();
    external_udp_dns();
    bulk_tcp_transfer(buf);
    dead_host_timing();
    post_stall_liveness();
    DisposePtr(buf);
}
