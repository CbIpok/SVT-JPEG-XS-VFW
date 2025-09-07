/* Implementation includes SVT headers, public header is C-only */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "BufferCodec.h"
#include "SvtJpegxs.h"
#include "SvtJpegxsEnc.h"
#include "SvtJpegxsDec.h"
#include "SvtJpegxsImageBufferTools.h"

struct buffer_encoder {
    svt_jpeg_xs_encoder_api_t enc;
    svt_jpeg_xs_frame_pool_t* pool;
    svt_jpeg_xs_image_config_t img;
    uint32_t bitstream_capacity;
};

struct buffer_decoder {
    svt_jpeg_xs_decoder_api_t dec;
    svt_jpeg_xs_frame_pool_t* pool; // image buffers only
    svt_jpeg_xs_image_config_t img;
};

static void copy_image_to_pool_contig(const svt_jpeg_xs_image_config_t* cfg,
                                      const uint8_t* src_contig,
                                      svt_jpeg_xs_image_buffer_t* dst) {
    size_t off = 0;
    for (uint8_t c = 0; c < cfg->components_num; ++c) {
        memcpy(dst->data_yuv[c], src_contig + off, cfg->components[c].byte_size);
        off += cfg->components[c].byte_size;
    }
}

static void copy_image_from_pool_to_contig(const svt_jpeg_xs_image_config_t* cfg,
                                           const svt_jpeg_xs_image_buffer_t* src,
                                           uint8_t* dst_contig) {
    size_t off = 0;
    for (uint8_t c = 0; c < cfg->components_num; ++c) {
        memcpy(dst_contig + off, src->data_yuv[c], cfg->components[c].byte_size);
        off += cfg->components[c].byte_size;
    }
}

static ColourFormat_t map_colour_format(int fmt) {
    switch (fmt) {
        case 0: return COLOUR_FORMAT_PLANAR_YUV400;
        case 1: return COLOUR_FORMAT_PLANAR_YUV420;
        case 2: return COLOUR_FORMAT_PLANAR_YUV422;
        case 3: return COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
        default: return COLOUR_FORMAT_PLANAR_YUV420;
    }
}

buffer_encoder_t* buffer_encoder_create(void) {
    buffer_encoder_t* h = (buffer_encoder_t*)calloc(1, sizeof(*h));
    if (!h) return NULL;
    svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &h->enc);
    h->enc.source_width = BUFFER_CODEC_WIDTH;
    h->enc.source_height = BUFFER_CODEC_HEIGHT;
    h->enc.input_bit_depth = BUFFER_CODEC_BIT_DEPTH;
    h->enc.colour_format = map_colour_format(BUFFER_CODEC_COLOUR_FORMAT);
    h->enc.bpp_numerator = BUFFER_CODEC_BPP_NUM;
    h->enc.bpp_denominator = BUFFER_CODEC_BPP_DEN;
    h->enc.ndecomp_v = BUFFER_CODEC_DECOMP_V;
    h->enc.ndecomp_h = BUFFER_CODEC_DECOMP_H;
    h->enc.quantization = BUFFER_CODEC_QUANT;
    h->enc.slice_height = BUFFER_CODEC_SLICE_HEIGHT;
    h->enc.threads_num = BUFFER_CODEC_THREADS;
    h->enc.cpu_profile = BUFFER_CODEC_PROFILE;

    uint32_t bytes_per_frame = 0;
    if (svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR,
                                             SVT_JPEGXS_API_VER_MINOR,
                                             &h->enc,
                                             &h->img,
                                             &bytes_per_frame) != SvtJxsErrorNone) {
        free(h);
        return NULL;
    }
    h->bitstream_capacity = bytes_per_frame;

    // Allocate pool with both image and bitstream buffers
    h->pool = svt_jpeg_xs_frame_pool_alloc(&h->img, bytes_per_frame, 3);
    if (!h->pool) {
        free(h);
        return NULL;
    }

    if (svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                  SVT_JPEGXS_API_VER_MINOR,
                                  &h->enc) != SvtJxsErrorNone) {
        svt_jpeg_xs_frame_pool_free(h->pool);
        free(h);
        return NULL;
    }
    return h;
}

int buffer_encoder_get_image_config(buffer_encoder_t* h, buffer_image_config_t* out_cfg, uint32_t* out_frame_bytes_capacity) {
    if (!h || !out_cfg) return -1;
    out_cfg->width = h->img.width;
    out_cfg->height = h->img.height;
    out_cfg->bit_depth = h->img.bit_depth;
    out_cfg->components_num = h->img.components_num;
    for (uint8_t c = 0; c < h->img.components_num; ++c) {
        out_cfg->comp[c].width = h->img.components[c].width;
        out_cfg->comp[c].height = h->img.components[c].height;
        out_cfg->comp[c].byte_size = h->img.components[c].byte_size;
    }
    if (out_frame_bytes_capacity) *out_frame_bytes_capacity = h->bitstream_capacity;
    return 0;
}

int buffer_encoder_encode_frame(buffer_encoder_t* h,
                                const uint8_t* in_image,
                                uint8_t* out_bitstream,
                                uint32_t out_capacity,
                                uint32_t* out_used) {
    if (!h || !in_image || !out_bitstream || out_capacity == 0) return -1;
    if (out_used) *out_used = 0;

    svt_jpeg_xs_frame_t enc_input;
    SvtJxsErrorType_t ret = svt_jpeg_xs_frame_pool_get(h->pool, &enc_input, /*blocking*/ 1);
    if (ret != SvtJxsErrorNone) return -1;

    // Fill input image data by copying from user buffers
    copy_image_to_pool_contig(&h->img, in_image, &enc_input.image);

    ret = svt_jpeg_xs_encoder_send_picture(&h->enc, &enc_input, /*blocking*/ 1);
    if (ret != SvtJxsErrorNone) {
        // Return buffer to pool before exit
        svt_jpeg_xs_frame_pool_release(h->pool, &enc_input);
        return -1;
    }

    uint32_t produced = 0;
    int last = 0;
    while (!last) {
        svt_jpeg_xs_frame_t enc_output;
        ret = svt_jpeg_xs_encoder_get_packet(&h->enc, &enc_output, /*blocking*/ 1);
        if (ret != SvtJxsErrorNone) {
            svt_jpeg_xs_frame_pool_release(h->pool, &enc_input);
            return -1;
        }
        if (produced + enc_output.bitstream.used_size > out_capacity) {
            svt_jpeg_xs_frame_pool_release(h->pool, &enc_output);
            svt_jpeg_xs_frame_pool_release(h->pool, &enc_input);
            return -2;
        }
        memcpy(out_bitstream + produced, enc_output.bitstream.buffer, enc_output.bitstream.used_size);
        produced += enc_output.bitstream.used_size;
        last = enc_output.bitstream.last_packet_in_frame;
        svt_jpeg_xs_frame_pool_release(h->pool, &enc_output);
    }
    // Now it's safe to release the input image buffer for reuse
    svt_jpeg_xs_frame_pool_release(h->pool, &enc_input);

    if (out_used) *out_used = produced;
    return 0;
}

void buffer_encoder_destroy(buffer_encoder_t* h) {
    if (!h) return;
    svt_jpeg_xs_frame_pool_free(h->pool);
    svt_jpeg_xs_encoder_close(&h->enc);
    free(h);
}

buffer_decoder_t* buffer_decoder_create_from_bitstream(const uint8_t* bitstream,
                                                       size_t bitstream_size,
                                                       buffer_image_config_t* out_image_config) {
    if (!bitstream || bitstream_size == 0) return NULL;
    buffer_decoder_t* h = (buffer_decoder_t*)calloc(1, sizeof(*h));
    if (!h) return NULL;
    svt_jpeg_xs_decoder_api_t dec = {0};
    dec.threads_num = BUFFER_CODEC_THREADS;
    dec.packetization_mode = 0; /* frame based */
    h->dec = dec;
    svt_jpeg_xs_image_config_t img = {0};
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR,
                                                     SVT_JPEGXS_API_VER_MINOR,
                                                     &h->dec,
                                                     bitstream,
                                                     bitstream_size,
                                                     &img);
    if (ret != SvtJxsErrorNone) {
        free(h);
        return NULL;
    }
    h->img = img;
    if (out_image_config) {
        out_image_config->width = img.width;
        out_image_config->height = img.height;
        out_image_config->bit_depth = img.bit_depth;
        out_image_config->components_num = img.components_num;
        for (uint8_t c = 0; c < img.components_num; ++c) {
            out_image_config->comp[c].width = img.components[c].width;
            out_image_config->comp[c].height = img.components[c].height;
            out_image_config->comp[c].byte_size = img.components[c].byte_size;
        }
    }
    h->pool = svt_jpeg_xs_frame_pool_alloc(&h->img, 0, 3);
    if (!h->pool) {
        svt_jpeg_xs_decoder_close(&h->dec);
        free(h);
        return NULL;
    }
    return h;
}

int buffer_decoder_decode_frame(buffer_decoder_t* h,
                                const uint8_t* bitstream,
                                uint32_t bitstream_size,
                                uint8_t* out_image,
                                uint32_t out_capacity,
                                uint32_t* out_used) {
    if (!h || !bitstream || !out_image) return -1;
    svt_jpeg_xs_bitstream_buffer_t bs;
    bs.buffer = (uint8_t*)bitstream;
    bs.used_size = bitstream_size;
    bs.allocation_size = bitstream_size;

    svt_jpeg_xs_frame_t dec_input;
    SvtJxsErrorType_t ret = svt_jpeg_xs_frame_pool_get(h->pool, &dec_input, /*blocking*/ 1);
    if (ret != SvtJxsErrorNone) return -1;
    dec_input.bitstream = bs;
    ret = svt_jpeg_xs_decoder_send_frame(&h->dec, &dec_input, /*blocking*/ 1);
    if (ret != SvtJxsErrorNone) {
        svt_jpeg_xs_frame_pool_release(h->pool, &dec_input);
        return -1;
    }

    svt_jpeg_xs_frame_t dec_output;
    ret = svt_jpeg_xs_decoder_get_frame(&h->dec, &dec_output, /*blocking*/ 1);
    if (ret != SvtJxsErrorNone) {
        svt_jpeg_xs_frame_pool_release(h->pool, &dec_input);
        return -1;
    }
    // calc required size
    uint32_t need = 0;
    for (uint8_t c = 0; c < h->img.components_num; ++c) need += h->img.components[c].byte_size;
    if (out_capacity < need) {
        svt_jpeg_xs_frame_pool_release(h->pool, &dec_output);
        svt_jpeg_xs_frame_pool_release(h->pool, &dec_input);
        return -2;
    }
    copy_image_from_pool_to_contig(&h->img, &dec_output.image, out_image);
    if (out_used) *out_used = need;
    svt_jpeg_xs_frame_pool_release(h->pool, &dec_output);
    svt_jpeg_xs_frame_pool_release(h->pool, &dec_input);
    return 0;
}

void buffer_decoder_destroy(buffer_decoder_t* h) {
    if (!h) return;
    svt_jpeg_xs_frame_pool_free(h->pool);
    svt_jpeg_xs_decoder_close(&h->dec);
    free(h);
}
