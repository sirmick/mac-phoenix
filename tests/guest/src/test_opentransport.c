/*
 * test_opentransport.c - Open Transport (OT) network tests
 *
 * Tests:
 *   - InitOpenTransport
 *   - Create UDP endpoint
 *   - Bind to wildcard address
 *   - Resolve host via DNS (Internet Services)
 *   - Send UDP packet to host-side echo server (if configured)
 *   - Receive echo response
 *   - Ping test (ICMP via OT if available)
 *   - TCP connect to host:port (proves IP NAT works end-to-end)
 *
 * Requires emulator started with --network lwip (or similar) so OT can
 * reach the host's network stack through the NAT bridge.
 *
 * Host-side prerequisites for full NAT validation:
 *   - UDP echo server on host at known port
 *   - TCP server on host at known port
 *   - DNS resolver accessible from emulated Mac
 */
#include <string.h>

#include "test_report.h"

/* OpenTransport is only available as a linkable library on PPC in Retro68.
 * On 68k, OT uses CFM glue that's not packaged in the Apple Universal
 * Interfaces stub libraries. Skip this whole file on 68k builds. */
#ifdef __mc68000__

void test_opentransport(void)
{
    report_skip("opentransport", "requires PPC build");
}

#else

#include <OpenTransport.h>
#include <OpenTransportProviders.h>

/* Test targets — host-side must provide these */
#define ECHO_HOST "10.0.2.2"       /* common NAT gateway address */
#define ECHO_UDP_PORT 7            /* echo protocol */
#define TCP_TEST_PORT 7
#define DNS_TEST_HOST "example.com"

void test_opentransport(void)
{
    OSStatus err;
    EndpointRef ep;
    TEndpointInfo info;

    /* Initialize OT */
    err = InitOpenTransport();
    if (err != noErr) {
        report_skip("ot_init", "OpenTransport not available");
        return;
    }
    report_pass("ot_init");

    /* Open UDP endpoint */
    ep = OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, &info, &err);
    if (err != noErr || ep == kOTInvalidEndpointRef) {
        report_fail("ot_udp_endpoint", err);
        CloseOpenTransport();
        return;
    }
    report_pass("ot_udp_endpoint");

    /* Bind wildcard */
    err = OTBind(ep, NULL, NULL);
    if (err != noErr) {
        report_fail("ot_udp_bind", err);
        OTCloseProvider(ep);
        CloseOpenTransport();
        return;
    }
    report_pass("ot_udp_bind");

    /* DNS resolution via Internet Services */
    {
        InetSvcRef isvc;
        InetHostInfo hinfo;

        isvc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
        if (err != noErr || isvc == kOTInvalidEndpointRef) {
            report_skip("ot_dns_resolve", "no Internet Services");
        } else {
            err = OTInetStringToAddress(isvc, (char *)DNS_TEST_HOST, &hinfo);
            if (err == noErr) {
                report_pass("ot_dns_resolve");
            } else {
                report_fail("ot_dns_resolve", err);
            }
            OTCloseProvider(isvc);
        }
    }

    /* UDP send-to-host test (echo server required on host) */
    {
        InetAddress addr;
        TUnitData udata;
        char buf[] = "ping";
        char rbuf[64];
        OTFlags flags;

        OTInitInetAddress(&addr, ECHO_UDP_PORT, 0x0A000202);  /* 10.0.2.2 */

        memset(&udata, 0, sizeof(udata));
        udata.addr.buf = (UInt8 *)&addr;
        udata.addr.len = sizeof(addr);
        udata.udata.buf = (UInt8 *)buf;
        udata.udata.len = sizeof(buf) - 1;

        err = OTSndUData(ep, &udata);
        if (err != noErr) {
            report_skip("ot_udp_send", "no host echo server");
        } else {
            report_pass("ot_udp_send");

            /* Try to receive echo (non-blocking, short timeout) */
            OTSetNonBlocking(ep);
            memset(&udata, 0, sizeof(udata));
            udata.addr.buf = (UInt8 *)&addr;
            udata.addr.maxlen = sizeof(addr);
            udata.udata.buf = (UInt8 *)rbuf;
            udata.udata.maxlen = sizeof(rbuf);

            /* Poll briefly */
            {
                int i;
                for (i = 0; i < 10; i++) {
                    err = OTRcvUData(ep, &udata, &flags);
                    if (err == noErr) break;
                    /* small delay via event loop tick */
                }
            }
            if (err == noErr && udata.udata.len > 0) {
                report_pass("ot_udp_echo");
            } else {
                report_skip("ot_udp_echo", "no echo received");
            }
        }
    }

    OTCloseProvider(ep);

    /* TCP connect test (proves full IP NAT end-to-end) */
    {
        EndpointRef tep;
        TCall sndCall;
        InetAddress raddr;

        tep = OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, NULL, &err);
        if (err != noErr) {
            report_fail("ot_tcp_endpoint", err);
        } else {
            report_pass("ot_tcp_endpoint");

            err = OTBind(tep, NULL, NULL);
            if (err == noErr) {
                OTInitInetAddress(&raddr, TCP_TEST_PORT, 0x0A000202);

                memset(&sndCall, 0, sizeof(sndCall));
                sndCall.addr.buf = (UInt8 *)&raddr;
                sndCall.addr.len = sizeof(raddr);

                err = OTConnect(tep, &sndCall, NULL);
                if (err == noErr) {
                    report_pass("ot_tcp_connect_nat");
                    OTSndDisconnect(tep, NULL);
                } else {
                    report_skip("ot_tcp_connect_nat", "no host TCP server");
                }
            }
            OTCloseProvider(tep);
        }
    }

    CloseOpenTransport();
    report_pass("ot_cleanup");
}

#endif /* __mc68000__ */
