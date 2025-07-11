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
    // Конструктор принимает DecoderConfig_t, но использует только то, что нужно для декодера:
    explicit JpegXsDecoder(DecoderConfig_t& cfg)
      : cfg_(cfg)
    {}

    // Декодирует один кадр из сырого буфера [data, data+size)
    // perfCtx — опциональный контекст для замеров (user_private_data_t)
    void decodeFrame(const uint8_t* data, size_t size, user_private_data_t* perfCtx = nullptr) {
        // Получаем новый svt_jpeg_xs_frame_t из пула
        svt_jpeg_xs_frame_t frame{};
        auto ret = svt_jpeg_xs_frame_pool_get(
            cfg_.frame_pool, &frame, /*blocking=*/1
        );
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to get frame from pool, error " + std::to_string(ret));
        }

        // Настраиваем битстрим
        svt_jpeg_xs_bitstream_buffer_t bs{};
        bs.buffer = const_cast<uint8_t*>(data);
        bs.used_size = uint32_t(size);
        bs.allocation_size = uint32_t(size);
        frame.bitstream = bs;

        // При необходимости: передать perfCtx
        frame.user_prv_ctx_ptr = perfCtx;

        // Отправляем в декодер
        ret = svt_jpeg_xs_decoder_send_frame(
            &cfg_.decoder, &frame, /*blocking=*/1
        );
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to send frame, error " + std::to_string(ret));
        }
    }

    // Отправляет маркер конца кодпотока
    void sendEOC() {
        auto ret = svt_jpeg_xs_decoder_send_eoc(&cfg_.decoder);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Decoder: failed to send EOC, error " + std::to_string(ret));
        }
    }

private:
    DecoderConfig_t& cfg_;
};
