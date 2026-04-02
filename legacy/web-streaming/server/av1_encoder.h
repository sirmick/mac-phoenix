/*
 * AV1 Encoder using SVT-AV1
 */

#ifndef AV1_ENCODER_H
#define AV1_ENCODER_H

#include "codec.h"
#include <EbSvtAv1Enc.h>
#include <vector>

class AV1Encoder : public VideoCodec {
public:
    AV1Encoder() = default;
    ~AV1Encoder() override { cleanup(); }

    // VideoCodec interface
    CodecType type() const override { return CodecType::AV1; }
    const char* name() const override { return "AV1"; }
    bool init(int width, int height, int fps = 30) override;
    void cleanup() override;
    EncodedFrame encode_i420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                             int width, int height, int y_stride, int uv_stride) override;
    EncodedFrame encode_bgra(const uint8_t* bgra, int width, int height, int stride) override;
    void request_keyframe() override { force_keyframe_ = true; }

private:
    EbComponentType* encoder_ = nullptr;
    EbSvtAv1EncConfiguration config_ = {};

    int width_ = 0;
    int height_ = 0;
    int fps_ = 30;
    bool force_keyframe_ = false;
    uint64_t frame_count_ = 0;

    // I420 conversion buffer
    std::vector<uint8_t> i420_buffer_;

    // Input buffer (allocated once, reused for all frames)
    EbBufferHeaderType* input_buffer_ = nullptr;
    EbSvtIOFormat* input_picture_ = nullptr;
};

#endif // AV1_ENCODER_H
