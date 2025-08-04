// JpegXsDecoder.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include "SvtJpegXs.h"      // svt_jpeg_xs_decoder_t, frame pool, error codes
#include "DecParamParser.h" // user_private_data_t, DecoderConfig_t

#include <string>

class JpegXsDecoder {
  public:
    explicit JpegXsDecoder(DecoderConfig_t& cfg) : cfg_(cfg) {
    }

    // data/size — bitstream from FileSender
    // out/out_size — ptr and size of preallocated YUV buffer
    // perfCtx — optional user_private_data_t* for perf tracking
    void decodeFrame(const uint8_t* data, size_t data_size, uint8_t* out, size_t out_size,
                     user_private_data_t* perfCtx = nullptr) {
        svt_jpeg_xs_frame_t frame{};
        auto ret = svt_jpeg_xs_frame_pool_get(cfg_.frame_pool, &frame, /*blocking=*/1);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to get frame from pool, error " + std::to_string(ret));
        }

        svt_jpeg_xs_bitstream_buffer_t bs{};
        bs.buffer = const_cast<uint8_t*>(data);
        bs.used_size = uint32_t(data_size);
        bs.allocation_size = uint32_t(data_size);
        frame.bitstream = bs;
        frame.user_prv_ctx_ptr = perfCtx;

        ret = svt_jpeg_xs_decoder_send_frame(&cfg_.decoder, &frame, /*blocking=*/1);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to send frame, error " + std::to_string(ret));
        }

        // Now pull back the decoded image
        svt_jpeg_xs_frame_t dec_out{};
        ret = svt_jpeg_xs_decoder_get_frame(&cfg_.decoder, &dec_out, /*blocking=*/1);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to get decoded frame, error " + std::to_string(ret));
        }

        // Copy planar YUV into contiguous out[]
        size_t offset = 0;
        for (uint32_t c = 0; c < cfg_.image_config.components_num; ++c) {
            size_t comp_size = cfg_.image_config.components[c].byte_size;
            if (offset + comp_size > out_size) {
                throw std::runtime_error("Decoder: output buffer too small");
            }
            std::memcpy(out + offset, dec_out.image.data_yuv[c], comp_size);
            offset += comp_size;
        }

        // Release the frame back to the pool
        svt_jpeg_xs_frame_pool_release(cfg_.frame_pool, &dec_out);
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
