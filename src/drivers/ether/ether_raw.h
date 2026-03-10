/*
 *  ether_raw.h - AF_PACKET raw socket bridged networking
 */

#ifndef ETHER_RAW_H
#define ETHER_RAW_H

// Register raw socket ethernet driver with g_platform
// if_name: host network interface to bind to (e.g. "eth0")
void ether_raw_register(const char* if_name);

#endif
