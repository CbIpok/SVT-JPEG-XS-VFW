// JpegXsSender.h
#pragma once

#include <cstdint>
#include <memory>
#include "SvtJpegXs.h"     // your JPEG-XS SDK headers
#include "DecParamParser.h" // defines DecoderConfig_t, user_private_data_t, etc.
#include "UtilityApp.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

class JpegXsSender {
public:
    // Construct with a fully-populated DecoderConfig_t.
    explicit JpegXsSender(DecoderConfig_t& cfg)
    : config_(&cfg)
    , fps_interval_ms_(cfg.limit_fps ? 1000.0 / cfg.limit_fps : 0.0)
    , bitstream_ptr_(cfg.bitstream_buf_ref + cfg.bitstream_offset)
    , bitstream_size_(cfg.bitstream_buf_size - cfg.bitstream_offset)
    , file_end_(false)
    , send_frames_(0)
    {
        get_current_time(&thread_start_time_[0], &thread_start_time_[1]);
    }

    // Sends one frame. Returns true if that was the last frame
    // (i.e. we hit end-of-file or reached frames_count).
    // Throws std::runtime_error on any error.
    bool sendFrame() {
        uint32_t frame_size = 0;
        auto ret = svt_jpeg_xs_decoder_get_single_frame_size(
            bitstream_ptr_,
            bitstream_size_,
            nullptr,
            &frame_size,
            /*disable_alt_scan=*/1
        );
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Failed to get frame size: error " + std::to_string(ret));
        }
        if (frame_size > bitstream_size_) {
            throw std::runtime_error("Invalid frame size (beyond buffer end)");
        }

        // Prepare bitstream buffer
        svt_jpeg_xs_bitstream_buffer_t bitstream;
        bitstream.buffer         = bitstream_ptr_;
        bitstream.used_size      = frame_size;
        bitstream.allocation_size = frame_size;

        // Detect end-of-file
        if (frame_size == bitstream_size_) {
            file_end_ = true;
            // wrap back to start if re-reading?
            bitstream_ptr_  = config_->bitstream_buf_ref + config_->bitstream_offset;
            bitstream_size_ = config_->bitstream_buf_size - config_->bitstream_offset;
        } else {
            bitstream_ptr_  += frame_size;
            bitstream_size_ -= frame_size;
        }

        // Grab an output frame buffer
        svt_jpeg_xs_frame_t dec_input{};
        ret = svt_jpeg_xs_frame_pool_get(
            config_->frame_pool,
            &dec_input,
            /*blocking=*/1
        );
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Failed to get frame from pool: error " + std::to_string(ret));
        }
        dec_input.bitstream = bitstream;

        // Optional latency measurement + FPS limiting
        if (config_->limit_fps) {
            double predicted_ms = send_frames_ * fps_interval_ms_;
            uint64_t now_s, now_ms;
            get_current_time(&now_s, &now_ms);
            double elapsed_ms = compute_elapsed_time_in_ms(
                thread_start_time_[0], thread_start_time_[1],
                now_s, now_ms
            );
            int32_t to_sleep = int32_t(predicted_ms - elapsed_ms);
            if (to_sleep > 0) {
                sleep_in_ms(to_sleep);
            }
        }
        // attach performance context
        auto* user_data = static_cast<user_private_data_t*>(
            std::malloc(sizeof(user_private_data_t))
        );
        if (!user_data) {
            throw std::runtime_error("Out of memory allocating performance context");
        }
        user_data->frame_num = send_frames_;
        get_current_time(&user_data->frame_start_time[0], &user_data->frame_start_time[1]);
        dec_input.user_prv_ctx_ptr = user_data;

        // Send to decoder
        ret = svt_jpeg_xs_decoder_send_frame(
            &config_->decoder,
            &dec_input,
            /*blocking=*/1
        );
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Failed to send frame to decoder: error " + std::to_string(ret));
        }

        ++send_frames_;

        // Determine if this was the last frame
        bool is_last = false;
        if (config_->frames_count != 0) {
            is_last = (send_frames_ >= config_->frames_count);
        } else {
            is_last = file_end_;
        }
        return is_last;
    }

    // Sends the EOC (end-of-codestream) marker.
    void sendEOC() {
        auto ret = svt_jpeg_xs_decoder_send_eoc(&config_->decoder);
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("Failed to send EOC marker: error " + std::to_string(ret));
        }
    }

private:
    DecoderConfig_t* config_;
    double               fps_interval_ms_;
    uint8_t*             bitstream_ptr_;
    uint64_t             bitstream_size_;
    uint64_t             thread_start_time_[2]{};
    bool                 file_end_;
    uint64_t             send_frames_;
};
