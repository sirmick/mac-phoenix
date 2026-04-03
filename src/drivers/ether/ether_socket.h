/*
 *  ether_socket.h - Unix socket ethernet driver
 *
 *  Connects to a net-bridge process via Unix domain socket.
 *  Ethernet frames are exchanged with 4-byte big-endian length prefix.
 */

#ifndef ETHER_SOCKET_H
#define ETHER_SOCKET_H

// Register Unix socket ethernet driver with g_platform.
// sock_path: path to the Unix domain socket to connect to.
void ether_socket_register(const char *sock_path);

#endif
