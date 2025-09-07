/* Buffer-based EncApp using VFW wrapper */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include <vfw.h>
#include "EncAppConfig.h"
#include "UtilityApp.h"
#include "BufferCodec.h" /* for BUFFER_CODEC_* macros only */

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

static uint32_t get_single_frame_size(const svt_jpeg_xs_image_config_t* image_config) {
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

typedef struct SJXS_Compress {
    const uint8_t* in;
    uint32_t in_size;
    uint8_t* out;
    uint32_t out_capacity;
    uint32_t out_used;
} SJXS_Compress;

static void fill_image_cfg_from_macros(svt_jpeg_xs_image_config_t* img) {
    memset(img, 0, sizeof(*img));
    img->width = BUFFER_CODEC_WIDTH;
    img->height = BUFFER_CODEC_HEIGHT;
    img->bit_depth = BUFFER_CODEC_BIT_DEPTH;
    if (BUFFER_CODEC_COLOUR_FORMAT == 0) img->format = COLOUR_FORMAT_PLANAR_YUV400;
    else if (BUFFER_CODEC_COLOUR_FORMAT == 1) img->format = COLOUR_FORMAT_PLANAR_YUV420;
    else if (BUFFER_CODEC_COLOUR_FORMAT == 2) img->format = COLOUR_FORMAT_PLANAR_YUV422;
    else img->format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    // derive components layout (planar)
    if (img->format == COLOUR_FORMAT_PLANAR_YUV400) {
        img->components_num = 1;
        img->components[0].width = img->width;
        img->components[0].height = img->height;
        img->components[0].byte_size = (img->width * img->height * ((img->bit_depth+7)/8));
    } else if (img->format == COLOUR_FORMAT_PLANAR_YUV420) {
        img->components_num = 3;
        const uint32_t bytes_per_sample = ((img->bit_depth + 7) / 8);
        img->components[0].width = img->width;
        img->components[0].height = img->height;
        img->components[0].byte_size = img->components[0].width * img->components[0].height * bytes_per_sample;
        img->components[1].width = (img->width + 1) / 2;
        img->components[1].height = (img->height + 1) / 2;
        img->components[1].byte_size = img->components[1].width * img->components[1].height * bytes_per_sample;
        img->components[2] = img->components[1];
    } else if (img->format == COLOUR_FORMAT_PLANAR_YUV422) {
        img->components_num = 3;
        const uint32_t bytes_per_sample = ((img->bit_depth + 7) / 8);
        img->components[0].width = img->width;
        img->components[0].height = img->height;
        img->components[0].byte_size = img->components[0].width * img->components[0].height * bytes_per_sample;
        img->components[1].width = (img->width + 1) / 2;
        img->components[1].height = img->height;
        img->components[1].byte_size = img->components[1].width * img->components[1].height * bytes_per_sample;
        img->components[2] = img->components[1];
    } else {
        // YUV444/RGB planar
        img->components_num = 3;
        const uint32_t bytes_per_sample = ((img->bit_depth + 7) / 8);
        for (int c = 0; c < 3; ++c) {
            img->components[c].width = img->width;
            img->components[c].height = img->height;
            img->components[c].byte_size = img->width * img->height * bytes_per_sample;
        }
    }
}

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

    // Derive image config from BufferCodec macros
    fill_image_cfg_from_macros(&config_enc.image_config);

    if (config_enc.frames_count == 0) {
        int64_t file_size = get_file_size(config_enc.in_file);
        uint32_t single_frame_size = get_single_frame_size(&config_enc.image_config);
        config_enc.frames_count = (uint32_t)(file_size / single_frame_size);
    }

    // Allocate contiguous input frame buffer
    uint32_t frame_size = get_single_frame_size(&config_enc.image_config);
    uint8_t* in_frame = (uint8_t*)malloc(frame_size);
    if (!in_frame) { fprintf(stderr, "Memory allocation failed\n"); return SvtJxsErrorInsufficientResources; }
    // Bitstream buffer: conservative capacity equals frame_size (BufferCodec reports capacity elsewhere, but we use BEGIN to init in DLL)
    uint32_t bs_capacity = frame_size; // will be enough for tests (4 bpp target)
    uint8_t* bs_buf = (uint8_t*)malloc(bs_capacity);
    if (!bs_buf) { fprintf(stderr, "Memory allocation failed\n"); return SvtJxsErrorInsufficientResources; }

    // Open VFW codec DLL and compressor instance
    HINSTANCE lib = LoadLibraryA("SvtJpegxsVfwCodec.dll");
    if (!lib) { fprintf(stderr, "Failed to load SvtJpegxsVfwCodec.dll\n"); return SvtJxsErrorUndefined; }
    FARPROC drv = GetProcAddress(lib, "DriverProc");
    HIC hic = ICOpen(mmioFOURCC('S','J','X','S'), mmioFOURCC('J','X','S','E'), ICMODE_COMPRESS);
    if (!hic && drv) {
        // Fallback to open via function pointer
        hic = ICOpenFunction(mmioFOURCC('S','J','X','S'), mmioFOURCC('J','X','S','E'), ICMODE_COMPRESS, drv);
    }
    if (!hic) { fprintf(stderr, "ICOpen failed for compressor\n"); FreeLibrary(lib); return SvtJxsErrorUndefined; }
    if (ICSendMessage(hic, ICM_COMPRESS_BEGIN, 0, 0) != ICERR_OK) {
        fprintf(stderr, "ICM_COMPRESS_BEGIN failed\n"); ICClose(hic); FreeLibrary(lib); return SvtJxsErrorUndefined;
    }

    uint64_t frames_done = 0;
    while (frames_done < config_enc.frames_count) {
        size_t rd = fread(in_frame, 1, frame_size, config_enc.in_file);
        if (rd != frame_size) {
            if (reset_yuv_file(config_enc.in_file) != 0) break;
            rd = fread(in_frame, 1, frame_size, config_enc.in_file);
            if (rd != frame_size) break;
        }
        SJXS_Compress ic = {0};
        ic.in = (const uint8_t*)in_frame;
        ic.in_size = frame_size;
        ic.out = bs_buf;
        ic.out_capacity = bs_capacity;
        if (ICSendMessage(hic, ICM_COMPRESS, (LPARAM)&ic, sizeof(ic)) != ICERR_OK) {
            fprintf(stderr, "ICM_COMPRESS failed\n"); return_error = SvtJxsErrorUndefined; break;
        }
        if (config_enc.out_file && ic.out_used) fwrite(bs_buf, 1, ic.out_used, config_enc.out_file);
        frames_done++;
        show_encoding_progress(&config_enc, frames_done);
    }

    ICSendMessage(hic, ICM_COMPRESS_END, 0, 0);
    ICClose(hic);
    FreeLibrary(lib);
    free(in_frame);
    free(bs_buf);
    if (config_enc.in_file) fclose(config_enc.in_file);
    if (config_enc.out_file) fclose(config_enc.out_file);
    return (int32_t)return_error;
}
