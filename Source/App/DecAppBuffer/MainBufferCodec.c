/* Buffer-based DecApp using VFW wrapper */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include <vfw.h>
#include "DecParamParser.h"
#include "UtilityApp.h"
#include "BufferCodec.h" /* for BUFFER_CODEC_* macros */
#include "SvtJpegxsDec.h"

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

typedef struct SJXS_Decompress {
    const uint8_t* in;
    uint32_t in_size;
    uint8_t* out;
    uint32_t out_capacity;
    uint32_t out_used;
} SJXS_Decompress;

static void copy_img_cfg(const svt_jpeg_xs_image_config_t* src, svt_jpeg_xs_image_config_t* dst) {
    *dst = *src;
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

    // Detect actual output layout from bitstream header using SVT API
    uint8_t* first_bs = config_dec.bitstream_buf_ref + config_dec.bitstream_offset;
    uint32_t first_bs_size = (uint32_t)(config_dec.bitstream_buf_size - config_dec.bitstream_offset);
    uint32_t frame_size = 0;
    return_error = svt_jpeg_xs_decoder_get_single_frame_size(first_bs, first_bs_size, NULL, &frame_size, 1);
    if (return_error != SvtJxsErrorNone) { fprintf(stderr, "Unable to get first frame size\n"); if (!config_dec.force_decode) return return_error; }
    svt_jpeg_xs_decoder_api_t tmp_dec = {0};
    svt_jpeg_xs_image_config_t detected_img = {0};
    if (svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                                 &tmp_dec, first_bs, first_bs_size, &detected_img) == SvtJxsErrorNone) {
        copy_img_cfg(&detected_img, &config_dec.image_config);
        svt_jpeg_xs_decoder_close(&tmp_dec);
    } else {
        // Fallback: derive from macros if detection fails
        fprintf(stderr, "Warning: decoder init for header parse failed, fallback to macros.\n");
        config_dec.image_config.width = BUFFER_CODEC_WIDTH;
        config_dec.image_config.height = BUFFER_CODEC_HEIGHT;
        config_dec.image_config.bit_depth = BUFFER_CODEC_BIT_DEPTH;
        config_dec.image_config.format = (BUFFER_CODEC_COLOUR_FORMAT == 0) ? COLOUR_FORMAT_PLANAR_YUV400 :
                                         (BUFFER_CODEC_COLOUR_FORMAT == 1) ? COLOUR_FORMAT_PLANAR_YUV420 :
                                         (BUFFER_CODEC_COLOUR_FORMAT == 2) ? COLOUR_FORMAT_PLANAR_YUV422 :
                                                                             COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        // approximate planar sizes
        const uint32_t bps = ((config_dec.image_config.bit_depth + 7) / 8);
        if (config_dec.image_config.format == COLOUR_FORMAT_PLANAR_YUV400) {
            config_dec.image_config.components_num = 1;
            config_dec.image_config.components[0].width = config_dec.image_config.width;
            config_dec.image_config.components[0].height = config_dec.image_config.height;
            config_dec.image_config.components[0].byte_size = config_dec.image_config.width * config_dec.image_config.height * bps;
        } else if (config_dec.image_config.format == COLOUR_FORMAT_PLANAR_YUV420) {
            config_dec.image_config.components_num = 3;
            config_dec.image_config.components[0].width = config_dec.image_config.width;
            config_dec.image_config.components[0].height = config_dec.image_config.height;
            config_dec.image_config.components[0].byte_size = config_dec.image_config.width * config_dec.image_config.height * bps;
            config_dec.image_config.components[1].width = (config_dec.image_config.width + 1) / 2;
            config_dec.image_config.components[1].height = (config_dec.image_config.height + 1) / 2;
            config_dec.image_config.components[1].byte_size = config_dec.image_config.components[1].width * config_dec.image_config.components[1].height * bps;
            config_dec.image_config.components[2] = config_dec.image_config.components[1];
        } else if (config_dec.image_config.format == COLOUR_FORMAT_PLANAR_YUV422) {
            config_dec.image_config.components_num = 3;
            config_dec.image_config.components[0].width = config_dec.image_config.width;
            config_dec.image_config.components[0].height = config_dec.image_config.height;
            config_dec.image_config.components[0].byte_size = config_dec.image_config.width * config_dec.image_config.height * bps;
            config_dec.image_config.components[1].width = (config_dec.image_config.width + 1) / 2;
            config_dec.image_config.components[1].height = config_dec.image_config.height;
            config_dec.image_config.components[1].byte_size = config_dec.image_config.components[1].width * config_dec.image_config.components[1].height * bps;
            config_dec.image_config.components[2] = config_dec.image_config.components[1];
        } else {
            config_dec.image_config.components_num = 3;
            for (int c = 0; c < 3; ++c) {
                config_dec.image_config.components[c].width = config_dec.image_config.width;
                config_dec.image_config.components[c].height = config_dec.image_config.height;
                config_dec.image_config.components[c].byte_size = config_dec.image_config.width * config_dec.image_config.height * bps;
            }
        }
    }

    // allocate one output image buffer (contiguous)
    uint32_t out_frame_capacity = frame_size_from_cfg(&config_dec.image_config);
    uint8_t* out_frame = (uint8_t*)malloc(out_frame_capacity);
    if (!out_frame) { fprintf(stderr, "Memory allocation failed\n"); return SvtJxsErrorInsufficientResources; }

    // Open VFW codec DLL and decompressor instance
    HINSTANCE lib = LoadLibraryA("SvtJpegxsVfwCodec.dll");
    if (!lib) { fprintf(stderr, "Failed to load SvtJpegxsVfwCodec.dll\n"); return SvtJxsErrorUndefined; }
    FARPROC drv = GetProcAddress(lib, "DriverProc");
    HIC hic = ICOpen(mmioFOURCC('S','J','X','S'), mmioFOURCC('J','X','S','D'), ICMODE_DECOMPRESS);
    if (!hic && drv) {
        hic = ICOpenFunction(mmioFOURCC('S','J','X','S'), mmioFOURCC('J','X','S','D'), ICMODE_DECOMPRESS, drv);
    }
    if (!hic) { fprintf(stderr, "ICOpen failed for decompressor\n"); FreeLibrary(lib); return SvtJxsErrorUndefined; }
    if (ICSendMessage(hic, ICM_DECOMPRESS_BEGIN, 0, 0) != ICERR_OK) {
        fprintf(stderr, "ICM_DECOMPRESS_BEGIN failed\n"); ICClose(hic); FreeLibrary(lib); return SvtJxsErrorUndefined; }

    uint8_t* bitstream_ptr = config_dec.bitstream_buf_ref + config_dec.bitstream_offset;
    uint64_t bitstream_size = config_dec.bitstream_buf_size - config_dec.bitstream_offset;
    uint64_t frames_done = 0;
    while (bitstream_size > 0 && (config_dec.frames_count == 0 || frames_done < config_dec.frames_count)) {
        uint32_t fs = 0;
        SvtJxsErrorType_t sz_ret = svt_jpeg_xs_decoder_get_single_frame_size(bitstream_ptr, (uint32_t)bitstream_size, NULL, &fs, 1);
        if (sz_ret != SvtJxsErrorNone || fs == 0 || fs > bitstream_size) break;
        SJXS_Decompress icd = {0};
        icd.in = bitstream_ptr;
        icd.in_size = fs;
        icd.out = out_frame;
        icd.out_capacity = out_frame_capacity;
        if (ICSendMessage(hic, ICM_DECOMPRESS, (LPARAM)&icd, sizeof(icd)) != ICERR_OK) {
            fprintf(stderr, "ICM_DECOMPRESS failed\n"); return_error = SvtJxsErrorUndefined; break;
        }
        if (config_dec.out_file) { size_t wr = fwrite(out_frame, 1, icd.out_used, config_dec.out_file); if (wr != icd.out_used) break; }
        bitstream_ptr += fs;
        bitstream_size -= fs;
        frames_done++;
    }

    ICSendMessage(hic, ICM_DECOMPRESS_END, 0, 0);
    ICClose(hic);
    FreeLibrary(lib);
    free(out_frame);
    free(config_dec.bitstream_buf_ref);
    if (config_dec.in_file) fclose(config_dec.in_file);
    if (config_dec.out_file) fclose(config_dec.out_file);
    return (int32_t)return_error;
}
