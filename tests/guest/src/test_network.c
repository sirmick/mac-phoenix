/*
 * test_network.c - Network driver tests
 *
 * Tests: open MacTCP driver, get local IP.
 * Requires emulator started with --network lwip or similar.
 */
#include <Devices.h>
#include <MacTCP.h>
#include <string.h>

#include "test_report.h"

void test_network(void)
{
    short refNum;
    OSErr err;

    /* Try to open MacTCP driver */
    err = OpenDriver("\p.IPP", &refNum);
    if (err != noErr) {
        report_fail("network_open_mactcp", err);
        return;
    }
    report_pass("network_open_mactcp");

    /* Get local IP address via ipctlGetAddr (async to avoid hang without network) */
    {
        struct GetAddrParamBlock pb;
        int i;
        memset(&pb, 0, sizeof(pb));
        pb.ioCRefNum = refNum;
        pb.csCode = ipctlGetAddr;
        pb.ioResult = 1;  /* mark as pending */
        err = PBControlAsync((ParmBlkPtr)&pb);
        if (err != noErr) {
            report_fail("network_get_ip", err);
        } else {
            /* Poll for completion (up to ~2 seconds) */
            for (i = 0; i < 200 && pb.ioResult > 0; i++) {
                /* Small delay — 68k busy loop, ~10ms per iteration */
                volatile long j;
                for (j = 0; j < 10000; j++) ;
            }
            if (pb.ioResult > 0) {
                report_skip("network_get_ip", "timeout (no network configured?)");
            } else if (pb.ioResult != noErr) {
                report_fail("network_get_ip", pb.ioResult);
            } else {
                report_pass("network_get_ip");
            }
        }
    }
}
