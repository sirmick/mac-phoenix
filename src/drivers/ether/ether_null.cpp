/*
 *  ether_null.cpp - Ethernet null driver
 */

#include "sysdeps.h"
#include "platform.h"

bool ether_null_init(void)
{
	return true;  // Success
}

void ether_null_exit(void)
{
	// No-op
}

void ether_null_reset(void)
{
	// No-op
}

int16_t ether_null_add_multicast(uint32_t pb)
{
	(void)pb;
	return 0;  // noErr
}

int16_t ether_null_del_multicast(uint32_t pb)
{
	(void)pb;
	return 0;  // noErr
}

int16_t ether_null_attach_ph(uint16_t type, uint32_t handler)
{
	(void)type;
	(void)handler;
	return 0;  // noErr
}

int16_t ether_null_detach_ph(uint16_t type)
{
	(void)type;
	return 0;  // noErr
}

int16_t ether_null_write(uint32_t wds)
{
	(void)wds;
	return 0;  // noErr
}

bool ether_null_start_udp_thread(int socket_fd)
{
	(void)socket_fd;
	return false;  // Not supported
}

void ether_null_stop_udp_thread(void)
{
	// No-op
}
