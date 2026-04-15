/*
 * test_serial.c - Serial port tests
 *
 * Tests: open serial driver, check status, close.
 * Loopback test requires host-side echo configuration.
 */
#include <Devices.h>
#include <Serial.h>
#include <string.h>

#include "test_report.h"

void test_serial(void)
{
    short outRef, inRef;
    OSErr err;
    SerStaRec status;

    /* Open modem port output */
    err = OpenDriver("\p.AOut", &outRef);
    if (err != noErr) {
        report_fail("serial_open_out", err);
        return;
    }
    report_pass("serial_open_out");

    /* Open modem port input */
    err = OpenDriver("\p.AIn", &inRef);
    if (err != noErr) {
        report_fail("serial_open_in", err);
        CloseDriver(outRef);
        return;
    }
    report_pass("serial_open_in");

    /* Configure: 9600 baud, 8N1 */
    err = SerReset(outRef, baud9600 | data8 | noParity | stop10);
    if (err != noErr) {
        report_fail("serial_config", err);
    } else {
        report_pass("serial_config");
    }

    /* Check status */
    err = SerStatus(inRef, &status);
    if (err != noErr) {
        report_fail("serial_status", err);
    } else {
        report_pass("serial_status");
    }

    CloseDriver(inRef);
    CloseDriver(outRef);
}
