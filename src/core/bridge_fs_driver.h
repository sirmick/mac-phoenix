/*
 * bridge_fs_driver.h - In-memory "Bridge:" volume for automation
 */
#ifndef BRIDGE_FS_DRIVER_H
#define BRIDGE_FS_DRIVER_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

void InstallBridgeFS(void);
int16_t BridgeFSComm(uint16_t message, uint32_t paramBlock, uint32_t globals);
int16_t BridgeFSHFS(uint32_t vcb, uint16_t selectCode, uint32_t paramBlock,
                     uint32_t globals, int16_t fsid);

#ifdef __cplusplus
}
#endif

#endif
