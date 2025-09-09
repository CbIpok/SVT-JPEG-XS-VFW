// Minimal VFW codec wrapper around BufferCodec
#include <windows.h>
#include <vfw.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

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
    // temp buffers
    uint8_t* tmp_in;
    uint32_t tmp_in_size;
    uint8_t* tmp_out;
    uint32_t tmp_out_size;
    // vfw formats
    DWORD in_fourcc;
    DWORD out_fourcc;
    int width;
    int height;
} VfwCodecCtx;

static void log_line(const char* tag, const char* msg) {
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH) return;
    strcat_s(path, MAX_PATH, "SvtJpegxsVfwCodec.log");
    FILE* f = NULL;
    if (fopen_s(&f, path, "a+") == 0 && f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                tag, msg);
        fclose(f);
    }
    OutputDebugStringA(msg);
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved) {
    (void)hInstance; (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH: log_line("DllMain", "PROCESS_ATTACH"); break;
    case DLL_THREAD_ATTACH:  log_line("DllMain", "THREAD_ATTACH"); break;
    case DLL_THREAD_DETACH:  log_line("DllMain", "THREAD_DETACH"); break;
    case DLL_PROCESS_DETACH: log_line("DllMain", "PROCESS_DETACH"); break;
    }
    return TRUE;
}

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
    if (c->tmp_in) free(c->tmp_in);
    if (c->tmp_out) free(c->tmp_out);
    free(c);
}

static LRESULT on_icm_compress_begin(VfwCodecCtx* c) {
    if (!c) return ICERR_ERROR;
    if (c->initialized_enc) return ICERR_OK;
    if (c->width > 0 && c->height > 0) {
        c->enc = buffer_encoder_create_with_params((uint32_t)c->width,
                                                   (uint32_t)c->height,
                                                   8,
                                                   1 /* YUV420 */,
                                                   BUFFER_CODEC_BPP_NUM,
                                                   BUFFER_CODEC_BPP_DEN,
                                                   BUFFER_CODEC_DECOMP_V,
                                                   BUFFER_CODEC_DECOMP_H,
                                                   BUFFER_CODEC_QUANT,
                                                   BUFFER_CODEC_SLICE_HEIGHT,
                                                   BUFFER_CODEC_THREADS,
                                                   BUFFER_CODEC_PROFILE);
    } else {
        c->enc = buffer_encoder_create();
    }
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
    case DRV_LOAD:         log_line("DriverProc", "DRV_LOAD"); return 1;
    case DRV_FREE:         log_line("DriverProc", "DRV_FREE"); return 1;
    case DRV_ENABLE:       log_line("DriverProc", "DRV_ENABLE"); return 1;
    case DRV_DISABLE:      log_line("DriverProc", "DRV_DISABLE"); return 1;
    case DRV_OPEN:
        log_line("DriverProc", "DRV_OPEN");
        return (LRESULT)ctx_alloc();
    case DRV_CLOSE:
        log_line("DriverProc", "DRV_CLOSE");
        ctx_free(c);
        return 1;
    case ICM_GETINFO:
        log_line("DriverProc", "ICM_GETINFO");
        if (lParam1 && lParam2 >= sizeof(ICINFO)) {
            ICINFO* info = (ICINFO*)lParam1;
            memset(info, 0, sizeof(*info));
            info->dwSize = sizeof(ICINFO);
            info->fccType = ICTYPE_VIDEO; // mmioFOURCC('v','i','d','c')
            info->fccHandler = mmioFOURCC('S','J','X','S');
            info->dwVersion = 0x00010000;
            info->dwVersionICM = 0x00010000;
            info->dwFlags = 0; // no ABOUT/CONFIG dialogs
            const wchar_t* name = L"SVT JPEG XS";
            const wchar_t* desc = L"SVT JPEG XS VFW Codec";
            const wchar_t* drv = L"SvtJpegxsVfwCodec.dll";
            wcsncpy(info->szName, name, sizeof(info->szName)/sizeof(info->szName[0]) - 1);
            wcsncpy(info->szDescription, desc, sizeof(info->szDescription)/sizeof(info->szDescription[0]) - 1);
            wcsncpy(info->szDriver, drv, sizeof(info->szDriver)/sizeof(info->szDriver[0]) - 1);
            return sizeof(ICINFO);
        }
        return ICERR_UNSUPPORTED;
    case ICM_COMPRESS_QUERY:
        log_line("DriverProc", "ICM_COMPRESS_QUERY");
        // For VirtualDub, ensure input is YV12 or I420 8-bit planar
        if (lParam1) {
            LPBITMAPINFOHEADER lpbi = (LPBITMAPINFOHEADER)lParam1;
            if (lpbi->biCompression == mmioFOURCC('Y','V','1','2') || lpbi->biCompression == mmioFOURCC('I','4','2','0')) {
                if (!(lpbi->biWidth & 1) && !(lpbi->biHeight & 1) && lpbi->biBitCount == 12) return ICERR_OK;
                return ICERR_BADFORMAT;
            }
            return ICERR_BADFORMAT;
        }
        return ICERR_OK;
    case ICM_COMPRESS_GET_FORMAT: {
        log_line("DriverProc", "ICM_COMPRESS_GET_FORMAT");
        LPBITMAPINFOHEADER lpbiIn = (LPBITMAPINFOHEADER)lParam1;
        LPBITMAPINFOHEADER lpbiOut = (LPBITMAPINFOHEADER)lParam2;
        if (!lpbiIn) return ICERR_BADFORMAT;
        if (!lpbiOut) return sizeof(BITMAPINFOHEADER);
        memset(lpbiOut, 0, sizeof(BITMAPINFOHEADER));
        lpbiOut->biSize = sizeof(BITMAPINFOHEADER);
        lpbiOut->biWidth = lpbiIn->biWidth;
        lpbiOut->biHeight = lpbiIn->biHeight;
        lpbiOut->biPlanes = 1;
        lpbiOut->biBitCount = 0; // compressed
        lpbiOut->biCompression = mmioFOURCC('S','J','X','S');
        DWORD w = (DWORD)lpbiIn->biWidth, h = (DWORD)lpbiIn->biHeight;
        lpbiOut->biSizeImage = w*h*2; // conservative
        return ICERR_OK;
    }
    case ICM_COMPRESS_GET_SIZE: {
        log_line("DriverProc", "ICM_COMPRESS_GET_SIZE");
        LPBITMAPINFOHEADER lpbiIn = (LPBITMAPINFOHEADER)lParam1;
        if (!lpbiIn) return ICERR_BADFORMAT;
        DWORD w = (DWORD)lpbiIn->biWidth, h = (DWORD)lpbiIn->biHeight;
        return (LRESULT)(w*h*2);
    }
    case ICM_COMPRESS_BEGIN:
        log_line("DriverProc", "ICM_COMPRESS_BEGIN");
        // VirtualDub passes input/output headers here
        if (lParam1) {
            LPBITMAPINFOHEADER lpbiIn = (LPBITMAPINFOHEADER)lParam1;
            if (c) { c->width = lpbiIn->biWidth; c->height = lpbiIn->biHeight; c->in_fourcc = lpbiIn->biCompression; }
        }
        return on_icm_compress_begin(c);
    case ICM_COMPRESS:
        log_line("DriverProc", "ICM_COMPRESS");
        if (lParam2 == sizeof(SJXS_Compress)) {
            return on_icm_compress(c, (SJXS_Compress*)lParam1);
        } else {
            // Treat as ICCOMPRESS
            if (!c || !c->enc) return ICERR_ERROR;
            ICCOMPRESS* ic = (ICCOMPRESS*)lParam1;
            if (!ic || !ic->lpInput || !ic->lpbiInput || !ic->lpOutput || !ic->lpbiOutput) return ICERR_BADPARAM;
            const uint8_t* in = (const uint8_t*)ic->lpInput;
            uint8_t* out = (uint8_t*)ic->lpOutput;
            DWORD out_cap = ic->lpbiOutput->biSizeImage;
            uint32_t w = (uint32_t)ic->lpbiInput->biWidth, h = (uint32_t)ic->lpbiInput->biHeight;
            uint32_t ysz = w*h; uint32_t csz = (w/2)*(h/2); uint32_t need = ysz + 2*csz;
            if (c->tmp_in_size < need) { free(c->tmp_in); c->tmp_in = (uint8_t*)malloc(need); c->tmp_in_size = need; }
            if (!c->tmp_in) return ICERR_MEMORY;
            const uint8_t* y = in; const uint8_t* p1 = in + ysz; const uint8_t* p2 = in + ysz + csz;
            if (c->in_fourcc == mmioFOURCC('Y','V','1','2')) { memcpy(c->tmp_in, y, ysz); memcpy(c->tmp_in+ysz, p2, csz); memcpy(c->tmp_in+ysz+csz, p1, csz);} else { memcpy(c->tmp_in, y, need);}            
            uint32_t used = 0; int r = buffer_encoder_encode_frame(c->enc, c->tmp_in, out, out_cap, &used);
            if (r != 0) return ICERR_ERROR;
            ic->lpbiOutput->biSizeImage = used; if (ic->lpdwFlags) *ic->lpdwFlags = 0; return ICERR_OK;
        }
    case ICM_COMPRESS_END:
        log_line("DriverProc", "ICM_COMPRESS_END");
        return on_icm_compress_end(c);
    case ICM_DECOMPRESS_QUERY:
        log_line("DriverProc", "ICM_DECOMPRESS_QUERY");
        if (!lParam1) return ICERR_BADFORMAT;
        if (((LPBITMAPINFOHEADER)lParam1)->biCompression != mmioFOURCC('S','J','X','S')) return ICERR_BADFORMAT;
        if (lParam2) {
            DWORD outfcc = ((LPBITMAPINFOHEADER)lParam2)->biCompression;
            if (!(outfcc == mmioFOURCC('Y','V','1','2') || outfcc == mmioFOURCC('I','4','2','0'))) return ICERR_BADFORMAT;
        }
        return ICERR_OK;
    case ICM_DECOMPRESS_GET_FORMAT: {
        log_line("DriverProc", "ICM_DECOMPRESS_GET_FORMAT");
        LPBITMAPINFOHEADER lpbiIn = (LPBITMAPINFOHEADER)lParam1;
        LPBITMAPINFOHEADER lpbiOut = (LPBITMAPINFOHEADER)lParam2;
        if (!lpbiIn) return ICERR_BADFORMAT;
        if (!lpbiOut) return sizeof(BITMAPINFOHEADER);
        memset(lpbiOut, 0, sizeof(BITMAPINFOHEADER));
        lpbiOut->biSize = sizeof(BITMAPINFOHEADER);
        lpbiOut->biWidth = lpbiIn->biWidth;
        lpbiOut->biHeight = lpbiIn->biHeight;
        lpbiOut->biPlanes = 1;
        lpbiOut->biBitCount = 12;
        lpbiOut->biCompression = mmioFOURCC('Y','V','1','2');
        DWORD w = (DWORD)lpbiOut->biWidth, h = (DWORD)lpbiOut->biHeight;
        lpbiOut->biSizeImage = w*h + 2*((w/2)*(h/2));
        return ICERR_OK;
    }
    case ICM_DECOMPRESS_BEGIN:
        log_line("DriverProc", "ICM_DECOMPRESS_BEGIN");
        if (lParam2) c->out_fourcc = ((LPBITMAPINFOHEADER)lParam2)->biCompression; else c->out_fourcc = mmioFOURCC('Y','V','1','2');
        return on_icm_decompress_begin(c);
    case ICM_DECOMPRESS:
        log_line("DriverProc", "ICM_DECOMPRESS");
        if (lParam2 == sizeof(SJXS_Decompress)) {
            return on_icm_decompress(c, (SJXS_Decompress*)lParam1);
        } else {
            if (!c) return ICERR_ERROR;
            ICDECOMPRESS* icd = (ICDECOMPRESS*)lParam1;
            if (!icd || !icd->lpInput || !icd->lpbiInput || !icd->lpOutput || !icd->lpbiOutput) return ICERR_BADPARAM;
            const uint8_t* bitstream = (const uint8_t*)icd->lpInput;
            uint32_t bs_size = icd->lpbiInput->biSizeImage;
            if (!c->dec) {
                if (bs_size == 0) return ICERR_BADPARAM;
                c->dec = buffer_decoder_create_from_bitstream(bitstream, bs_size, &c->img);
                if (!c->dec) return ICERR_ERROR;
                c->width = (int)c->img.width; c->height = (int)c->img.height;
            }
            uint32_t ysz = c->img.width * c->img.height;
            uint32_t csz = (c->img.width/2) * (c->img.height/2);
            uint32_t need = ysz + 2*csz;
            if (c->tmp_out_size < need) { free(c->tmp_out); c->tmp_out = (uint8_t*)malloc(need); c->tmp_out_size = need; }
            if (!c->tmp_out) return ICERR_MEMORY;
            uint32_t out_used = 0; int r = buffer_decoder_decode_frame(c->dec, bitstream, bs_size, c->tmp_out, c->tmp_out_size, &out_used);
            if (r != 0) return ICERR_ERROR;
            uint8_t* dst = (uint8_t*)icd->lpOutput; uint8_t* src = c->tmp_out;
            const uint8_t* sY = src; const uint8_t* sU = src + ysz; const uint8_t* sV = src + ysz + csz;
            memcpy(dst, sY, ysz);
            if (c->out_fourcc == mmioFOURCC('I','4','2','0')) { memcpy(dst+ysz, sU, csz); memcpy(dst+ysz+csz, sV, csz);} else { memcpy(dst+ysz, sV, csz); memcpy(dst+ysz+csz, sU, csz);}            
            return ICERR_OK;
        }
    case ICM_DECOMPRESS_END:
        log_line("DriverProc", "ICM_DECOMPRESS_END");
        return on_icm_decompress_end(c);
    default:
        log_line("DriverProc", "UNKNOWN_MSG");
        return ICERR_UNSUPPORTED;
    }
}
