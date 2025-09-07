/* Buffer-based DecApp using BufferCodec wrapper */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "DecParamParser.h"
#include "UtilityApp.h"
#include "BufferCodec.h"

static uint32_t frame_size_from_cfg(const svt_jpeg_xs_image_config_t* cfg) {
    uint32_t sz = 0;
    for (uint8_t c = 0; c < cfg->components_num; ++c) sz += cfg->components[c].byte_size;
    return sz;
}

SvtJxsErrorType_t read_data_from_file(FILE* f, uint8_t* buf, size_t size) {
    if (f == NULL) {
        return SvtJxsErrorUndefined;
    }
    size_t read_size = fread(buf, 1, size, f);
    if (read_size != size) {
        fprintf(stderr, "Error while reading file, read=%zu bytes, expected=%zu bytes\n", read_size, size);
        return SvtJxsErrorUndefined;
    }
    return SvtJxsErrorNone;
}

int32_t main(int32_t argc, char* argv[]) {
    if (get_help(argc, argv)) return 0;

    DecoderConfig_t config_dec = {0};
    SvtJxsErrorType_t return_error = read_command_line(argc, argv, &config_dec);
    if (return_error != SvtJxsErrorNone) return return_error;

    if (!config_dec.in_filename[0]) { fprintf(stderr, "Input file not specified.\n"); return SvtJxsErrorBadParameter; }
    FOPEN(config_dec.in_file, config_dec.in_filename, "rb");
    if (!config_dec.in_file) { fprintf(stderr, "Invalid input file\n"); return SvtJxsErrorBadParameter; }
    if (config_dec.out_filename[0]) {
        FOPEN(config_dec.out_file, config_dec.out_filename, "wb");
        if (!config_dec.out_file) { fprintf(stderr, "Invalid output file\n"); return SvtJxsErrorBadParameter; }
    }

    config_dec.bitstream_buf_size = get_file_size(config_dec.in_file);
    if (config_dec.bitstream_buf_size <= 0) { fprintf(stderr, "Unable to open file %s\n", config_dec.in_filename); return SvtJxsErrorDecoderInvalidBitstream; }
    config_dec.bitstream_buf_ref = (uint8_t*)malloc(config_dec.bitstream_buf_size);
    if (!config_dec.bitstream_buf_ref) { fprintf(stderr, "Unable allocate memory for read file\n"); return SvtJxsErrorInsufficientResources; }
    return_error = read_data_from_file(config_dec.in_file, config_dec.bitstream_buf_ref, config_dec.bitstream_buf_size);
    if (return_error) { fprintf(stderr, "Unable to read file %s\n", config_dec.in_filename); return return_error; }

    // autodetect header if requested
    if (config_dec.autodetect_bitstream_header) {
        int find = 0;
        for (size_t i = 0; i < config_dec.bitstream_buf_size - 4; ++i) {
            if ((config_dec.bitstream_buf_ref[i] == 0xff) && (config_dec.bitstream_buf_ref[i + 1] == 0x10) &&
                (config_dec.bitstream_buf_ref[i + 2] == 0xff) && (config_dec.bitstream_buf_ref[i + 3] == 0x50)) {
                config_dec.bitstream_offset = i;
                find = 1;
                fprintf(stderr, "Detect Bitstream header offset: %zu\n", i);
                break;
            }
        }
        if (!find) { fprintf(stderr, "Detect Bitstream header FAILED!\n"); return SvtJxsErrorUndefined; }
    }

    // init decoder wrapper from first frame
    uint32_t frame_size = 0;
    return_error = svt_jpeg_xs_decoder_get_single_frame_size(config_dec.bitstream_buf_ref + config_dec.bitstream_offset,
                                                             config_dec.bitstream_buf_size - config_dec.bitstream_offset,
                                                             NULL, &frame_size, 1);
    if (return_error != SvtJxsErrorNone) { fprintf(stderr, "Unable to get first frame size\n"); if (!config_dec.force_decode) return return_error; }

    buffer_image_config_t img_cfg;
    buffer_decoder_t* dec = buffer_decoder_create_from_bitstream(
        config_dec.bitstream_buf_ref + config_dec.bitstream_offset,
        config_dec.bitstream_buf_size - config_dec.bitstream_offset,
        &img_cfg);
    if (!dec) { fprintf(stderr, "Decoder init failed\n"); return SvtJxsErrorUndefined; }
    // Map to app image_config
    config_dec.image_config.width = img_cfg.width;
    config_dec.image_config.height = img_cfg.height;
    config_dec.image_config.bit_depth = img_cfg.bit_depth;
    // format unknown here, not needed for raw write
    config_dec.image_config.components_num = img_cfg.components_num;
    for (uint8_t c = 0; c < img_cfg.components_num; ++c) {
        config_dec.image_config.components[c].width = img_cfg.comp[c].width;
        config_dec.image_config.components[c].height = img_cfg.comp[c].height;
        config_dec.image_config.components[c].byte_size = img_cfg.comp[c].byte_size;
    }

    // allocate one output image buffer (contiguous)
    uint32_t out_frame_capacity = frame_size_from_cfg(&config_dec.image_config);
    uint8_t* out_frame = (uint8_t*)malloc(out_frame_capacity);
    if (!out_frame) { fprintf(stderr, "Memory allocation failed\n"); return SvtJxsErrorInsufficientResources; }

    uint8_t* bitstream_ptr = config_dec.bitstream_buf_ref + config_dec.bitstream_offset;
    uint64_t bitstream_size = config_dec.bitstream_buf_size - config_dec.bitstream_offset;
    uint64_t frames_done = 0;
    while (bitstream_size > 0 && (config_dec.frames_count == 0 || frames_done < config_dec.frames_count)) {
        uint32_t fs = 0;
        SvtJxsErrorType_t sz_ret = svt_jpeg_xs_decoder_get_single_frame_size(bitstream_ptr, (uint32_t)bitstream_size, NULL, &fs, 1);
        if (sz_ret != SvtJxsErrorNone || fs == 0 || fs > bitstream_size) break;
        uint32_t out_used = 0;
        int r = buffer_decoder_decode_frame(dec, bitstream_ptr, fs, out_frame, out_frame_capacity, &out_used);
        if (r != 0) { fprintf(stderr, "Decode error %d\n", r); return_error = SvtJxsErrorUndefined; break; }
        if (config_dec.out_file) { size_t wr = fwrite(out_frame, 1, out_used, config_dec.out_file); if (wr != out_used) break; }
        bitstream_ptr += fs;
        bitstream_size -= fs;
        frames_done++;
    }

    buffer_decoder_destroy(dec);
    free(out_frame);
    free(config_dec.bitstream_buf_ref);
    if (config_dec.in_file) fclose(config_dec.in_file);
    if (config_dec.out_file) fclose(config_dec.out_file);
    return (int32_t)return_error;
}
