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

// Executor backend: hand one frame to the parent directly (the Toolbox
// mixes in C++, there is no Apple Mixer to pull from). S16MSB interleaved,
// `samples` frames of `channels` samples at `sample_rate`. False when the
// ring is full or there is no IPC buffer.
bool audio_direct_push_frame(const uint8_t* s16be, uint32_t samples,
                             uint32_t sample_rate, uint32_t channels);

#endif // AUDIO_DIRECT_H
