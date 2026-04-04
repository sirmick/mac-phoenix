/*
 * Video Encoder Thread - Header
 */

#ifndef VIDEO_ENCODER_THREAD_H
#define VIDEO_ENCODER_THREAD_H

#include "video_output.h"
#include "../../config/emulator_config.h"
#include <atomic>

// Forward declare to avoid pulling ipc_protocol.h into every translation unit
struct IPCBuffer;

namespace video {

/**
 * Video encoder thread entry point
 *
 * Reads frames from VideoOutput triple buffer and encodes to H.264/VP9/WebP/PNG.
 * Runs until g_running is set to false.
 *
 * @param video_output Triple buffer to read frames from (also used for screenshots)
 * @param config Configuration (for codec selection)
 * @param ipc_shm_ptr Optional atomic pointer to IPC shared memory. When the pointed-to
 *                    value is non-null, the encoder reads frames directly from IPC SHM
 *                    (zero-copy), bypassing VideoOutput's read path. VideoOutput is
 *                    still updated for the screenshot API.
 */
void video_encoder_main(VideoOutput* video_output, config::EmulatorConfig* config,
                        std::atomic<IPCBuffer*>* ipc_shm_ptr = nullptr,
                        std::atomic<int>* ipc_eventfd_ptr = nullptr);

} // namespace video

#endif // VIDEO_ENCODER_THREAD_H
