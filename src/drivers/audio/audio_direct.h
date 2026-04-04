/*
 *  audio_direct.h - Audio driver for IPC child process
 */

#ifndef AUDIO_DIRECT_H
#define AUDIO_DIRECT_H

#include "ipc_protocol.h"

// Set IPC buffer before calling init
void audio_direct_set_ipc_buffer(IPCBuffer* buf);

// Platform API compatible init/exit (child process)
void audio_direct_init(void);
void audio_direct_exit(void);

// Called from control_ipc_child when parent sends IPC_INPUT_AUDIO_REQUEST
void audio_request_data(uint32_t requested_samples);

#endif // AUDIO_DIRECT_H
