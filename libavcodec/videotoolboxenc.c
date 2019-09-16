/*
 * copyright (c) 2015 Rick Kern <kernrj@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <VideoToolbox/VideoToolbox.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreMedia/CoreMedia.h>
#include <TargetConditionals.h>
#include <Availability.h>
#include "avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavcodec/avcodec.h"
#include "libavutil/pixdesc.h"
#include "internal.h"
#include <pthread.h>
#include "h264.h"
#include "h264_sei.h"
#include <dlfcn.h>

#if !HAVE_KCMVIDEOCODECTYPE_HEVC
enum { kCMVideoCodecType_HEVC = 'hvc1' };
#endif

#if !HAVE_KCVPIXELFORMATTYPE_420YPCBCR10BIPLANARVIDEORANGE
enum { kCVPixelFormatType_420YpCbCr10BiPlanarFullRange = 'xf20' };
enum { kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange = 'x420' };
#endif

typedef OSStatus (*getParameterSetAtIndex)(CMFormatDescriptionRef videoDesc,
                                           size_t parameterSetIndex,
                                           const uint8_t **parameterSetPointerOut,
                                           size_t *parameterSetSizeOut,
                                           size_t *parameterSetCountOut,
                                           int *NALUnitHeaderLengthOut);

//These symbols may not be present
static struct{
    CFStringRef kCVImageBufferColorPrimaries_ITU_R_2020;
    CFStringRef kCVImageBufferTransferFunction_ITU_R_2020;
    CFStringRef kCVImageBufferYCbCrMatrix_ITU_R_2020;

    CFStringRef kVTCompressionPropertyKey_H264EntropyMode;
    CFStringRef kVTH264EntropyMode_CAVLC;
    CFStringRef kVTH264EntropyMode_CABAC;

    CFStringRef kVTProfileLevel_H264_Baseline_4_0;
    CFStringRef kVTProfileLevel_H264_Baseline_4_2;
    CFStringRef kVTProfileLevel_H264_Baseline_5_0;
    CFStringRef kVTProfileLevel_H264_Baseline_5_1;
    CFStringRef kVTProfileLevel_H264_Baseline_5_2;
    CFStringRef kVTProfileLevel_H264_Baseline_AutoLevel;
    CFStringRef kVTProfileLevel_H264_Main_4_2;
    CFStringRef kVTProfileLevel_H264_Main_5_1;
    CFStringRef kVTProfileLevel_H264_Main_5_2;
    CFStringRef kVTProfileLevel_H264_Main_AutoLevel;
    CFStringRef kVTProfileLevel_H264_High_3_0;
    CFStringRef kVTProfileLevel_H264_High_3_1;
    CFStringRef kVTProfileLevel_H264_High_3_2;
    CFStringRef kVTProfileLevel_H264_High_4_0;
    CFStringRef kVTProfileLevel_H264_High_4_1;
    CFStringRef kVTProfileLevel_H264_High_4_2;
    CFStringRef kVTProfileLevel_H264_High_5_1;
    CFStringRef kVTProfileLevel_H264_High_5_2;
    CFStringRef kVTProfileLevel_H264_High_AutoLevel;
    CFStringRef kVTProfileLevel_H264_Extended_5_0;
    CFStringRef kVTProfileLevel_H264_Extended_AutoLevel;

    CFStringRef kVTProfileLevel_HEVC_Main_AutoLevel;
    CFStringRef kVTProfileLevel_HEVC_Main10_AutoLevel;

    CFStringRef kVTCompressionPropertyKey_RealTime;

    CFStringRef kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder;
    CFStringRef kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder;

    getParameterSetAtIndex CMVideoFormatDescriptionGetHEVCParameterSetAtIndex;
} compat_keys;

#define GET_SYM(symbol, defaultVal)                                     \
do{                                                                     \
    CFStringRef* handle = (CFStringRef*)dlsym(RTLD_DEFAULT, #symbol);   \
    if(!handle)                                                         \
        compat_keys.symbol = CFSTR(defaultVal);                         \
    else                                                                \
        compat_keys.symbol = *handle;                                   \
}while(0)

static pthread_once_t once_ctrl = PTHREAD_ONCE_INIT;

static void loadVTEncSymbols(){
    compat_keys.CMVideoFormatDescriptionGetHEVCParameterSetAtIndex =
        (getParameterSetAtIndex)dlsym(
            RTLD_DEFAULT,
            "CMVideoFormatDescriptionGetHEVCParameterSetAtIndex"
        );

    GET_SYM(kCVImageBufferColorPrimaries_ITU_R_2020,   "ITU_R_2020");
    GET_SYM(kCVImageBufferTransferFunction_ITU_R_2020, "ITU_R_2020");
    GET_SYM(kCVImageBufferYCbCrMatrix_ITU_R_2020,      "ITU_R_2020");

    GET_SYM(kVTCompressionPropertyKey_H264EntropyMode, "H264EntropyMode");
    GET_SYM(kVTH264EntropyMode_CAVLC, "CAVLC");
    GET_SYM(kVTH264EntropyMode_CABAC, "CABAC");

    GET_SYM(kVTProfileLevel_H264_Baseline_4_0,       "H264_Baseline_4_0");
    GET_SYM(kVTProfileLevel_H264_Baseline_4_2,       "H264_Baseline_4_2");
    GET_SYM(kVTProfileLevel_H264_Baseline_5_0,       "H264_Baseline_5_0");
    GET_SYM(kVTProfileLevel_H264_Baseline_5_1,       "H264_Baseline_5_1");
    GET_SYM(kVTProfileLevel_H264_Baseline_5_2,       "H264_Baseline_5_2");
    GET_SYM(kVTProfileLevel_H264_Baseline_AutoLevel, "H264_Baseline_AutoLevel");
    GET_SYM(kVTProfileLevel_H264_Main_4_2,           "H264_Main_4_2");
    GET_SYM(kVTProfileLevel_H264_Main_5_1,           "H264_Main_5_1");
    GET_SYM(kVTProfileLevel_H264_Main_5_2,           "H264_Main_5_2");
    GET_SYM(kVTProfileLevel_H264_Main_AutoLevel,     "H264_Main_AutoLevel");
    GET_SYM(kVTProfileLevel_H264_High_3_0,           "H264_High_3_0");
    GET_SYM(kVTProfileLevel_H264_High_3_1,           "H264_High_3_1");
    GET_SYM(kVTProfileLevel_H264_High_3_2,           "H264_High_3_2");
    GET_SYM(kVTProfileLevel_H264_High_4_0,           "H264_High_4_0");
    GET_SYM(kVTProfileLevel_H264_High_4_1,           "H264_High_4_1");
    GET_SYM(kVTProfileLevel_H264_High_4_2,           "H264_High_4_2");
    GET_SYM(kVTProfileLevel_H264_High_5_1,           "H264_High_5_1");
    GET_SYM(kVTProfileLevel_H264_High_5_2,           "H264_High_5_2");
    GET_SYM(kVTProfileLevel_H264_High_AutoLevel,     "H264_High_AutoLevel");
    GET_SYM(kVTProfileLevel_H264_Extended_5_0,       "H264_Extended_5_0");
    GET_SYM(kVTProfileLevel_H264_Extended_AutoLevel, "H264_Extended_AutoLevel");

    GET_SYM(kVTProfileLevel_HEVC_Main_AutoLevel,     "HEVC_Main_AutoLevel");
    GET_SYM(kVTProfileLevel_HEVC_Main10_AutoLevel,   "HEVC_Main10_AutoLevel");

    GET_SYM(kVTCompressionPropertyKey_RealTime, "RealTime");

    GET_SYM(kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
            "EnableHardwareAcceleratedVideoEncoder");
    GET_SYM(kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder,
            "RequireHardwareAcceleratedVideoEncoder");
}

typedef enum VT_H264Profile {
    H264_PROF_AUTO,
    H264_PROF_BASELINE,
    H264_PROF_MAIN,
    H264_PROF_HIGH,
    H264_PROF_EXTENDED,
    H264_PROF_COUNT
} VT_H264Profile;

typedef enum VTH264Entropy{
    VT_ENTROPY_NOT_SET,
    VT_CAVLC,
    VT_CABAC
} VTH264Entropy;

typedef enum VT_HEVCProfile {
    HEVC_PROF_AUTO,
    HEVC_PROF_MAIN,
    HEVC_PROF_MAIN10,
    HEVC_PROF_COUNT
} VT_HEVCProfile;

static const uint8_t start_code[] = { 0, 0, 0, 1 };

typedef struct ExtraSEI {
  void *data;
  size_t size;
} ExtraSEI;

typedef struct BufNode {
    CMSampleBufferRef cm_buffer;
    ExtraSEI *sei;
    struct BufNode* next;
    int error;
} BufNode;

typedef struct VTEncContext {
    AVClass *class;
    enum AVCodecID codec_id;
    VTCompressionSessionRef session;
    CFStringRef ycbcr_matrix;
    CFStringRef color_primaries;
    CFStringRef transfer_function;
    getParameterSetAtIndex get_param_set_func;

    pthread_mutex_t lock;
    pthread_cond_t  cv_sample_sent;

    int async_error;

    BufNode *q_head;
    BufNode *q_tail;

    int64_t frame_ct_out;
    int64_t frame_ct_in;

    int64_t first_pts;
    int64_t dts_delta;

    int64_t profile;
    int64_t level;
    int64_t entropy;
    int64_t realtime;
    int64_t frames_before;
    int64_t frames_after;

    int64_t allow_sw;
    int64_t require_sw;

    bool flushing;
    bool has_b_frames;
    bool warned_color_range;
    bool a53_cc;
} VTEncContext;

static int vtenc_populate_extradata(AVCodecContext   *avctx,
                                    CMVideoCodecType codec_type,
                                    CFStringRef      profile_level,
                                    CFNumberRef      gamma_level,
                                    CFDictionaryRef  enc_info,
                                    CFDictionaryRef  pixel_buffer_info);

/**
 * NULL-safe release of *refPtr, and sets value to NULL.
 */
static void vt_release_num(CFNumberRef* refPtr){
    if (!*refPtr) {
        return;
    }

    CFRelease(*refPtr);
    *refPtr = NULL;
}

static void set_async_error(VTEncContext *vtctx, int err)
{
    BufNode *info;

    pthread_mutex_lock(&vtctx->lock);

    vtctx->async_error = err;

    info = vtctx->q_head;
    vtctx->q_head = vtctx->q_tail = NULL;

    while (info) {
        BufNode *next = info->next;
        CFRelease(info->cm_buffer);
        av_free(info);
        info = next;
    }

    pthread_mutex_unlock(&vtctx->lock);
}

static void clear_frame_queue(VTEncContext *vtctx)
{
    set_async_error(vtctx, 0);
}

static int vtenc_q_pop(VTEncContext *vtctx, bool wait, CMSampleBufferRef *buf, ExtraSEI **sei)
{
    BufNode *info;

    pthread_mutex_lock(&vtctx->lock);

    if (vtctx->async_error) {
        pthread_mutex_unlock(&vtctx->lock);
        return vtctx->async_error;
    }

    if (vtctx->flushing && vtctx->frame_ct_in == vtctx->frame_ct_out) {
        *buf = NULL;

        pthread_mutex_unlock(&vtctx->lock);
        return 0;
    }

    while (!vtctx->q_head && !vtctx->async_error && wait) {
        pthread_cond_wait(&vtctx->cv_sample_sent, &vtctx->lock);
    }

    if (!vtctx->q_head) {
        pthread_mutex_unlock(&vtctx->lock);
        *buf = NULL;
        return 0;
    }

    info = vtctx->q_head;
    vtctx->q_head = vtctx->q_head->next;
    if (!vtctx->q_head) {
        vtctx->q_tail = NULL;
    }

    pthread_mutex_unlock(&vtctx->lock);

    *buf = info->cm_buffer;
    if (sei && *buf) {
        *sei = info->sei;
    } else if (info->sei) {
        if (info->sei->data) av_free(info->sei->data);
        av_free(info->sei);
    }
    av_free(info);

    vtctx->frame_ct_out++;

    return 0;
}

static void vtenc_q_push(VTEncContext *vtctx, CMSampleBufferRef buffer, ExtraSEI *sei)
{
    BufNode *info = av_malloc(sizeof(BufNode));
    if (!info) {
        set_async_error(vtctx, AVERROR(ENOMEM));
        return;
    }

    CFRetain(buffer);
    info->cm_buffer = buffer;
    info->sei = sei;
    info->next = NULL;

    pthread_mutex_lock(&vtctx->lock);
    pthread_cond_signal(&vtctx->cv_sample_sent);

    if (!vtctx->q_head) {
        vtctx->q_head = info;
    } else {
        vtctx->q_tail->next = info;
    }

    vtctx->q_tail = info;

    pthread_mutex_unlock(&vtctx->lock);
}

static int count_nalus(size_t length_code_size,
                       CMSampleBufferRef sample_buffer,
                       int *count)
{
    size_t offset = 0;
    int status;
    int nalu_ct = 0;
    uint8_t size_buf[4];
    size_t src_size = CMSampleBufferGetTotalSampleSize(sample_buffer);
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample_buffer);

    if (length_code_size > 4)
        return AVERROR_INVALIDDATA;

    while (offset < src_size) {
        size_t curr_src_len;
        size_t box_len = 0;
        size_t i;

        status = CMBlockBufferCopyDataBytes(block,
                                            offset,
                                            length_code_size,
                                            size_buf);

        for (i = 0; i < length_code_size; i++) {
            box_len <<= 8;
            box_len |= size_buf[i];
        }

        curr_src_len = box_len + length_code_size;
        offset += curr_src_len;

        nalu_ct++;
    }

    *count = nalu_ct;
    return 0;
}

static CMVideoCodecType get_cm_codec_type(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264: return kCMVideoCodecType_H264;
    case AV_CODEC_ID_HEVC: return kCMVideoCodecType_HEVC;
    default:               return 0;
    }
}

/**
 * Get the parameter sets from a CMSampleBufferRef.
 * @param dst If *dst isn't NULL, the parameters are copied into existing
 *            memory. *dst_size must be set accordingly when *dst != NULL.
 *            If *dst is NULL, it will be allocated.
 *            In all cases, *dst_size is set to the number of bytes used starting
 *            at *dst.
 */
static int get_params_size(
    AVCodecContext              *avctx,
    CMVideoFormatDescriptionRef vid_fmt,
    size_t                      *size)
{
    VTEncContext *vtctx = avctx->priv_data;
    size_t total_size = 0;
    size_t ps_count;
    int is_count_bad = 0;
    size_t i;
    int status;
    status = vtctx->get_param_set_func(vid_fmt,
                                       0,
                                       NULL,
                                       NULL,
                                       &ps_count,
                                       NULL);
    if (status) {
        is_count_bad = 1;
        ps_count     = 0;
        status       = 0;
    }

    for (i = 0; i < ps_count || is_count_bad; i++) {
        const uint8_t *ps;
        size_t ps_size;
        status = vtctx->get_param_set_func(vid_fmt,
                                           i,
                                           &ps,
                                           &ps_size,
                                           NULL,
                                           NULL);
        if (status) {
            /*
             * When ps_count is invalid, status != 0 ends the loop normally
             * unless we didn't get any parameter sets.
             */
            if (i > 0 && is_count_bad) status = 0;

            break;
        }

        total_size += ps_size + sizeof(start_code);
    }

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error getting parameter set sizes: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    *size = total_size;
    return 0;
}

static int copy_param_sets(
    AVCodecContext              *avctx,
    CMVideoFormatDescriptionRef vid_fmt,
    uint8_t                     *dst,
    size_t                      dst_size)
{
    VTEncContext *vtctx = avctx->priv_data;
    size_t ps_count;
    int is_count_bad = 0;
    int status;
    size_t offset = 0;
    size_t i;

    status = vtctx->get_param_set_func(vid_fmt,
                                       0,
                                       NULL,
                                       NULL,
                                       &ps_count,
                                       NULL);
    if (status) {
        is_count_bad = 1;
        ps_count     = 0;
        status       = 0;
    }


    for (i = 0; i < ps_count || is_count_bad; i++) {
        const uint8_t *ps;
        size_t ps_size;
        size_t next_offset;

        status = vtctx->get_param_set_func(vid_fmt,
                                           i,
                                           &ps,
                                           &ps_size,
                                           NULL,
                                           NULL);
        if (status) {
            if (i > 0 && is_count_bad) status = 0;

            break;
        }

        next_offset = offset + sizeof(start_code) + ps_size;
        if (dst_size < next_offset) {
            av_log(avctx, AV_LOG_ERROR, "Error: buffer too small for parameter sets.\n");
            return AVERROR_BUFFER_TOO_SMALL;
        }

        memcpy(dst + offset, start_code, sizeof(start_code));
        offset += sizeof(start_code);

        memcpy(dst + offset, ps, ps_size);
        offset = next_offset;
    }

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error getting parameter set data: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int set_extradata(AVCodecContext *avctx, CMSampleBufferRef sample_buffer)
{
    CMVideoFormatDescriptionRef vid_fmt;
    size_t total_size;
    int status;

    vid_fmt = CMSampleBufferGetFormatDescription(sample_buffer);
    if (!vid_fmt) {
        av_log(avctx, AV_LOG_ERROR, "No video format.\n");
        return AVERROR_EXTERNAL;
    }

    status = get_params_size(avctx, vid_fmt, &total_size);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Could not get parameter sets.\n");
        return status;
    }

    avctx->extradata = av_mallocz(total_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        return AVERROR(ENOMEM);
    }
    avctx->extradata_size = total_size;

    status = copy_param_sets(avctx, vid_fmt, avctx->extradata, total_size);

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Could not copy param sets.\n");
        return status;
    }

    return 0;
}

static void vtenc_output_callback(
    void *ctx,
    void *sourceFrameCtx,
    OSStatus status,
    VTEncodeInfoFlags flags,
    CMSampleBufferRef sample_buffer)
{
    AVCodecContext *avctx = ctx;
    VTEncContext   *vtctx = avctx->priv_data;
    ExtraSEI *sei = sourceFrameCtx;

    if (vtctx->async_error) {
        if(sample_buffer) CFRelease(sample_buffer);
        return;
    }

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error encoding frame: %d\n", (int)status);
        set_async_error(vtctx, AVERROR_EXTERNAL);
        return;
    }

    if (!sample_buffer) {
        return;
    }

    if (!avctx->extradata && (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
        int set_status = set_extradata(avctx, sample_buffer);
        if (set_status) {
            set_async_error(vtctx, set_status);
            return;
        }
    }

    vtenc_q_push(vtctx, sample_buffer, sei);
}

static int get_length_code_size(
    AVCodecContext    *avctx,
    CMSampleBufferRef sample_buffer,
    size_t            *size)
{
    VTEncContext *vtctx = avctx->priv_data;
    CMVideoFormatDescriptionRef vid_fmt;
    int isize;
    int status;

    vid_fmt = CMSampleBufferGetFormatDescription(sample_buffer);
    if (!vid_fmt) {
        av_log(avctx, AV_LOG_ERROR, "Error getting buffer format description.\n");
        return AVERROR_EXTERNAL;
    }

    status = vtctx->get_param_set_func(vid_fmt,
                                       0,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &isize);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error getting length code size: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    *size = isize;
    return 0;
}

/*
 * Returns true on success.
 *
 * If profile_level_val is NULL and this method returns true, don't specify the
 * profile/level to the encoder.
 */
static bool get_vt_h264_profile_level(AVCodecContext *avctx,
                                      CFStringRef    *profile_level_val)
{
    VTEncContext *vtctx = avctx->priv_data;
    int64_t profile = vtctx->profile;

    if (profile == H264_PROF_AUTO && vtctx->level) {
        //Need to pick a profile if level is not auto-selected.
        profile = vtctx->has_b_frames ? H264_PROF_MAIN : H264_PROF_BASELINE;
    }

    *profile_level_val = NULL;

    switch (profile) {
        case H264_PROF_AUTO:
            return true;

        case H264_PROF_BASELINE:
            switch (vtctx->level) {
                case  0: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Baseline_AutoLevel; break;
                case 13: *profile_level_val = kVTProfileLevel_H264_Baseline_1_3;       break;
                case 30: *profile_level_val = kVTProfileLevel_H264_Baseline_3_0;       break;
                case 31: *profile_level_val = kVTProfileLevel_H264_Baseline_3_1;       break;
                case 32: *profile_level_val = kVTProfileLevel_H264_Baseline_3_2;       break;
                case 40: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Baseline_4_0;       break;
                case 41: *profile_level_val = kVTProfileLevel_H264_Baseline_4_1;       break;
                case 42: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Baseline_4_2;       break;
                case 50: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Baseline_5_0;       break;
                case 51: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Baseline_5_1;       break;
                case 52: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Baseline_5_2;       break;
            }
            break;

        case H264_PROF_MAIN:
            switch (vtctx->level) {
                case  0: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Main_AutoLevel; break;
                case 30: *profile_level_val = kVTProfileLevel_H264_Main_3_0;       break;
                case 31: *profile_level_val = kVTProfileLevel_H264_Main_3_1;       break;
                case 32: *profile_level_val = kVTProfileLevel_H264_Main_3_2;       break;
                case 40: *profile_level_val = kVTProfileLevel_H264_Main_4_0;       break;
                case 41: *profile_level_val = kVTProfileLevel_H264_Main_4_1;       break;
                case 42: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Main_4_2;       break;
                case 50: *profile_level_val = kVTProfileLevel_H264_Main_5_0;       break;
                case 51: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Main_5_1;       break;
                case 52: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Main_5_2;       break;
            }
            break;

        case H264_PROF_HIGH:
            switch (vtctx->level) {
                case  0: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_AutoLevel; break;
                case 30: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_3_0;       break;
                case 31: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_3_1;       break;
                case 32: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_3_2;       break;
                case 40: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_4_0;       break;
                case 41: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_4_1;       break;
                case 42: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_4_2;       break;
                case 50: *profile_level_val = kVTProfileLevel_H264_High_5_0;       break;
                case 51: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_5_1;       break;
                case 52: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_High_5_2;       break;
            }
            break;
        case H264_PROF_EXTENDED:
            switch (vtctx->level) {
                case  0: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Extended_AutoLevel; break;
                case 50: *profile_level_val =
                                  compat_keys.kVTProfileLevel_H264_Extended_5_0;       break;
            }
            break;
    }

    if (!*profile_level_val) {
        av_log(avctx, AV_LOG_ERROR, "Invalid Profile/Level.\n");
        return false;
    }

    return true;
}

/*
 * Returns true on success.
 *
 * If profile_level_val is NULL and this method returns true, don't specify the
 * profile/level to the encoder.
 */
static bool get_vt_hevc_profile_level(AVCodecContext *avctx,
                                      CFStringRef    *profile_level_val)
{
    VTEncContext *vtctx = avctx->priv_data;
    int64_t profile = vtctx->profile;

    *profile_level_val = NULL;

    switch (profile) {
        case HEVC_PROF_AUTO:
            return true;
        case HEVC_PROF_MAIN:
            *profile_level_val =
                compat_keys.kVTProfileLevel_HEVC_Main_AutoLevel;
            break;
        case HEVC_PROF_MAIN10:
            *profile_level_val =
                compat_keys.kVTProfileLevel_HEVC_Main10_AutoLevel;
            break;
    }

    if (!*profile_level_val) {
        av_log(avctx, AV_LOG_ERROR, "Invalid Profile/Level.\n");
        return false;
    }

    return true;
}

static int get_cv_pixel_format(AVCodecContext* avctx,
                               enum AVPixelFormat fmt,
                               enum AVColorRange range,
                               int* av_pixel_format,
                               int* range_guessed)
{
    if (range_guessed) *range_guessed = range != AVCOL_RANGE_MPEG &&
                                        range != AVCOL_RANGE_JPEG;

    //MPEG range is used when no range is set
    if (fmt == AV_PIX_FMT_NV12) {
        *av_pixel_format = range == AVCOL_RANGE_JPEG ?
                                        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange :
                                        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    } else if (fmt == AV_PIX_FMT_YUV420P) {
        *av_pixel_format = range == AVCOL_RANGE_JPEG ?
                                        kCVPixelFormatType_420YpCbCr8PlanarFullRange :
                                        kCVPixelFormatType_420YpCbCr8Planar;
    } else if (fmt == AV_PIX_FMT_P010LE) {
        *av_pixel_format = range == AVCOL_RANGE_JPEG ?
                                        kCVPixelFormatType_420YpCbCr10BiPlanarFullRange :
                                        kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
        *av_pixel_format = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    } else {
        return AVERROR(EINVAL);
    }

    return 0;
}

static void add_color_attr(AVCodecContext *avctx, CFMutableDictionaryRef dict) {
    VTEncContext *vtctx = avctx->priv_data;

    if (vtctx->color_primaries) {
        CFDictionarySetValue(dict,
                             kCVImageBufferColorPrimariesKey,
                             vtctx->color_primaries);
    }

    if (vtctx->transfer_function) {
        CFDictionarySetValue(dict,
                             kCVImageBufferTransferFunctionKey,
                             vtctx->transfer_function);
    }

    if (vtctx->ycbcr_matrix) {
        CFDictionarySetValue(dict,
                             kCVImageBufferYCbCrMatrixKey,
                             vtctx->ycbcr_matrix);
    }
}

static int create_cv_pixel_buffer_info(AVCodecContext* avctx,
                                       CFMutableDictionaryRef* dict)
{
    CFNumberRef cv_color_format_num = NULL;
    CFNumberRef width_num = NULL;
    CFNumberRef height_num = NULL;
    CFMutableDictionaryRef pixel_buffer_info = NULL;
    int cv_color_format;
    int status = get_cv_pixel_format(avctx,
                                     avctx->pix_fmt,
                                     avctx->color_range,
                                     &cv_color_format,
                                     NULL);
    if (status) return status;

    pixel_buffer_info = CFDictionaryCreateMutable(
                            kCFAllocatorDefault,
                            20,
                            &kCFCopyStringDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);

    if (!pixel_buffer_info) goto pbinfo_nomem;

    cv_color_format_num = CFNumberCreate(kCFAllocatorDefault,
                                         kCFNumberSInt32Type,
                                         &cv_color_format);
    if (!cv_color_format_num) goto pbinfo_nomem;

    CFDictionarySetValue(pixel_buffer_info,
                         kCVPixelBufferPixelFormatTypeKey,
                         cv_color_format_num);
    vt_release_num(&cv_color_format_num);

    width_num = CFNumberCreate(kCFAllocatorDefault,
                               kCFNumberSInt32Type,
                               &avctx->width);
    if (!width_num) return AVERROR(ENOMEM);

    CFDictionarySetValue(pixel_buffer_info,
                         kCVPixelBufferWidthKey,
                         width_num);
    vt_release_num(&width_num);

    height_num = CFNumberCreate(kCFAllocatorDefault,
                                kCFNumberSInt32Type,
                                &avctx->height);
    if (!height_num) goto pbinfo_nomem;

    CFDictionarySetValue(pixel_buffer_info,
                         kCVPixelBufferHeightKey,
                         height_num);
    vt_release_num(&height_num);

    add_color_attr(avctx, pixel_buffer_info);

    *dict = pixel_buffer_info;
    return 0;

pbinfo_nomem:
    vt_release_num(&cv_color_format_num);
    vt_release_num(&width_num);
    vt_release_num(&height_num);
    if (pixel_buffer_info) CFRelease(pixel_buffer_info);

    return AVERROR(ENOMEM);
}

static int get_cv_color_primaries(AVCodecContext *avctx,
                                  CFStringRef *primaries)
{
    enum AVColorPrimaries pri = avctx->color_primaries;
    switch (pri) {
        case AVCOL_PRI_UNSPECIFIED:
            *primaries = NULL;
            break;

        case AVCOL_PRI_BT709:
            *primaries = kCVImageBufferColorPrimaries_ITU_R_709_2;
            break;

        case AVCOL_PRI_BT2020:
            *primaries = compat_keys.kCVImageBufferColorPrimaries_ITU_R_2020;
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Color primaries %s is not supported.\n", av_color_primaries_name(pri));
            *primaries = NULL;
            return -1;
    }

    return 0;
}

static int get_cv_transfer_function(AVCodecContext *avctx,
                                    CFStringRef *transfer_fnc,
                                    CFNumberRef *gamma_level)
{
    enum AVColorTransferCharacteristic trc = avctx->color_trc;
    Float32 gamma;
    *gamma_level = NULL;

    switch (trc) {
        case AVCOL_TRC_UNSPECIFIED:
            *transfer_fnc = NULL;
            break;

        case AVCOL_TRC_BT709:
            *transfer_fnc = kCVImageBufferTransferFunction_ITU_R_709_2;
            break;

        case AVCOL_TRC_SMPTE240M:
            *transfer_fnc = kCVImageBufferTransferFunction_SMPTE_240M_1995;
            break;

#if HAVE_KCVIMAGEBUFFERTRANSFERFUNCTION_SMPTE_ST_2084_PQ
        case AVCOL_TRC_SMPTE2084:
            *transfer_fnc = kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ;
            break;
#endif
#if HAVE_KCVIMAGEBUFFERTRANSFERFUNCTION_LINEAR
        case AVCOL_TRC_LINEAR:
            *transfer_fnc = kCVImageBufferTransferFunction_Linear;
            break;
#endif
#if HAVE_KCVIMAGEBUFFERTRANSFERFUNCTION_ITU_R_2100_HLG
        case AVCOL_TRC_ARIB_STD_B67:
            *transfer_fnc = kCVImageBufferTransferFunction_ITU_R_2100_HLG;
            break;
#endif

        case AVCOL_TRC_GAMMA22:
            gamma = 2.2;
            *transfer_fnc = kCVImageBufferTransferFunction_UseGamma;
            *gamma_level = CFNumberCreate(NULL, kCFNumberFloat32Type, &gamma);
            break;

        case AVCOL_TRC_GAMMA28:
            gamma = 2.8;
            *transfer_fnc = kCVImageBufferTransferFunction_UseGamma;
            *gamma_level = CFNumberCreate(NULL, kCFNumberFloat32Type, &gamma);
            break;

        case AVCOL_TRC_BT2020_10:
        case AVCOL_TRC_BT2020_12:
            *transfer_fnc = compat_keys.kCVImageBufferTransferFunction_ITU_R_2020;
            break;

        default:
            *transfer_fnc = NULL;
            av_log(avctx, AV_LOG_ERROR, "Transfer function %s is not supported.\n", av_color_transfer_name(trc));
            return -1;
    }

    return 0;
}

static int get_cv_ycbcr_matrix(AVCodecContext *avctx, CFStringRef *matrix) {
    switch(avctx->colorspace) {
        case AVCOL_SPC_BT709:
            *matrix = kCVImageBufferYCbCrMatrix_ITU_R_709_2;
            break;

        case AVCOL_SPC_UNSPECIFIED:
            *matrix = NULL;
            break;

        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
            *matrix = kCVImageBufferYCbCrMatrix_ITU_R_601_4;
            break;

        case AVCOL_SPC_SMPTE240M:
            *matrix = kCVImageBufferYCbCrMatrix_SMPTE_240M_1995;
            break;

        case AVCOL_SPC_BT2020_NCL:
            *matrix = compat_keys.kCVImageBufferYCbCrMatrix_ITU_R_2020;
            break;

        default:
            av_log(avctx, AV_LOG_ERROR, "Color space %s is not supported.\n", av_color_space_name(avctx->colorspace));
            return -1;
    }

    return 0;
}

static int vtenc_create_encoder(AVCodecContext   *avctx,
                                CMVideoCodecType codec_type,
                                CFStringRef      profile_level,
                                CFNumberRef      gamma_level,
                                CFDictionaryRef  enc_info,
                                CFDictionaryRef  pixel_buffer_info,
                                VTCompressionSessionRef *session)
{
    VTEncContext *vtctx = avctx->priv_data;
    SInt32       bit_rate = avctx->bit_rate;
    SInt32       max_rate = avctx->rc_max_rate;
    CFNumberRef  bit_rate_num;
    CFNumberRef  bytes_per_second;
    CFNumberRef  one_second;
    CFArrayRef   data_rate_limits;
    int64_t      bytes_per_second_value = 0;
    int64_t      one_second_value = 0;
    void         *nums[2];

    int status = VTCompressionSessionCreate(kCFAllocatorDefault,
                                            avctx->width,
                                            avctx->height,
                                            codec_type,
                                            enc_info,
                                            pixel_buffer_info,
                                            kCFAllocatorDefault,
                                            vtenc_output_callback,
                                            avctx,
                                            session);

    if (status || !vtctx->session) {
        av_log(avctx, AV_LOG_ERROR, "Error: cannot create compression session: %d\n", status);

#if !TARGET_OS_IPHONE
        if (!vtctx->allow_sw) {
            av_log(avctx, AV_LOG_ERROR, "Try -allow_sw 1. The hardware encoder may be busy, or not supported.\n");
        }
#endif

        return AVERROR_EXTERNAL;
    }

    bit_rate_num = CFNumberCreate(kCFAllocatorDefault,
                                  kCFNumberSInt32Type,
                                  &bit_rate);
    if (!bit_rate_num) return AVERROR(ENOMEM);

    status = VTSessionSetProperty(vtctx->session,
                                  kVTCompressionPropertyKey_AverageBitRate,
                                  bit_rate_num);
    CFRelease(bit_rate_num);

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error setting bitrate property: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    if (vtctx->codec_id == AV_CODEC_ID_H264 && max_rate > 0) {
        // kVTCompressionPropertyKey_DataRateLimits is not available for HEVC
        bytes_per_second_value = max_rate >> 3;
        bytes_per_second = CFNumberCreate(kCFAllocatorDefault,
                                          kCFNumberSInt64Type,
                                          &bytes_per_second_value);
        if (!bytes_per_second) {
            return AVERROR(ENOMEM);
        }
        one_second_value = 1;
        one_second = CFNumberCreate(kCFAllocatorDefault,
                                    kCFNumberSInt64Type,
                                    &one_second_value);
        if (!one_second) {
            CFRelease(bytes_per_second);
            return AVERROR(ENOMEM);
        }
        nums[0] = (void *)bytes_per_second;
        nums[1] = (void *)one_second;
        data_rate_limits = CFArrayCreate(kCFAllocatorDefault,
                                         (const void **)nums,
                                         2,
                                         &kCFTypeArrayCallBacks);

        if (!data_rate_limits) {
            CFRelease(bytes_per_second);
            CFRelease(one_second);
            return AVERROR(ENOMEM);
        }
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_DataRateLimits,
                                      data_rate_limits);

        CFRelease(bytes_per_second);
        CFRelease(one_second);
        CFRelease(data_rate_limits);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting max bitrate property: %d\n", status);
            return AVERROR_EXTERNAL;
        }
    }

    if (vtctx->codec_id == AV_CODEC_ID_H264) {
        // kVTCompressionPropertyKey_ProfileLevel is not available for HEVC
        if (profile_level) {
            status = VTSessionSetProperty(vtctx->session,
                                        kVTCompressionPropertyKey_ProfileLevel,
                                        profile_level);
            if (status) {
                av_log(avctx, AV_LOG_ERROR, "Error setting profile/level property: %d. Output will be encoded using a supported profile/level combination.\n", status);
            }
        }
    }

    if (avctx->gop_size > 0) {
        CFNumberRef interval = CFNumberCreate(kCFAllocatorDefault,
                                              kCFNumberIntType,
                                              &avctx->gop_size);
        if (!interval) {
            return AVERROR(ENOMEM);
        }

        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_MaxKeyFrameInterval,
                                      interval);
        CFRelease(interval);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting 'max key-frame interval' property: %d\n", status);
            return AVERROR_EXTERNAL;
        }
    }

    if (vtctx->frames_before) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_MoreFramesBeforeStart,
                                      kCFBooleanTrue);

        if (status == kVTPropertyNotSupportedErr) {
            av_log(avctx, AV_LOG_WARNING, "frames_before property is not supported on this device. Ignoring.\n");
        } else if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting frames_before property: %d\n", status);
        }
    }

    if (vtctx->frames_after) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_MoreFramesAfterEnd,
                                      kCFBooleanTrue);

        if (status == kVTPropertyNotSupportedErr) {
            av_log(avctx, AV_LOG_WARNING, "frames_after property is not supported on this device. Ignoring.\n");
        } else if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting frames_after property: %d\n", status);
        }
    }

    if (avctx->sample_aspect_ratio.num != 0) {
        CFNumberRef num;
        CFNumberRef den;
        CFMutableDictionaryRef par;
        AVRational *avpar = &avctx->sample_aspect_ratio;

        av_reduce(&avpar->num, &avpar->den,
                   avpar->num,  avpar->den,
                  0xFFFFFFFF);

        num = CFNumberCreate(kCFAllocatorDefault,
                             kCFNumberIntType,
                             &avpar->num);

        den = CFNumberCreate(kCFAllocatorDefault,
                             kCFNumberIntType,
                             &avpar->den);



        par = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                        2,
                                        &kCFCopyStringDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);

        if (!par || !num || !den) {
            if (par) CFRelease(par);
            if (num) CFRelease(num);
            if (den) CFRelease(den);

            return AVERROR(ENOMEM);
        }

        CFDictionarySetValue(
            par,
            kCMFormatDescriptionKey_PixelAspectRatioHorizontalSpacing,
            num);

        CFDictionarySetValue(
            par,
            kCMFormatDescriptionKey_PixelAspectRatioVerticalSpacing,
            den);

        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_PixelAspectRatio,
                                      par);

        CFRelease(par);
        CFRelease(num);
        CFRelease(den);

        if (status) {
            av_log(avctx,
                   AV_LOG_ERROR,
                   "Error setting pixel aspect ratio to %d:%d: %d.\n",
                   avctx->sample_aspect_ratio.num,
                   avctx->sample_aspect_ratio.den,
                   status);

            return AVERROR_EXTERNAL;
        }
    }


    if (vtctx->transfer_function) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_TransferFunction,
                                      vtctx->transfer_function);

        if (status) {
            av_log(avctx, AV_LOG_WARNING, "Could not set transfer function: %d\n", status);
        }
    }


    if (vtctx->ycbcr_matrix) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_YCbCrMatrix,
                                      vtctx->ycbcr_matrix);

        if (status) {
            av_log(avctx, AV_LOG_WARNING, "Could not set ycbcr matrix: %d\n", status);
        }
    }


    if (vtctx->color_primaries) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_ColorPrimaries,
                                      vtctx->color_primaries);

        if (status) {
            av_log(avctx, AV_LOG_WARNING, "Could not set color primaries: %d\n", status);
        }
    }

    if (gamma_level) {
        status = VTSessionSetProperty(vtctx->session,
                                      kCVImageBufferGammaLevelKey,
                                      gamma_level);

        if (status) {
            av_log(avctx, AV_LOG_WARNING, "Could not set gamma level: %d\n", status);
        }
    }

    if (!vtctx->has_b_frames) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_AllowFrameReordering,
                                      kCFBooleanFalse);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting 'allow frame reordering' property: %d\n", status);
            return AVERROR_EXTERNAL;
        }
    }

    if (vtctx->entropy != VT_ENTROPY_NOT_SET) {
        CFStringRef entropy = vtctx->entropy == VT_CABAC ?
                                compat_keys.kVTH264EntropyMode_CABAC:
                                compat_keys.kVTH264EntropyMode_CAVLC;

        status = VTSessionSetProperty(vtctx->session,
                                      compat_keys.kVTCompressionPropertyKey_H264EntropyMode,
                                      entropy);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting entropy property: %d\n", status);
        }
    }

    if (vtctx->realtime) {
        status = VTSessionSetProperty(vtctx->session,
                                      compat_keys.kVTCompressionPropertyKey_RealTime,
                                      kCFBooleanTrue);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting realtime property: %d\n", status);
        }
    }

    status = VTCompressionSessionPrepareToEncodeFrames(vtctx->session);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error: cannot prepare encoder: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int vtenc_configure_encoder(AVCodecContext *avctx)
{
    CFMutableDictionaryRef enc_info;
    CFMutableDictionaryRef pixel_buffer_info;
    CMVideoCodecType       codec_type;
    VTEncContext           *vtctx = avctx->priv_data;
    CFStringRef            profile_level;
    CFNumberRef            gamma_level = NULL;
    int                    status;

    codec_type = get_cm_codec_type(avctx->codec_id);
    if (!codec_type) {
        av_log(avctx, AV_LOG_ERROR, "Error: no mapping for AVCodecID %d\n", avctx->codec_id);
        return AVERROR(EINVAL);
    }

    vtctx->codec_id = avctx->codec_id;

    if (vtctx->codec_id == AV_CODEC_ID_H264) {
        vtctx->get_param_set_func = CMVideoFormatDescriptionGetH264ParameterSetAtIndex;

        vtctx->has_b_frames = avctx->max_b_frames > 0;
        if(vtctx->has_b_frames && vtctx->profile == H264_PROF_BASELINE){
            av_log(avctx, AV_LOG_WARNING, "Cannot use B-frames with baseline profile. Output will not contain B-frames.\n");
            vtctx->has_b_frames = false;
        }

        if (vtctx->entropy == VT_CABAC && vtctx->profile == H264_PROF_BASELINE) {
            av_log(avctx, AV_LOG_WARNING, "CABAC entropy requires 'main' or 'high' profile, but baseline was requested. Encode will not use CABAC entropy.\n");
            vtctx->entropy = VT_ENTROPY_NOT_SET;
        }

        if (!get_vt_h264_profile_level(avctx, &profile_level)) return AVERROR(EINVAL);
    } else {
        vtctx->get_param_set_func = compat_keys.CMVideoFormatDescriptionGetHEVCParameterSetAtIndex;
        if (!vtctx->get_param_set_func) return AVERROR(EINVAL);
        if (!get_vt_hevc_profile_level(avctx, &profile_level)) return AVERROR(EINVAL);
    }

    enc_info = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        20,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    if (!enc_info) return AVERROR(ENOMEM);

#if !TARGET_OS_IPHONE
    if(vtctx->require_sw) {
        CFDictionarySetValue(enc_info,
                             compat_keys.kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
                             kCFBooleanFalse);
    } else if (!vtctx->allow_sw) {
        CFDictionarySetValue(enc_info,
                             compat_keys.kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder,
                             kCFBooleanTrue);
    } else {
        CFDictionarySetValue(enc_info,
                             compat_keys.kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
                             kCFBooleanTrue);
    }
#endif

    if (avctx->pix_fmt != AV_PIX_FMT_VIDEOTOOLBOX) {
        status = create_cv_pixel_buffer_info(avctx, &pixel_buffer_info);
        if (status)
            goto init_cleanup;
    } else {
        pixel_buffer_info = NULL;
    }

    vtctx->dts_delta = vtctx->has_b_frames ? -1 : 0;

    get_cv_transfer_function(avctx, &vtctx->transfer_function, &gamma_level);
    get_cv_ycbcr_matrix(avctx, &vtctx->ycbcr_matrix);
    get_cv_color_primaries(avctx, &vtctx->color_primaries);


    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        status = vtenc_populate_extradata(avctx,
                                          codec_type,
                                          profile_level,
                                          gamma_level,
                                          enc_info,
                                          pixel_buffer_info);
        if (status)
            goto init_cleanup;
    }

    status = vtenc_create_encoder(avctx,
                                  codec_type,
                                  profile_level,
                                  gamma_level,
                                  enc_info,
                                  pixel_buffer_info,
                                  &vtctx->session);

init_cleanup:
    if (gamma_level)
        CFRelease(gamma_level);

    if (pixel_buffer_info)
        CFRelease(pixel_buffer_info);

    CFRelease(enc_info);

    return status;
}

static av_cold int vtenc_init(AVCodecContext *avctx)
{
    VTEncContext    *vtctx = avctx->priv_data;
    CFBooleanRef    has_b_frames_cfbool;
    int             status;

    pthread_once(&once_ctrl, loadVTEncSymbols);

    pthread_mutex_init(&vtctx->lock, NULL);
    pthread_cond_init(&vtctx->cv_sample_sent, NULL);

    vtctx->session = NULL;
    status = vtenc_configure_encoder(avctx);
    if (status) return status;

    status = VTSessionCopyProperty(vtctx->session,
                                   kVTCompressionPropertyKey_AllowFrameReordering,
                                   kCFAllocatorDefault,
                                   &has_b_frames_cfbool);

    if (!status && has_b_frames_cfbool) {
        //Some devices don't output B-frames for main profile, even if requested.
        vtctx->has_b_frames = CFBooleanGetValue(has_b_frames_cfbool);
        CFRelease(has_b_frames_cfbool);
    }
    avctx->has_b_frames = vtctx->has_b_frames;

    return 0;
}

static void vtenc_get_frame_info(CMSampleBufferRef buffer, bool *is_key_frame)
{
    CFArrayRef      attachments;
    CFDictionaryRef attachment;
    CFBooleanRef    not_sync;
    CFIndex         len;

    attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, false);
    len = !attachments ? 0 : CFArrayGetCount(attachments);

    if (!len) {
        *is_key_frame = true;
        return;
    }

    attachment = CFArrayGetValueAtIndex(attachments, 0);

    if (CFDictionaryGetValueIfPresent(attachment,
                                      kCMSampleAttachmentKey_NotSync,
                                      (const void **)&not_sync))
    {
        *is_key_frame = !CFBooleanGetValue(not_sync);
    } else {
        *is_key_frame = true;
    }
}

static int is_post_sei_nal_type(int nal_type){
    return nal_type != H264_NAL_SEI &&
           nal_type != H264_NAL_SPS &&
           nal_type != H264_NAL_PPS &&
           nal_type != H264_NAL_AUD;
}

/*
 * Finds the sei message start/size of type find_sei_type.
 * If more than one of that type exists, the last one is returned.
 */
static int find_sei_end(AVCodecContext *avctx,
                        uint8_t        *nal_data,
                        size_t          nal_size,
                        uint8_t       **sei_end)
{
    int nal_type;
    size_t sei_payload_size = 0;
    int sei_payload_type = 0;
    *sei_end = NULL;
    uint8_t *nal_start = nal_data;

    if (!nal_size)
        return 0;

    nal_type = *nal_data & 0x1F;
    if (nal_type != H264_NAL_SEI)
        return 0;

    nal_data++;
    nal_size--;

    if (nal_data[nal_size - 1] == 0x80)
        nal_size--;

    while (nal_size > 0 && *nal_data > 0) {
        do{
            sei_payload_type += *nal_data;
            nal_data++;
            nal_size--;
        } while (nal_size > 0 && *nal_data == 0xFF);

        if (!nal_size) {
            av_log(avctx, AV_LOG_ERROR, "Unexpected end of SEI NAL Unit parsing type.\n");
            return AVERROR_INVALIDDATA;
        }

        do{
            sei_payload_size += *nal_data;
            nal_data++;
            nal_size--;
        } while (nal_size > 0 && *nal_data == 0xFF);

        if (nal_size < sei_payload_size) {
            av_log(avctx, AV_LOG_ERROR, "Unexpected end of SEI NAL Unit parsing size.\n");
            return AVERROR_INVALIDDATA;
        }

        nal_data += sei_payload_size;
        nal_size -= sei_payload_size;
    }

    *sei_end = nal_data;

    return nal_data - nal_start + 1;
}

/**
 * Copies the data inserting emulation prevention bytes as needed.
 * Existing data in the destination can be taken into account by providing
 * dst with a dst_offset > 0.
 *
 * @return The number of bytes copied on success. On failure, the negative of
 *         the number of bytes needed to copy src is returned.
 */
static int copy_emulation_prev(const uint8_t *src,
                               size_t         src_size,
                               uint8_t       *dst,
                               ssize_t        dst_offset,
                               size_t         dst_size)
{
    int zeros = 0;
    int wrote_bytes;
    uint8_t* dst_start;
    uint8_t* dst_end = dst + dst_size;
    const uint8_t* src_end = src + src_size;
    int start_at = dst_offset > 2 ? dst_offset - 2 : 0;
    int i;
    for (i = start_at; i < dst_offset && i < dst_size; i++) {
        if (!dst[i])
            zeros++;
        else
            zeros = 0;
    }

    dst += dst_offset;
    dst_start = dst;
    for (; src < src_end; src++, dst++) {
        if (zeros == 2) {
            int insert_ep3_byte = *src <= 3;
            if (insert_ep3_byte) {
                if (dst < dst_end)
                    *dst = 3;
                dst++;
            }

            zeros = 0;
        }

        if (dst < dst_end)
            *dst = *src;

        if (!*src)
            zeros++;
        else
            zeros = 0;
    }

    wrote_bytes = dst - dst_start;

    if (dst > dst_end)
        return -wrote_bytes;

    return wrote_bytes;
}

static int write_sei(const ExtraSEI *sei,
                     int             sei_type,
                     uint8_t        *dst,
                     size_t          dst_size)
{
    uint8_t *sei_start = dst;
    size_t remaining_sei_size = sei->size;
    size_t remaining_dst_size = dst_size;
    int header_bytes;
    int bytes_written;
    ssize_t offset;

    if (!remaining_dst_size)
        return AVERROR_BUFFER_TOO_SMALL;

    while (sei_type && remaining_dst_size != 0) {
        int sei_byte = sei_type > 255 ? 255 : sei_type;
        *dst = sei_byte;

        sei_type -= sei_byte;
        dst++;
        remaining_dst_size--;
    }

    if (!dst_size)
        return AVERROR_BUFFER_TOO_SMALL;

    while (remaining_sei_size && remaining_dst_size != 0) {
        int size_byte = remaining_sei_size > 255 ? 255 : remaining_sei_size;
        *dst = size_byte;

        remaining_sei_size -= size_byte;
        dst++;
        remaining_dst_size--;
    }

    if (remaining_dst_size < sei->size)
        return AVERROR_BUFFER_TOO_SMALL;

    header_bytes = dst - sei_start;

    offset = header_bytes;
    bytes_written = copy_emulation_prev(sei->data,
                                        sei->size,
                                        sei_start,
                                        offset,
                                        dst_size);
    if (bytes_written < 0)
        return AVERROR_BUFFER_TOO_SMALL;

    bytes_written += header_bytes;
    return bytes_written;
}

/**
 * Copies NAL units and replaces length codes with
 * H.264 Annex B start codes. On failure, the contents of
 * dst_data may have been modified.
 *
 * @param length_code_size Byte length of each length code
 * @param sample_buffer NAL units prefixed with length codes.
 * @param sei Optional A53 closed captions SEI data.
 * @param dst_data Must be zeroed before calling this function.
 *                 Contains the copied NAL units prefixed with
 *                 start codes when the function returns
 *                 successfully.
 * @param dst_size Length of dst_data
 * @return 0 on success
 *         AVERROR_INVALIDDATA if length_code_size is invalid
 *         AVERROR_BUFFER_TOO_SMALL if dst_data is too small
 *         or if a length_code in src_data specifies data beyond
 *         the end of its buffer.
 */
static int copy_replace_length_codes(
    AVCodecContext *avctx,
    size_t        length_code_size,
    CMSampleBufferRef sample_buffer,
    ExtraSEI      *sei,
    uint8_t       *dst_data,
    size_t        dst_size)
{
    size_t src_size = CMSampleBufferGetTotalSampleSize(sample_buffer);
    size_t remaining_src_size = src_size;
    size_t remaining_dst_size = dst_size;
    size_t src_offset = 0;
    int wrote_sei = 0;
    int status;
    uint8_t size_buf[4];
    uint8_t nal_type;
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample_buffer);

    if (length_code_size > 4) {
        return AVERROR_INVALIDDATA;
    }

    while (remaining_src_size > 0) {
        size_t curr_src_len;
        size_t curr_dst_len;
        size_t box_len = 0;
        size_t i;

        uint8_t       *dst_box;

        status = CMBlockBufferCopyDataBytes(block,
                                            src_offset,
                                            length_code_size,
                                            size_buf);
        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Cannot copy length: %d\n", status);
            return AVERROR_EXTERNAL;
        }

        status = CMBlockBufferCopyDataBytes(block,
                                            src_offset + length_code_size,
                                            1,
                                            &nal_type);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Cannot copy type: %d\n", status);
            return AVERROR_EXTERNAL;
        }

        nal_type &= 0x1F;

        for (i = 0; i < length_code_size; i++) {
            box_len <<= 8;
            box_len |= size_buf[i];
        }

        if (sei && !wrote_sei && is_post_sei_nal_type(nal_type)) {
            //No SEI NAL unit - insert.
            int wrote_bytes;

            memcpy(dst_data, start_code, sizeof(start_code));
            dst_data += sizeof(start_code);
            remaining_dst_size -= sizeof(start_code);

            *dst_data = H264_NAL_SEI;
            dst_data++;
            remaining_dst_size--;

            wrote_bytes = write_sei(sei,
                                    H264_SEI_TYPE_USER_DATA_REGISTERED,
                                    dst_data,
                                    remaining_dst_size);

            if (wrote_bytes < 0)
                return wrote_bytes;

            remaining_dst_size -= wrote_bytes;
            dst_data += wrote_bytes;

            if (remaining_dst_size <= 0)
                return AVERROR_BUFFER_TOO_SMALL;

            *dst_data = 0x80;

            dst_data++;
            remaining_dst_size--;

            wrote_sei = 1;
        }

        curr_src_len = box_len + length_code_size;
        curr_dst_len = box_len + sizeof(start_code);

        if (remaining_src_size < curr_src_len) {
            return AVERROR_BUFFER_TOO_SMALL;
        }

        if (remaining_dst_size < curr_dst_len) {
            return AVERROR_BUFFER_TOO_SMALL;
        }

        dst_box = dst_data + sizeof(start_code);

        memcpy(dst_data, start_code, sizeof(start_code));
        status = CMBlockBufferCopyDataBytes(block,
                                            src_offset + length_code_size,
                                            box_len,
                                            dst_box);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Cannot copy data: %d\n", status);
            return AVERROR_EXTERNAL;
        }

        if (sei && !wrote_sei && nal_type == H264_NAL_SEI) {
            //Found SEI NAL unit - append.
            int wrote_bytes;
            int old_sei_length;
            int extra_bytes;
            uint8_t *new_sei;
            old_sei_length = find_sei_end(avctx, dst_box, box_len, &new_sei);
            if (old_sei_length < 0)
                return status;

            wrote_bytes = write_sei(sei,
                                    H264_SEI_TYPE_USER_DATA_REGISTERED,
                                    new_sei,
                                    remaining_dst_size - old_sei_length);
            if (wrote_bytes < 0)
                return wrote_bytes;

            if (new_sei + wrote_bytes >= dst_data + remaining_dst_size)
                return AVERROR_BUFFER_TOO_SMALL;

            new_sei[wrote_bytes++] = 0x80;
            extra_bytes = wrote_bytes - (dst_box + box_len - new_sei);

            dst_data += extra_bytes;
            remaining_dst_size -= extra_bytes;

            wrote_sei = 1;
        }

        src_offset += curr_src_len;
        dst_data += curr_dst_len;

        remaining_src_size -= curr_src_len;
        remaining_dst_size -= curr_dst_len;
    }

    return 0;
}

/**
 * Returns a sufficient number of bytes to contain the sei data.
 * It may be greater than the minimum required.
 */
static int get_sei_msg_bytes(const ExtraSEI* sei, int type){
    int copied_size;
    if (sei->size == 0)
        return 0;

    copied_size = -copy_emulation_prev(sei->data,
                                       sei->size,
                                       NULL,
                                       0,
                                       0);

    if ((sei->size % 255) == 0) //may result in an extra byte
        copied_size++;

    return copied_size + sei->size / 255 + 1 + type / 255 + 1;
}

static int vtenc_cm_to_avpacket(
    AVCodecContext    *avctx,
    CMSampleBufferRef sample_buffer,
    AVPacket          *pkt,
    ExtraSEI          *sei)
{
    VTEncContext *vtctx = avctx->priv_data;

    int     status;
    bool    is_key_frame;
    bool    add_header;
    size_t  length_code_size;
    size_t  header_size = 0;
    size_t  in_buf_size;
    size_t  out_buf_size;
    size_t  sei_nalu_size = 0;
    int64_t dts_delta;
    int64_t time_base_num;
    int nalu_count;
    CMTime  pts;
    CMTime  dts;
    CMVideoFormatDescriptionRef vid_fmt;


    vtenc_get_frame_info(sample_buffer, &is_key_frame);
    status = get_length_code_size(avctx, sample_buffer, &length_code_size);
    if (status) return status;

    add_header = is_key_frame && !(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER);

    if (add_header) {
        vid_fmt = CMSampleBufferGetFormatDescription(sample_buffer);
        if (!vid_fmt) {
            av_log(avctx, AV_LOG_ERROR, "Cannot get format description.\n");
            return AVERROR_EXTERNAL;
        }

        int status = get_params_size(avctx, vid_fmt, &header_size);
        if (status) return status;
    }

    status = count_nalus(length_code_size, sample_buffer, &nalu_count);
    if(status)
        return status;

    if (sei) {
        size_t msg_size = get_sei_msg_bytes(sei,
                                            H264_SEI_TYPE_USER_DATA_REGISTERED);

        sei_nalu_size = sizeof(start_code) + 1 + msg_size + 1;
    }

    in_buf_size = CMSampleBufferGetTotalSampleSize(sample_buffer);
    out_buf_size = header_size +
                   in_buf_size +
                   sei_nalu_size +
                   nalu_count * ((int)sizeof(start_code) - (int)length_code_size);

    status = ff_alloc_packet2(avctx, pkt, out_buf_size, out_buf_size);
    if (status < 0)
        return status;

    if (add_header) {
        status = copy_param_sets(avctx, vid_fmt, pkt->data, out_buf_size);
        if(status) return status;
    }

    status = copy_replace_length_codes(
        avctx,
        length_code_size,
        sample_buffer,
        sei,
        pkt->data + header_size,
        pkt->size - header_size
    );

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error copying packet data: %d\n", status);
        return status;
    }

    if (is_key_frame) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    pts = CMSampleBufferGetPresentationTimeStamp(sample_buffer);
    dts = CMSampleBufferGetDecodeTimeStamp      (sample_buffer);

    if (CMTIME_IS_INVALID(dts)) {
        if (!vtctx->has_b_frames) {
            dts = pts;
        } else {
            av_log(avctx, AV_LOG_ERROR, "DTS is invalid.\n");
            return AVERROR_EXTERNAL;
        }
    }

    dts_delta = vtctx->dts_delta >= 0 ? vtctx->dts_delta : 0;
    time_base_num = avctx->time_base.num;
    pkt->pts = pts.value / time_base_num;
    pkt->dts = dts.value / time_base_num - dts_delta;
    pkt->size = out_buf_size;

    return 0;
}

/*
 * contiguous_buf_size is 0 if not contiguous, and the size of the buffer
 * containing all planes if so.
 */
static int get_cv_pixel_info(
    AVCodecContext *avctx,
    const AVFrame  *frame,
    int            *color,
    int            *plane_count,
    size_t         *widths,
    size_t         *heights,
    size_t         *strides,
    size_t         *contiguous_buf_size)
{
    VTEncContext *vtctx = avctx->priv_data;
    int av_format       = frame->format;
    int av_color_range  = frame->color_range;
    int i;
    int range_guessed;
    int status;

    status = get_cv_pixel_format(avctx, av_format, av_color_range, color, &range_guessed);
    if (status) {
        av_log(avctx,
            AV_LOG_ERROR,
            "Could not get pixel format for color format '%s' range '%s'.\n",
            av_get_pix_fmt_name(av_format),
            av_color_range > AVCOL_RANGE_UNSPECIFIED &&
            av_color_range < AVCOL_RANGE_NB ?
               av_color_range_name(av_color_range) :
               "Unknown");

        return AVERROR(EINVAL);
    }

    if (range_guessed) {
        if (!vtctx->warned_color_range) {
            vtctx->warned_color_range = true;
            av_log(avctx,
                   AV_LOG_WARNING,
                   "Color range not set for %s. Using MPEG range.\n",
                   av_get_pix_fmt_name(av_format));
        }
    }

    switch (av_format) {
    case AV_PIX_FMT_NV12:
        *plane_count = 2;

        widths [0] = avctx->width;
        heights[0] = avctx->height;
        strides[0] = frame ? frame->linesize[0] : avctx->width;

        widths [1] = (avctx->width  + 1) / 2;
        heights[1] = (avctx->height + 1) / 2;
        strides[1] = frame ? frame->linesize[1] : (avctx->width + 1) & -2;
        break;

    case AV_PIX_FMT_YUV420P:
        *plane_count = 3;

        widths [0] = avctx->width;
        heights[0] = avctx->height;
        strides[0] = frame ? frame->linesize[0] : avctx->width;

        widths [1] = (avctx->width  + 1) / 2;
        heights[1] = (avctx->height + 1) / 2;
        strides[1] = frame ? frame->linesize[1] : (avctx->width + 1) / 2;

        widths [2] = (avctx->width  + 1) / 2;
        heights[2] = (avctx->height + 1) / 2;
        strides[2] = frame ? frame->linesize[2] : (avctx->width + 1) / 2;
        break;

    case AV_PIX_FMT_P010LE:
        *plane_count = 2;
        widths[0] = avctx->width;
        heights[0] = avctx->height;
        strides[0] = frame ? frame->linesize[0] : (avctx->width * 2 + 63) & -64;

        widths[1] = (avctx->width + 1) / 2;
        heights[1] = (avctx->height + 1) / 2;
        strides[1] = frame ? frame->linesize[1] : ((avctx->width + 1) / 2 + 63) & -64;
        break;

    default:
        av_log(
               avctx,
               AV_LOG_ERROR,
               "Could not get frame format info for color %d range %d.\n",
               av_format,
               av_color_range);

        return AVERROR(EINVAL);
    }

    *contiguous_buf_size = 0;
    for (i = 0; i < *plane_count; i++) {
        if (i < *plane_count - 1 &&
            frame->data[i] + strides[i] * heights[i] != frame->data[i + 1]) {
            *contiguous_buf_size = 0;
            break;
        }

        *contiguous_buf_size += strides[i] * heights[i];
    }

    return 0;
}

//Not used on OSX - frame is never copied.
static int copy_avframe_to_pixel_buffer(AVCodecContext   *avctx,
                                        const AVFrame    *frame,
                                        CVPixelBufferRef cv_img,
                                        const size_t     *plane_strides,
                                        const size_t     *plane_rows)
{
    int i, j;
    size_t plane_count;
    int status;
    int rows;
    int src_stride;
    int dst_stride;
    uint8_t *src_addr;
    uint8_t *dst_addr;
    size_t copy_bytes;

    status = CVPixelBufferLockBaseAddress(cv_img, 0);
    if (status) {
        av_log(
            avctx,
            AV_LOG_ERROR,
            "Error: Could not lock base address of CVPixelBuffer: %d.\n",
            status
        );
    }

    if (CVPixelBufferIsPlanar(cv_img)) {
        plane_count = CVPixelBufferGetPlaneCount(cv_img);
        for (i = 0; frame->data[i]; i++) {
            if (i == plane_count) {
                CVPixelBufferUnlockBaseAddress(cv_img, 0);
                av_log(avctx,
                    AV_LOG_ERROR,
                    "Error: different number of planes in AVFrame and CVPixelBuffer.\n"
                );

                return AVERROR_EXTERNAL;
            }

            dst_addr = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(cv_img, i);
            src_addr = (uint8_t*)frame->data[i];
            dst_stride = CVPixelBufferGetBytesPerRowOfPlane(cv_img, i);
            src_stride = plane_strides[i];
            rows = plane_rows[i];

            if (dst_stride == src_stride) {
                memcpy(dst_addr, src_addr, src_stride * rows);
            } else {
                copy_bytes = dst_stride < src_stride ? dst_stride : src_stride;

                for (j = 0; j < rows; j++) {
                    memcpy(dst_addr + j * dst_stride, src_addr + j * src_stride, copy_bytes);
                }
            }
        }
    } else {
        if (frame->data[1]) {
            CVPixelBufferUnlockBaseAddress(cv_img, 0);
            av_log(avctx,
                AV_LOG_ERROR,
                "Error: different number of planes in AVFrame and non-planar CVPixelBuffer.\n"
            );

            return AVERROR_EXTERNAL;
        }

        dst_addr = (uint8_t*)CVPixelBufferGetBaseAddress(cv_img);
        src_addr = (uint8_t*)frame->data[0];
        dst_stride = CVPixelBufferGetBytesPerRow(cv_img);
        src_stride = plane_strides[0];
        rows = plane_rows[0];

        if (dst_stride == src_stride) {
            memcpy(dst_addr, src_addr, src_stride * rows);
        } else {
            copy_bytes = dst_stride < src_stride ? dst_stride : src_stride;

            for (j = 0; j < rows; j++) {
                memcpy(dst_addr + j * dst_stride, src_addr + j * src_stride, copy_bytes);
            }
        }
    }

    status = CVPixelBufferUnlockBaseAddress(cv_img, 0);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error: Could not unlock CVPixelBuffer base address: %d.\n", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int create_cv_pixel_buffer(AVCodecContext   *avctx,
                                  const AVFrame    *frame,
                                  CVPixelBufferRef *cv_img)
{
    int plane_count;
    int color;
    size_t widths [AV_NUM_DATA_POINTERS];
    size_t heights[AV_NUM_DATA_POINTERS];
    size_t strides[AV_NUM_DATA_POINTERS];
    int status;
    size_t contiguous_buf_size;
    CVPixelBufferPoolRef pix_buf_pool;
    VTEncContext* vtctx = avctx->priv_data;

    if (avctx->pix_fmt == AV_PIX_FMT_VIDEOTOOLBOX) {
        av_assert0(frame->format == AV_PIX_FMT_VIDEOTOOLBOX);

        *cv_img = (CVPixelBufferRef)frame->data[3];
        av_assert0(*cv_img);

        CFRetain(*cv_img);
        return 0;
    }

    memset(widths,  0, sizeof(widths));
    memset(heights, 0, sizeof(heights));
    memset(strides, 0, sizeof(strides));

    status = get_cv_pixel_info(
        avctx,
        frame,
        &color,
        &plane_count,
        widths,
        heights,
        strides,
        &contiguous_buf_size
    );

    if (status) {
        av_log(
            avctx,
            AV_LOG_ERROR,
            "Error: Cannot convert format %d color_range %d: %d\n",
            frame->format,
            frame->color_range,
            status
        );

        return AVERROR_EXTERNAL;
    }

    pix_buf_pool = VTCompressionSessionGetPixelBufferPool(vtctx->session);
    if (!pix_buf_pool) {
        /* On iOS, the VT session is invalidated when the APP switches from
         * foreground to background and vice versa. Fetch the actual error code
         * of the VT session to detect that case and restart the VT session
         * accordingly. */
        OSStatus vtstatus;

        vtstatus = VTCompressionSessionPrepareToEncodeFrames(vtctx->session);
        if (vtstatus == kVTInvalidSessionErr) {
            CFRelease(vtctx->session);
            vtctx->session = NULL;
            status = vtenc_configure_encoder(avctx);
            if (status == 0)
                pix_buf_pool = VTCompressionSessionGetPixelBufferPool(vtctx->session);
        }
        if (!pix_buf_pool) {
            av_log(avctx, AV_LOG_ERROR, "Could not get pixel buffer pool.\n");
            return AVERROR_EXTERNAL;
        }
        else
            av_log(avctx, AV_LOG_WARNING, "VT session restarted because of a "
                   "kVTInvalidSessionErr error.\n");
    }

    status = CVPixelBufferPoolCreatePixelBuffer(NULL,
                                                pix_buf_pool,
                                                cv_img);


    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Could not create pixel buffer from pool: %d.\n", status);
        return AVERROR_EXTERNAL;
    }

    status = copy_avframe_to_pixel_buffer(avctx, frame, *cv_img, strides, heights);
    if (status) {
        CFRelease(*cv_img);
        *cv_img = NULL;
        return status;
    }

    return 0;
}

static int create_encoder_dict_h264(const AVFrame *frame,
                                    CFDictionaryRef* dict_out)
{
    CFDictionaryRef dict = NULL;
    if (frame->pict_type == AV_PICTURE_TYPE_I) {
        const void *keys[] = { kVTEncodeFrameOptionKey_ForceKeyFrame };
        const void *vals[] = { kCFBooleanTrue };

        dict = CFDictionaryCreate(NULL, keys, vals, 1, NULL, NULL);
        if(!dict) return AVERROR(ENOMEM);
    }

    *dict_out = dict;
    return 0;
}

static int vtenc_send_frame(AVCodecContext *avctx,
                            VTEncContext   *vtctx,
                            const AVFrame  *frame)
{
    CMTime time;
    CFDictionaryRef frame_dict;
    CVPixelBufferRef cv_img = NULL;
    AVFrameSideData *side_data = NULL;
    ExtraSEI *sei = NULL;
    int status = create_cv_pixel_buffer(avctx, frame, &cv_img);

    if (status) return status;

    status = create_encoder_dict_h264(frame, &frame_dict);
    if (status) {
        CFRelease(cv_img);
        return status;
    }

    side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC);
    if (vtctx->a53_cc && side_data && side_data->size) {
        sei = av_mallocz(sizeof(*sei));
        if (!sei) {
            av_log(avctx, AV_LOG_ERROR, "Not enough memory for closed captions, skipping\n");
        } else {
            int ret = ff_alloc_a53_sei(frame, 0, &sei->data, &sei->size);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Not enough memory for closed captions, skipping\n");
                av_free(sei);
                sei = NULL;
            }
        }
    }

    time = CMTimeMake(frame->pts * avctx->time_base.num, avctx->time_base.den);
    status = VTCompressionSessionEncodeFrame(
        vtctx->session,
        cv_img,
        time,
        kCMTimeInvalid,
        frame_dict,
        sei,
        NULL
    );

    if (frame_dict) CFRelease(frame_dict);
    CFRelease(cv_img);

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error: cannot encode frame: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static av_cold int vtenc_frame(
    AVCodecContext *avctx,
    AVPacket       *pkt,
    const AVFrame  *frame,
    int            *got_packet)
{
    VTEncContext *vtctx = avctx->priv_data;
    bool get_frame;
    int status;
    CMSampleBufferRef buf = NULL;
    ExtraSEI *sei = NULL;

    if (frame) {
        status = vtenc_send_frame(avctx, vtctx, frame);

        if (status) {
            status = AVERROR_EXTERNAL;
            goto end_nopkt;
        }

        if (vtctx->frame_ct_in == 0) {
            vtctx->first_pts = frame->pts;
        } else if(vtctx->frame_ct_in == 1 && vtctx->has_b_frames) {
            vtctx->dts_delta = frame->pts - vtctx->first_pts;
        }

        vtctx->frame_ct_in++;
    } else if(!vtctx->flushing) {
        vtctx->flushing = true;

        status = VTCompressionSessionCompleteFrames(vtctx->session,
                                                    kCMTimeIndefinite);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error flushing frames: %d\n", status);
            status = AVERROR_EXTERNAL;
            goto end_nopkt;
        }
    }

    *got_packet = 0;
    get_frame = vtctx->dts_delta >= 0 || !frame;
    if (!get_frame) {
        status = 0;
        goto end_nopkt;
    }

    status = vtenc_q_pop(vtctx, !frame, &buf, &sei);
    if (status) goto end_nopkt;
    if (!buf)   goto end_nopkt;

    status = vtenc_cm_to_avpacket(avctx, buf, pkt, sei);
    if (sei) {
        if (sei->data) av_free(sei->data);
        av_free(sei);
    }
    CFRelease(buf);
    if (status) goto end_nopkt;

    *got_packet = 1;
    return 0;

end_nopkt:
    av_packet_unref(pkt);
    return status;
}

static int vtenc_populate_extradata(AVCodecContext   *avctx,
                                    CMVideoCodecType codec_type,
                                    CFStringRef      profile_level,
                                    CFNumberRef      gamma_level,
                                    CFDictionaryRef  enc_info,
                                    CFDictionaryRef  pixel_buffer_info)
{
    VTEncContext *vtctx = avctx->priv_data;
    int status;
    CVPixelBufferPoolRef pool = NULL;
    CVPixelBufferRef pix_buf = NULL;
    CMTime time;
    CMSampleBufferRef buf = NULL;

    status = vtenc_create_encoder(avctx,
                                  codec_type,
                                  profile_level,
                                  gamma_level,
                                  enc_info,
                                  pixel_buffer_info,
                                  &vtctx->session);
    if (status)
        goto pe_cleanup;

    pool = VTCompressionSessionGetPixelBufferPool(vtctx->session);
    if(!pool){
        av_log(avctx, AV_LOG_ERROR, "Error getting pixel buffer pool.\n");
        goto pe_cleanup;
    }

    status = CVPixelBufferPoolCreatePixelBuffer(NULL,
                                                pool,
                                                &pix_buf);

    if(status != kCVReturnSuccess){
        av_log(avctx, AV_LOG_ERROR, "Error creating frame from pool: %d\n", status);
        goto pe_cleanup;
    }

    time = CMTimeMake(0, avctx->time_base.den);
    status = VTCompressionSessionEncodeFrame(vtctx->session,
                                             pix_buf,
                                             time,
                                             kCMTimeInvalid,
                                             NULL,
                                             NULL,
                                             NULL);

    if (status) {
        av_log(avctx,
               AV_LOG_ERROR,
               "Error sending frame for extradata: %d\n",
               status);

        goto pe_cleanup;
    }

    //Populates extradata - output frames are flushed and param sets are available.
    status = VTCompressionSessionCompleteFrames(vtctx->session,
                                                kCMTimeIndefinite);

    if (status)
        goto pe_cleanup;

    status = vtenc_q_pop(vtctx, 0, &buf, NULL);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "popping: %d\n", status);
        goto pe_cleanup;
    }

    CFRelease(buf);



pe_cleanup:
    if(vtctx->session)
        CFRelease(vtctx->session);

    vtctx->session = NULL;
    vtctx->frame_ct_out = 0;

    av_assert0(status != 0 || (avctx->extradata && avctx->extradata_size > 0));

    return status;
}

static av_cold int vtenc_close(AVCodecContext *avctx)
{
    VTEncContext *vtctx = avctx->priv_data;

    pthread_cond_destroy(&vtctx->cv_sample_sent);
    pthread_mutex_destroy(&vtctx->lock);

    if(!vtctx->session) return 0;

    VTCompressionSessionCompleteFrames(vtctx->session,
                                       kCMTimeIndefinite);
    clear_frame_queue(vtctx);
    CFRelease(vtctx->session);
    vtctx->session = NULL;

    if (vtctx->color_primaries) {
        CFRelease(vtctx->color_primaries);
        vtctx->color_primaries = NULL;
    }

    if (vtctx->transfer_function) {
        CFRelease(vtctx->transfer_function);
        vtctx->transfer_function = NULL;
    }

    if (vtctx->ycbcr_matrix) {
        CFRelease(vtctx->ycbcr_matrix);
        vtctx->ycbcr_matrix = NULL;
    }

    return 0;
}

static const enum AVPixelFormat avc_pix_fmts[] = {
    AV_PIX_FMT_VIDEOTOOLBOX,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat hevc_pix_fmts[] = {
    AV_PIX_FMT_VIDEOTOOLBOX,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_P010LE,
    AV_PIX_FMT_NONE
};

#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
#define COMMON_OPTIONS \
    { "allow_sw", "Allow software encoding", OFFSET(allow_sw), AV_OPT_TYPE_BOOL, \
        { .i64 = 0 }, 0, 1, VE }, \
    { "require_sw", "Require software encoding", OFFSET(require_sw), AV_OPT_TYPE_BOOL, \
        { .i64 = 0 }, 0, 1, VE }, \
    { "realtime", "Hint that encoding should happen in real-time if not faster (e.g. capturing from camera).", \
        OFFSET(realtime), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE }, \
    { "frames_before", "Other frames will come before the frames in this session. This helps smooth concatenation issues.", \
        OFFSET(frames_before), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE }, \
    { "frames_after", "Other frames will come after the frames in this session. This helps smooth concatenation issues.", \
        OFFSET(frames_after), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },

#define OFFSET(x) offsetof(VTEncContext, x)
static const AVOption h264_options[] = {
    { "profile", "Profile", OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = H264_PROF_AUTO }, H264_PROF_AUTO, H264_PROF_COUNT, VE, "profile" },
    { "baseline", "Baseline Profile", 0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_BASELINE }, INT_MIN, INT_MAX, VE, "profile" },
    { "main",     "Main Profile",     0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_MAIN     }, INT_MIN, INT_MAX, VE, "profile" },
    { "high",     "High Profile",     0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_HIGH     }, INT_MIN, INT_MAX, VE, "profile" },
    { "extended", "Extend Profile",   0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_EXTENDED }, INT_MIN, INT_MAX, VE, "profile" },

    { "level", "Level", OFFSET(level), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 52, VE, "level" },
    { "1.3", "Level 1.3, only available with Baseline Profile", 0, AV_OPT_TYPE_CONST, { .i64 = 13 }, INT_MIN, INT_MAX, VE, "level" },
    { "3.0", "Level 3.0", 0, AV_OPT_TYPE_CONST, { .i64 = 30 }, INT_MIN, INT_MAX, VE, "level" },
    { "3.1", "Level 3.1", 0, AV_OPT_TYPE_CONST, { .i64 = 31 }, INT_MIN, INT_MAX, VE, "level" },
    { "3.2", "Level 3.2", 0, AV_OPT_TYPE_CONST, { .i64 = 32 }, INT_MIN, INT_MAX, VE, "level" },
    { "4.0", "Level 4.0", 0, AV_OPT_TYPE_CONST, { .i64 = 40 }, INT_MIN, INT_MAX, VE, "level" },
    { "4.1", "Level 4.1", 0, AV_OPT_TYPE_CONST, { .i64 = 41 }, INT_MIN, INT_MAX, VE, "level" },
    { "4.2", "Level 4.2", 0, AV_OPT_TYPE_CONST, { .i64 = 42 }, INT_MIN, INT_MAX, VE, "level" },
    { "5.0", "Level 5.0", 0, AV_OPT_TYPE_CONST, { .i64 = 50 }, INT_MIN, INT_MAX, VE, "level" },
    { "5.1", "Level 5.1", 0, AV_OPT_TYPE_CONST, { .i64 = 51 }, INT_MIN, INT_MAX, VE, "level" },
    { "5.2", "Level 5.2", 0, AV_OPT_TYPE_CONST, { .i64 = 52 }, INT_MIN, INT_MAX, VE, "level" },

    { "coder", "Entropy coding", OFFSET(entropy), AV_OPT_TYPE_INT, { .i64 = VT_ENTROPY_NOT_SET }, VT_ENTROPY_NOT_SET, VT_CABAC, VE, "coder" },
    { "cavlc", "CAVLC entropy coding", 0, AV_OPT_TYPE_CONST, { .i64 = VT_CAVLC }, INT_MIN, INT_MAX, VE, "coder" },
    { "vlc",   "CAVLC entropy coding", 0, AV_OPT_TYPE_CONST, { .i64 = VT_CAVLC }, INT_MIN, INT_MAX, VE, "coder" },
    { "cabac", "CABAC entropy coding", 0, AV_OPT_TYPE_CONST, { .i64 = VT_CABAC }, INT_MIN, INT_MAX, VE, "coder" },
    { "ac",    "CABAC entropy coding", 0, AV_OPT_TYPE_CONST, { .i64 = VT_CABAC }, INT_MIN, INT_MAX, VE, "coder" },

    { "a53cc", "Use A53 Closed Captions (if available)", OFFSET(a53_cc), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, VE },

    COMMON_OPTIONS
    { NULL },
};

static const AVClass h264_videotoolbox_class = {
    .class_name = "h264_videotoolbox",
    .item_name  = av_default_item_name,
    .option     = h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_videotoolbox_encoder = {
    .name             = "h264_videotoolbox",
    .long_name        = NULL_IF_CONFIG_SMALL("VideoToolbox H.264 Encoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H264,
    .priv_data_size   = sizeof(VTEncContext),
    .pix_fmts         = avc_pix_fmts,
    .init             = vtenc_init,
    .encode2          = vtenc_frame,
    .close            = vtenc_close,
    .capabilities     = AV_CODEC_CAP_DELAY,
    .priv_class       = &h264_videotoolbox_class,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};

static const AVOption hevc_options[] = {
    { "profile", "Profile", OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = HEVC_PROF_AUTO }, HEVC_PROF_AUTO, HEVC_PROF_COUNT, VE, "profile" },
    { "main",     "Main Profile",     0, AV_OPT_TYPE_CONST, { .i64 = HEVC_PROF_MAIN   }, INT_MIN, INT_MAX, VE, "profile" },
    { "main10",   "Main10 Profile",   0, AV_OPT_TYPE_CONST, { .i64 = HEVC_PROF_MAIN10 }, INT_MIN, INT_MAX, VE, "profile" },

    COMMON_OPTIONS
    { NULL },
};

static const AVClass hevc_videotoolbox_class = {
    .class_name = "hevc_videotoolbox",
    .item_name  = av_default_item_name,
    .option     = hevc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_hevc_videotoolbox_encoder = {
    .name             = "hevc_videotoolbox",
    .long_name        = NULL_IF_CONFIG_SMALL("VideoToolbox H.265 Encoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HEVC,
    .priv_data_size   = sizeof(VTEncContext),
    .pix_fmts         = hevc_pix_fmts,
    .init             = vtenc_init,
    .encode2          = vtenc_frame,
    .close            = vtenc_close,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .priv_class       = &hevc_videotoolbox_class,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
    .wrapper_name     = "videotoolbox",
};
