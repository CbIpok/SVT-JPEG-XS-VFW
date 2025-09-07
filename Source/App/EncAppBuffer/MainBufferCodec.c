/* Buffer-based EncApp using BufferCodec wrapper */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "EncAppConfig.h"
#include "UtilityApp.h"
#include "BufferCodec.h"

#ifndef TEST_STRIDE
#define TEST_STRIDE 0
#endif

static int32_t read_yuv_frame(FILE* in_file, svt_jpeg_xs_image_config_t* image_config, svt_jpeg_xs_image_buffer_t* yuv_buffer) {
    if (feof(in_file)) {
        return 1;
    }
    for (uint8_t c = 0; c < image_config->components_num; ++c) {
        uint32_t read_size = (uint32_t)fread(yuv_buffer->data_yuv[c], 1, yuv_buffer->alloc_size[c], in_file);
        if (image_config->components[c].byte_size != read_size) {
            return 1;
        }
    }
    return 0;
}

static uint32_t get_single_frame_size(svt_jpeg_xs_image_config_t* image_config) {
    uint32_t size = 0;
    for (uint8_t c = 0; c < image_config->components_num; ++c) size += image_config->components[c].byte_size;
    return size;
}

static int32_t reset_yuv_file(FILE* in_file) { return fseek(in_file, 0, SEEK_SET); }

static void show_encoding_progress(EncoderConfig_t* config, uint64_t encoded_frame_count) {
    switch (config->progress) {
    case 0: break;
    case 1:
        fprintf(stderr, "\b\b\b\b\b\b\b\b\b%9lu", (unsigned long)encoded_frame_count);
        break;
    default: break;
    }
    fflush(stderr);
}

#define ENC_IGNORE_SOME_FRAMES (11)

int32_t main(int32_t argc, char* argv[]) {
    if (get_help(argc, argv)) return 0;

    SvtJxsErrorType_t return_error = SvtJxsErrorNone;
    EncoderConfig_t config_enc = {0};
    config_enc.progress = 1;
    config_enc.frames_count = 0;

    return_error = svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &config_enc.encoder);
    if (return_error != SvtJxsErrorNone) {
        fprintf(stderr, "Error while loading default parameters!\n");
        return return_error;
    }
    return_error = read_command_line(argc, argv, &config_enc);
    if (return_error != SvtJxsErrorNone) return return_error;
    if (!config_enc.in_filename[0]) { fprintf(stderr, "Error: Not set Input source File\n"); return SvtJxsErrorBadParameter; }
    FOPEN(config_enc.in_file, config_enc.in_filename, "rb");
    if (!config_enc.in_file) { fprintf(stderr, "Invalid input file: '%s'\n", config_enc.in_filename); return SvtJxsErrorBadParameter; }
    if (config_enc.out_filename[0]) {
        FOPEN(config_enc.out_file, config_enc.out_filename, "wb");
        if (!config_enc.out_file) { fprintf(stderr, "Invalid output file: '%s'\n", config_enc.out_filename); return SvtJxsErrorBadParameter; }
    }
    return_error = verify_settings(&config_enc);
    if (return_error != SvtJxsErrorNone) { fprintf(stderr, "Error in configuration\n"); return return_error; }

    uint32_t bs_capacity = 0;
    buffer_encoder_t* enc = buffer_encoder_create();
    if (!enc) { fprintf(stderr, "Failed to create buffer encoder\n"); return SvtJxsErrorInsufficientResources; }
    buffer_image_config_t img_cfg;
    if (buffer_encoder_get_image_config(enc, &img_cfg, &bs_capacity) != 0) { fprintf(stderr, "Encoder cfg failed\n"); return SvtJxsErrorUndefined; }

    // Map to app image_config for I/O helpers
    config_enc.image_config.width = img_cfg.width;
    config_enc.image_config.height = img_cfg.height;
    config_enc.image_config.bit_depth = img_cfg.bit_depth;
    // infer components
    if (BUFFER_CODEC_COLOUR_FORMAT == 0) config_enc.image_config.format = COLOUR_FORMAT_PLANAR_YUV400;
    else if (BUFFER_CODEC_COLOUR_FORMAT == 1) config_enc.image_config.format = COLOUR_FORMAT_PLANAR_YUV420;
    else if (BUFFER_CODEC_COLOUR_FORMAT == 2) config_enc.image_config.format = COLOUR_FORMAT_PLANAR_YUV422;
    else config_enc.image_config.format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    config_enc.image_config.components_num = img_cfg.components_num;
    for (uint8_t c = 0; c < img_cfg.components_num; ++c) {
        config_enc.image_config.components[c].width = img_cfg.comp[c].width;
        config_enc.image_config.components[c].height = img_cfg.comp[c].height;
        config_enc.image_config.components[c].byte_size = img_cfg.comp[c].byte_size;
    }

    if (config_enc.frames_count == 0) {
        int64_t file_size = get_file_size(config_enc.in_file);
        uint32_t single_frame_size = get_single_frame_size(&config_enc.image_config);
        config_enc.frames_count = (uint32_t)(file_size / single_frame_size);
    }

    // Allocate contiguous input frame buffer
    uint32_t frame_size = get_single_frame_size(&config_enc.image_config);
    uint8_t* in_frame = (uint8_t*)malloc(frame_size);
    if (!in_frame) { fprintf(stderr, "Memory allocation failed\n"); return SvtJxsErrorInsufficientResources; }
    uint8_t* bs_buf = (uint8_t*)malloc(bs_capacity);
    if (!bs_buf) { fprintf(stderr, "Memory allocation failed\n"); return SvtJxsErrorInsufficientResources; }

    uint64_t frames_done = 0;
    while (frames_done < config_enc.frames_count) {
        size_t rd = fread(in_frame, 1, frame_size, config_enc.in_file);
        if (rd != frame_size) {
            if (reset_yuv_file(config_enc.in_file) != 0) break;
            rd = fread(in_frame, 1, frame_size, config_enc.in_file);
            if (rd != frame_size) break;
        }
        uint32_t used = 0;
        int r = buffer_encoder_encode_frame(enc, (const uint8_t*)in_frame, bs_buf, bs_capacity, &used);
        if (r != 0) { fprintf(stderr, "Encode error %d\n", r); return_error = SvtJxsErrorUndefined; break; }
        if (config_enc.out_file && used) fwrite(bs_buf, 1, used, config_enc.out_file);
        frames_done++;
        show_encoding_progress(&config_enc, frames_done);
    }

    buffer_encoder_destroy(enc);
    free(in_frame);
    free(bs_buf);
    if (config_enc.in_file) fclose(config_enc.in_file);
    if (config_enc.out_file) fclose(config_enc.out_file);
    return (int32_t)return_error;
}
