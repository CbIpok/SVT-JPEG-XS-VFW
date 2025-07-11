#include "JpegXsDecoder.h"

#include <string>
class JpegXsFileSender {
public:
    explicit JpegXsFileSender(DecoderConfig_t& cfg)
      : cfg_(cfg)
      , decoder_(cfg)
      , fps_interval_ms_(cfg.limit_fps ? 1000.0 / cfg.limit_fps : 0.0)
      , bitstream_ptr_(cfg.bitstream_buf_ref + cfg.bitstream_offset)
      , bitstream_size_(cfg.bitstream_buf_size - cfg.bitstream_offset)
      , file_end_(false)
      , send_frames_(0)
    {
        get_current_time(&thread_start_time_[0], &thread_start_time_[1]);
    }

    // Возвращает true, если это был последний кадр
    bool sendFrame() {
        // Узнаём размер следующего кадра
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

        // Определяем, последний ли это кадр
        if (frame_size == bitstream_size_) {
            file_end_ = true;
        }

        // Опционально – FPS-лимит и замеры
        user_private_data_t* perfCtx = nullptr;
        if (cfg_.limit_fps) {
            double predicted = send_frames_ * fps_interval_ms_;
            uint64_t now_s, now_ms;
            get_current_time(&now_s, &now_ms);
            double elapsed = compute_elapsed_time_in_ms(
                thread_start_time_[0], thread_start_time_[1], now_s, now_ms
            );
            int32_t to_sleep = int32_t(predicted - elapsed);
            if (to_sleep > 0) {
                sleep_in_ms(to_sleep);
            }
        }
        // Создаём контекст замеров
        perfCtx = static_cast<user_private_data_t*>(
            std::malloc(sizeof(user_private_data_t))
        );
        if (!perfCtx) {
            throw std::runtime_error("FileSender: malloc perfCtx failed");
        }
        perfCtx->frame_num = send_frames_;
        get_current_time(&perfCtx->frame_start_time[0], &perfCtx->frame_start_time[1]);

        // Вызываем декодер
        decoder_.decodeFrame(bitstream_ptr_, frame_size, perfCtx);
        ++send_frames_;

        // Сдвигаем указатель буфера
        if (file_end_) {
            // оборачиваемся на начало
            bitstream_ptr_  = cfg_.bitstream_buf_ref + cfg_.bitstream_offset;
            bitstream_size_ = cfg_.bitstream_buf_size - cfg_.bitstream_offset;
        } else {
            bitstream_ptr_  += frame_size;
            bitstream_size_ -= frame_size;
        }

        // Проверяем, достигли ли мы числа кадров
        bool is_last = (cfg_.frames_count
                        ? (send_frames_ >= cfg_.frames_count)
                        : file_end_);
        return is_last;
    }

    // После завершения всех кадров нужно вызвать EOC
    void sendEOC() {
        decoder_.sendEOC();
    }

private:
    DecoderConfig_t& cfg_;
    JpegXsDecoder    decoder_;

    double    fps_interval_ms_;
    uint8_t*  bitstream_ptr_;
    uint64_t  bitstream_size_;

    uint64_t  thread_start_time_[2];
    bool      file_end_;
    uint64_t  send_frames_;
};