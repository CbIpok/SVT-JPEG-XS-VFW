// Minimal VFW codec wrapper around BufferCodec
#include <windows.h>
#include <vfw.h>
#include <stdint.h>
#include <string.h>

#include "BufferCodec.h"

#ifndef ICERR_OK
#define ICERR_OK 0
#endif
#ifndef ICERR_ERROR
#define ICERR_ERROR (-1)
#endif
#ifndef ICERR_UNSUPPORTED
#define ICERR_UNSUPPORTED (-2)
#endif

// Simple private structs passed via ICM_* messages
typedef struct SJXS_Compress {
    const uint8_t* in;
    uint32_t in_size;
    uint8_t* out;
    uint32_t out_capacity;
    uint32_t out_used; // written by codec
} SJXS_Compress;

typedef struct SJXS_Decompress {
    const uint8_t* in;
    uint32_t in_size;
    uint8_t* out;
    uint32_t out_capacity;
    uint32_t out_used; // written by codec
} SJXS_Decompress;

typedef struct VfwCodecCtx {
    buffer_encoder_t* enc;
    buffer_decoder_t* dec;
    buffer_image_config_t img;
    uint32_t bs_capacity;
    int initialized_enc;
    int initialized_dec;
} VfwCodecCtx;

static VfwCodecCtx* ctx_alloc(void) {
    VfwCodecCtx* c = (VfwCodecCtx*)calloc(1, sizeof(VfwCodecCtx));
    return c;
}

static void ctx_free(VfwCodecCtx* c) {
    if (!c) return;
    if (c->enc) {
        buffer_encoder_destroy(c->enc);
        c->enc = NULL;
    }
    if (c->dec) {
        buffer_decoder_destroy(c->dec);
        c->dec = NULL;
    }
    free(c);
}

static LRESULT on_icm_compress_begin(VfwCodecCtx* c) {
    if (!c) return ICERR_ERROR;
    if (c->initialized_enc) return ICERR_OK;
    c->enc = buffer_encoder_create();
    if (!c->enc) return ICERR_ERROR;
    if (buffer_encoder_get_image_config(c->enc, &c->img, &c->bs_capacity) != 0) return ICERR_ERROR;
    c->initialized_enc = 1;
    return ICERR_OK;
}

static LRESULT on_icm_compress(VfwCodecCtx* c, SJXS_Compress* ic) {
    if (!c || !ic || !c->enc) return ICERR_ERROR;
    uint32_t used = 0;
    int r = buffer_encoder_encode_frame(c->enc, ic->in, ic->out, ic->out_capacity, &used);
    if (r != 0) return ICERR_ERROR;
    ic->out_used = used;
    return ICERR_OK;
}

static LRESULT on_icm_compress_end(VfwCodecCtx* c) {
    if (!c) return ICERR_ERROR;
    if (c->enc) buffer_encoder_destroy(c->enc);
    c->enc = NULL;
    c->initialized_enc = 0;
    c->bs_capacity = 0;
    memset(&c->img, 0, sizeof(c->img));
    return ICERR_OK;
}

static LRESULT on_icm_decompress_begin(VfwCodecCtx* c) {
    if (!c) return ICERR_ERROR;
    // Delay actual decoder creation until first frame (need bitstream)
    c->initialized_dec = 1;
    return ICERR_OK;
}

static LRESULT on_icm_decompress(VfwCodecCtx* c, SJXS_Decompress* icd) {
    if (!c || !icd) return ICERR_ERROR;
    if (!c->dec) {
        c->dec = buffer_decoder_create_from_bitstream(icd->in, icd->in_size, &c->img);
        if (!c->dec) return ICERR_ERROR;
    }
    uint32_t used = 0;
    int r = buffer_decoder_decode_frame(c->dec, icd->in, icd->in_size, icd->out, icd->out_capacity, &used);
    if (r != 0) return ICERR_ERROR;
    icd->out_used = used;
    return ICERR_OK;
}

static LRESULT on_icm_decompress_end(VfwCodecCtx* c) {
    if (!c) return ICERR_ERROR;
    if (c->dec) buffer_decoder_destroy(c->dec);
    c->dec = NULL;
    c->initialized_dec = 0;
    memset(&c->img, 0, sizeof(c->img));
    return ICERR_OK;
}

__declspec(dllexport) LRESULT CALLBACK DriverProc(DWORD_PTR dwDriverId, HDRVR hdrvr, UINT uMsg, LPARAM lParam1, LPARAM lParam2) {
    (void)hdrvr;
    VfwCodecCtx* c = (VfwCodecCtx*)dwDriverId;
    switch (uMsg) {
    case DRV_OPEN:
        return (LRESULT)ctx_alloc();
    case DRV_CLOSE:
        ctx_free(c);
        return 1;
    case ICM_COMPRESS_QUERY:
        return ICERR_OK;
    case ICM_COMPRESS_BEGIN:
        return on_icm_compress_begin(c);
    case ICM_COMPRESS:
        return on_icm_compress(c, (SJXS_Compress*)lParam1);
    case ICM_COMPRESS_END:
        return on_icm_compress_end(c);
    case ICM_DECOMPRESS_QUERY:
        return ICERR_OK;
    case ICM_DECOMPRESS_BEGIN:
        return on_icm_decompress_begin(c);
    case ICM_DECOMPRESS:
        return on_icm_decompress(c, (SJXS_Decompress*)lParam1);
    case ICM_DECOMPRESS_END:
        return on_icm_decompress_end(c);
    default:
        return ICERR_UNSUPPORTED;
    }
}
