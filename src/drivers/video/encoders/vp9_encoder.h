/*
 * VP9 Encoder using libvpx
 * Optimized for screen content and UI rendering
 */

#ifndef VP9_ENCODER_H
#define VP9_ENCODER_H

#include "codec.h"
#include <vector>

#ifdef HAVE_VPX

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

class VP9Encoder : public VideoCodec {
public:
    VP9Encoder() = default;
    ~VP9Encoder() override { cleanup(); }

    // VideoCodec interface
    CodecType type() const override { return CodecType::VP9; }
    const char* name() const override { return "VP9"; }
    bool init(int width, int height, int fps = 30) override;
    void cleanup() override;
    EncodedFrame encode_i420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                             int width, int height, int y_stride, int uv_stride) override;
    EncodedFrame encode_bgra(const uint8_t* bgra, int width, int height, int stride) override;
    EncodedFrame encode_argb(const uint8_t* argb, int width, int height, int stride) override;
    void request_keyframe() override { force_keyframe_ = true; }

private:
    vpx_codec_ctx_t encoder_ = {};
    vpx_codec_enc_cfg_t config_ = {};

    int width_ = 0;
    int height_ = 0;
    int fps_ = 30;
    bool initialized_ = false;
    bool force_keyframe_ = false;
    uint64_t frame_count_ = 0;

    // I420 conversion buffer
    std::vector<uint8_t> i420_buffer_;
};

#else // !HAVE_VPX

class VP9Encoder : public VideoCodec {
public:
    CodecType type() const override { return CodecType::VP9; }
    const char* name() const override { return "VP9 (unavailable)"; }
    bool init(int, int, int = 30) override { return false; }
    void cleanup() override {}
    EncodedFrame encode_i420(const uint8_t*, const uint8_t*, const uint8_t*,
                             int w, int h, int, int) override { return EncodedFrame{.codec=CodecType::VP9,.width=w,.height=h}; }
    EncodedFrame encode_bgra(const uint8_t*, int w, int h, int) override { return EncodedFrame{.codec=CodecType::VP9,.width=w,.height=h}; }
    EncodedFrame encode_argb(const uint8_t*, int w, int h, int) override { return EncodedFrame{.codec=CodecType::VP9,.width=w,.height=h}; }
    void request_keyframe() override {}
};

#endif // HAVE_VPX

#endif // VP9_ENCODER_H
