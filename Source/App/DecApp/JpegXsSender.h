#include "JpegXsDecoder.h"

#include <string>

class JpegXsFileSender {
public:
    struct Frame {
        const uint8_t* data;      // указатель на начало кадра
        size_t         size;      // размер кадра
        bool           is_last;   // true, если это последний кадр
        user_private_data_t* perf_ctx; // контекст производительности (для латентности)
    };

    explicit JpegXsFileSender(DecoderConfig_t& cfg)
      : cfg_(cfg)
      , fps_interval_ms_(cfg.limit_fps ? 1000.0 / cfg.limit_fps : 0.0)
      , bitstream_ptr_(cfg.bitstream_buf_ref + cfg.bitstream_offset)
      , bitstream_size_(cfg.bitstream_buf_size - cfg.bitstream_offset)
      , file_end_(false)
      , send_frames_(0)
    {
        get_current_time(&thread_start_time_[0], &thread_start_time_[1]);
    }

    // Возвращает структуру с указателем на следующий кадр,
    // размером, флагом последнего кадра и perf_ctx.
    // Бросает std::runtime_error при ошибке.
    Frame nextFrame() {
        uint32_t frame_size = 0;
        auto ret = svt_jpeg_xs_decoder_get_single_frame_size(
            bitstream_ptr_, bitstream_size_, nullptr, &frame_size, /*disable_alt_scan=*/1
        );
        if (ret != SvtJxsErrorNone) {
            throw std::runtime_error("FileSender: get_frame_size error " + std::to_string(ret));
        }
        if (frame_size > bitstream_size_) {
            throw std::runtime_error("FileSender: invalid frame size beyond buffer");
        }

        bool is_last = false;
        if (frame_size == bitstream_size_) {
            // последний в буфере
            file_end_ = true;
        }
        // проверка рамки счетчика
        if (cfg_.frames_count != 0 && send_frames_ + 1 >= cfg_.frames_count) {
            is_last = true;
        } else if (cfg_.frames_count == 0 && file_end_) {
            is_last = true;
        }

        // FPS-лимит и замеры
        if (cfg_.limit_fps) {
            double predicted   = send_frames_ * fps_interval_ms_;
            uint64_t now_s, now_ms;
            get_current_time(&now_s, &now_ms);
            double elapsed     = compute_elapsed_time_in_ms(
                thread_start_time_[0], thread_start_time_[1], now_s, now_ms
            );
            int32_t to_sleep   = int32_t(predicted - elapsed);
            if (to_sleep > 0) {
                sleep_in_ms(to_sleep);
            }
        }

        // Создаём perf_ctx
        auto* perfCtx = static_cast<user_private_data_t*>(
            std::malloc(sizeof(user_private_data_t))
        );
        if (!perfCtx) {
            throw std::runtime_error("FileSender: malloc perfCtx failed");
        }
        perfCtx->frame_num = send_frames_;
        get_current_time(&perfCtx->frame_start_time[0], &perfCtx->frame_start_time[1]);

        // Подготовка результата
        Frame result;
        result.data     = bitstream_ptr_;
        result.size     = frame_size;
        result.is_last  = is_last;
        result.perf_ctx = perfCtx;

        // Сдвигаем указатель
        if (file_end_) {
            // wrap around
            bitstream_ptr_  = cfg_.bitstream_buf_ref + cfg_.bitstream_offset;
            bitstream_size_ = cfg_.bitstream_buf_size - cfg_.bitstream_offset;
        } else {
            bitstream_ptr_  += frame_size;
            bitstream_size_ -= frame_size;
        }

        ++send_frames_;
        return result;
    }

private:
    DecoderConfig_t& cfg_;

    double    fps_interval_ms_;
    uint8_t*  bitstream_ptr_;
    uint64_t  bitstream_size_;

    uint64_t  thread_start_time_[2];
    bool      file_end_;
    uint64_t  send_frames_;
};