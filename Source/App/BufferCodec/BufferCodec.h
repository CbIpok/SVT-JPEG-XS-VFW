/*
 * Simple buffer-to-buffer codec wrappers with C-only public API
 * All configuration is controlled by preprocessor defines below.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time configuration (override via -D...)
 * Encoder image params and threading/profile options
 */
#ifndef BUFFER_CODEC_WIDTH
#define BUFFER_CODEC_WIDTH 3840
#endif
#ifndef BUFFER_CODEC_HEIGHT
#define BUFFER_CODEC_HEIGHT 2160
#endif
#ifndef BUFFER_CODEC_BIT_DEPTH
#define BUFFER_CODEC_BIT_DEPTH 8
#endif
/* Colour format: 0=YUV400,1=YUV420,2=YUV422,3=YUV444 */
#ifndef BUFFER_CODEC_COLOUR_FORMAT
#define BUFFER_CODEC_COLOUR_FORMAT 2 /* YUV422 */
#endif
#ifndef BUFFER_CODEC_BPP_NUM
#define BUFFER_CODEC_BPP_NUM 4
#endif
#ifndef BUFFER_CODEC_BPP_DEN
#define BUFFER_CODEC_BPP_DEN 1
#endif
#ifndef BUFFER_CODEC_DECOMP_V
#define BUFFER_CODEC_DECOMP_V 2
#endif
#ifndef BUFFER_CODEC_DECOMP_H
#define BUFFER_CODEC_DECOMP_H 5
#endif
#ifndef BUFFER_CODEC_QUANT
#define BUFFER_CODEC_QUANT 0 /* deadzone */
#endif
#ifndef BUFFER_CODEC_SLICE_HEIGHT
#define BUFFER_CODEC_SLICE_HEIGHT 16
#endif
#ifndef BUFFER_CODEC_THREADS
#define BUFFER_CODEC_THREADS 32
#endif
#ifndef BUFFER_CODEC_PROFILE
#define BUFFER_CODEC_PROFILE 0 /* low latency */
#endif

typedef struct buffer_encoder buffer_encoder_t;
typedef struct buffer_decoder buffer_decoder_t;

typedef struct buffer_image_config_component {
    uint32_t width;
    uint32_t height;
    uint32_t byte_size;
} buffer_image_config_component_t;

typedef struct buffer_image_config {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t components_num;
    buffer_image_config_component_t comp[4];
} buffer_image_config_t;

/* ENCODER */
buffer_encoder_t* buffer_encoder_create(void);
buffer_encoder_t* buffer_encoder_create_with_params(uint32_t width,
                                                    uint32_t height,
                                                    uint8_t bit_depth,
                                                    int colour_format /* 0=YUV400,1=YUV420,2=YUV422,3=YUV444 */,
                                                    uint32_t bpp_num,
                                                    uint32_t bpp_den,
                                                    uint8_t ndecomp_v,
                                                    uint8_t ndecomp_h,
                                                    uint8_t quant,
                                                    uint32_t slice_height,
                                                    uint32_t threads,
                                                    uint8_t profile);
int buffer_encoder_get_image_config(buffer_encoder_t* enc, buffer_image_config_t* out_cfg, uint32_t* out_frame_bytes_capacity);
/* in_image points to a contiguous planar buffer in the order of components from buffer_image_config_t */
int buffer_encoder_encode_frame(buffer_encoder_t* enc,
                                const uint8_t* in_image,
                                uint8_t* out_bitstream,
                                uint32_t out_capacity,
                                uint32_t* out_used);
void buffer_encoder_destroy(buffer_encoder_t* enc);

/* DECODER */
buffer_decoder_t* buffer_decoder_create_from_bitstream(const uint8_t* bitstream,
                                                       size_t bitstream_size,
                                                       buffer_image_config_t* out_cfg);
/* out_image points to a contiguous planar buffer with capacity out_capacity bytes */
int buffer_decoder_decode_frame(buffer_decoder_t* dec,
                                const uint8_t* bitstream,
                                uint32_t bitstream_size,
                                uint8_t* out_image,
                                uint32_t out_capacity,
                                uint32_t* out_used);
void buffer_decoder_destroy(buffer_decoder_t* dec);

#ifdef __cplusplus
}
#endif
