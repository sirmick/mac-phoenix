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
        report_skip("network_open_mactcp", "MacTCP not available");
        return;
    }
    report_pass("network_open_mactcp");

    /* Get local IP address via ipctlGetAddr */
    {
        struct GetAddrParamBlock pb;
        memset(&pb, 0, sizeof(pb));
        pb.ioCRefNum = refNum;
        pb.csCode = ipctlGetAddr;
        err = PBControlSync((ParmBlkPtr)&pb);
        if (err != noErr) {
            report_fail("network_get_ip", err);
        } else {
            report_pass("network_get_ip");
        }
    }
}
