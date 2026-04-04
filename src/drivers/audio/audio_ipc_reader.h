/*
 *  audio_ipc_reader.h - Parent-side audio IPC reader
 *
 *  Reads audio frames from child's IPC SHM ring buffer,
 *  byte-swaps S16MSB→S16LE, and feeds AudioOutput for Opus encoding.
 */

#ifndef AUDIO_IPC_READER_H
#define AUDIO_IPC_READER_H

#include "audio_output.h"
#include "ipc_client.h"

// Start/stop the audio IPC reader thread (parent process)
void audio_ipc_reader_start(IPCClient* client, AudioOutput* output);
void audio_ipc_reader_stop(void);

#endif // AUDIO_IPC_READER_H
