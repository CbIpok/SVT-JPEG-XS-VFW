// JpegXsDecoder.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include "SvtJpegXs.h"     // svt_jpeg_xs_decoder_t, frame pool, error codes
#include "DecParamParser.h" // user_private_data_t, DecoderConfig_t

#include <string>

class JpegXsDecoder {
public:
    explicit JpegXsDecoder(DecoderConfig_t& cfg)
      : cfg_(cfg)
    {}

    // data/size — полученные из FileSender, perfCtx — опциональный указатель на user_private_data_t
    void decodeFrame(const uint8_t* data, size_t size, user_private_data_t* perfCtx = nullptr) {
        svt_jpeg_xs_frame_t frame{};
        auto ret = svt_jpeg_xs_frame_pool_get(cfg_.frame_pool, &frame, /*blocking=*/1);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to get frame from pool, error " + std::to_string(ret));
        }

        svt_jpeg_xs_bitstream_buffer_t bs{};
        bs.buffer         = const_cast<uint8_t*>(data);
        bs.used_size      = uint32_t(size);
        bs.allocation_size = uint32_t(size);
        frame.bitstream   = bs;
        frame.user_prv_ctx_ptr = perfCtx;

        ret = svt_jpeg_xs_decoder_send_frame(&cfg_.decoder, &frame, /*blocking=*/1);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to send frame, error " + std::to_string(ret));
        }
    }

    void sendEOC() {
        auto ret = svt_jpeg_xs_decoder_send_eoc(&cfg_.decoder);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to send EOC, error " + std::to_string(ret));
        }
    }

private:
    DecoderConfig_t& cfg_;
};
