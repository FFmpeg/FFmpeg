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
#include "libavutil/atomic.h"
#include "libavutil/avstring.h"
#include "libavcodec/avcodec.h"
#include "internal.h"
#include <pthread.h>

typedef enum VT_H264Profile {
    H264_PROF_AUTO,
    H264_PROF_BASELINE,
    H264_PROF_MAIN,
    H264_PROF_HIGH,
    H264_PROF_COUNT
} VT_H264Profile;

static const uint8_t start_code[] = { 0, 0, 0, 1 };

typedef struct BufNode {
    CMSampleBufferRef cm_buffer;
    struct BufNode* next;
    int error;
} BufNode;

typedef struct VTEncContext {
    AVClass *class;
    VTCompressionSessionRef session;

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

    bool flushing;
    bool has_b_frames;
    bool warned_color_range;
} VTEncContext;

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

static int vtenc_q_pop(VTEncContext *vtctx, bool wait, CMSampleBufferRef *buf)
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
    av_free(info);

    vtctx->frame_ct_out++;

    return 0;
}

static void vtenc_q_push(VTEncContext *vtctx, CMSampleBufferRef buffer)
{
    BufNode *info = av_malloc(sizeof(BufNode));
    if (!info) {
        set_async_error(vtctx, AVERROR(ENOMEM));
        return;
    }

    CFRetain(buffer);
    info->cm_buffer = buffer;
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

static CMVideoCodecType get_cm_codec_type(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264: return kCMVideoCodecType_H264;
    default:               return 0;
    }
}

static void vtenc_free_block(void *opaque, uint8_t *data)
{
    CMBlockBufferRef block = opaque;
    CFRelease(block);
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
    size_t total_size = 0;
    size_t ps_count;
    int is_count_bad = 0;
    size_t i;
    int status;
    status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(vid_fmt,
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
        status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(vid_fmt,
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
    size_t ps_count;
    int is_count_bad = 0;
    int status;
    size_t offset = 0;
    size_t i;

    status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(vid_fmt,
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

        status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(vid_fmt,
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

    avctx->extradata = av_malloc(total_size);
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

    if (vtctx->async_error) {
        if(sample_buffer) CFRelease(sample_buffer);
        return;
    }

    if (status || !sample_buffer) {
        av_log(avctx, AV_LOG_ERROR, "Error encoding frame: %d\n", (int)status);
        set_async_error(vtctx, AVERROR_EXTERNAL);
        return;
    }

    if (!avctx->extradata && (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
        int set_status = set_extradata(avctx, sample_buffer);
        if (set_status) {
            set_async_error(vtctx, set_status);
            return;
        }
    }

    vtenc_q_push(vtctx, sample_buffer);
}

static int get_length_code_size(
    AVCodecContext    *avctx,
    CMSampleBufferRef sample_buffer,
    size_t            *size)
{
    CMVideoFormatDescriptionRef vid_fmt;
    int isize;
    int status;

    vid_fmt = CMSampleBufferGetFormatDescription(sample_buffer);
    if (!vid_fmt) {
        av_log(avctx, AV_LOG_ERROR, "Error getting buffer format description.\n");
        return AVERROR_EXTERNAL;
    }

    status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(vid_fmt,
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
static bool get_vt_profile_level(AVCodecContext *avctx,
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
                case  0: *profile_level_val = kVTProfileLevel_H264_Baseline_AutoLevel; break;
                case 13: *profile_level_val = kVTProfileLevel_H264_Baseline_1_3;       break;
                case 30: *profile_level_val = kVTProfileLevel_H264_Baseline_3_0;       break;
                case 31: *profile_level_val = kVTProfileLevel_H264_Baseline_3_1;       break;
                case 32: *profile_level_val = kVTProfileLevel_H264_Baseline_3_2;       break;
                case 40: *profile_level_val = kVTProfileLevel_H264_Baseline_4_0;       break;
                case 41: *profile_level_val = kVTProfileLevel_H264_Baseline_4_1;       break;
                case 42: *profile_level_val = kVTProfileLevel_H264_Baseline_4_2;       break;
                case 50: *profile_level_val = kVTProfileLevel_H264_Baseline_5_0;       break;
                case 51: *profile_level_val = kVTProfileLevel_H264_Baseline_5_1;       break;
                case 52: *profile_level_val = kVTProfileLevel_H264_Baseline_5_2;       break;
            }
            break;

        case H264_PROF_MAIN:
            switch (vtctx->level) {
                case  0: *profile_level_val = kVTProfileLevel_H264_Main_AutoLevel; break;
                case 30: *profile_level_val = kVTProfileLevel_H264_Main_3_0;       break;
                case 31: *profile_level_val = kVTProfileLevel_H264_Main_3_1;       break;
                case 32: *profile_level_val = kVTProfileLevel_H264_Main_3_2;       break;
                case 40: *profile_level_val = kVTProfileLevel_H264_Main_4_0;       break;
                case 41: *profile_level_val = kVTProfileLevel_H264_Main_4_1;       break;
                case 42: *profile_level_val = kVTProfileLevel_H264_Main_4_2;       break;
                case 50: *profile_level_val = kVTProfileLevel_H264_Main_5_0;       break;
                case 51: *profile_level_val = kVTProfileLevel_H264_Main_5_1;       break;
                case 52: *profile_level_val = kVTProfileLevel_H264_Main_5_2;       break;
            }
            break;

        case H264_PROF_HIGH:
            switch (vtctx->level) {
                case  0: *profile_level_val = kVTProfileLevel_H264_High_AutoLevel; break;
                case 30: *profile_level_val = kVTProfileLevel_H264_High_3_0;       break;
                case 31: *profile_level_val = kVTProfileLevel_H264_High_3_1;       break;
                case 32: *profile_level_val = kVTProfileLevel_H264_High_3_2;       break;
                case 40: *profile_level_val = kVTProfileLevel_H264_High_4_0;       break;
                case 41: *profile_level_val = kVTProfileLevel_H264_High_4_1;       break;
                case 42: *profile_level_val = kVTProfileLevel_H264_High_4_2;       break;
                case 50: *profile_level_val = kVTProfileLevel_H264_High_5_0;       break;
                case 51: *profile_level_val = kVTProfileLevel_H264_High_5_1;       break;
                case 52: *profile_level_val = kVTProfileLevel_H264_High_5_2;       break;
            }
            break;
    }

    if (!*profile_level_val) {
        av_log(avctx, AV_LOG_ERROR, "Invalid Profile/Level.\n");
        return false;
    }

    return true;
}

static av_cold int vtenc_init(AVCodecContext *avctx)
{
    CFMutableDictionaryRef enc_info;
    CMVideoCodecType       codec_type;
    VTEncContext           *vtctx = avctx->priv_data;
    CFStringRef            profile_level;
    SInt32                 bit_rate = avctx->bit_rate;
    CFNumberRef            bit_rate_num;
    int                    status;

    codec_type = get_cm_codec_type(avctx->codec_id);
    if (!codec_type) {
        av_log(avctx, AV_LOG_ERROR, "Error: no mapping for AVCodecID %d\n", avctx->codec_id);
        return AVERROR(EINVAL);
    }

    vtctx->has_b_frames = avctx->has_b_frames || avctx->max_b_frames > 0;
    if(vtctx->has_b_frames && vtctx->profile == H264_PROF_BASELINE){
        av_log(avctx, AV_LOG_WARNING, "Cannot use B-frames with baseline profile. Output will not contain B-frames.\n");
        vtctx->has_b_frames = false;
    }

    if (!get_vt_profile_level(avctx, &profile_level)) return AVERROR(EINVAL);

    vtctx->session = NULL;

    enc_info = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        20,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    if (!enc_info) return AVERROR(ENOMEM);

#if !TARGET_OS_IPHONE
    CFDictionarySetValue(enc_info, kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder, kCFBooleanTrue);
    CFDictionarySetValue(enc_info, kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,  kCFBooleanTrue);
#endif

    status = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        avctx->width,
        avctx->height,
        codec_type,
        enc_info,
        NULL,
        kCFAllocatorDefault,
        vtenc_output_callback,
        avctx,
        &vtctx->session
    );

#if !TARGET_OS_IPHONE
    if (status != 0 || !vtctx->session) {
        CFDictionaryRemoveValue(enc_info, kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder);

        status = VTCompressionSessionCreate(
            kCFAllocatorDefault,
            avctx->width,
            avctx->height,
            codec_type,
            enc_info,
            NULL,
            kCFAllocatorDefault,
            vtenc_output_callback,
            avctx,
            &vtctx->session
        );
    }
#endif

    CFRelease(enc_info);

    if (status || !vtctx->session) {
        av_log(avctx, AV_LOG_ERROR, "Error: cannot create compression session: %d\n", status);
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

    if (profile_level) {
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_ProfileLevel,
                                      profile_level);
        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting profile/level property: %d\n", status);
            return AVERROR_EXTERNAL;
        }
    }

    if (avctx->gop_size > 0) {
        CFNumberRef interval = CFNumberCreate(kCFAllocatorDefault,
                                              kCFNumberIntType,
                                              &avctx->gop_size);
        status = VTSessionSetProperty(vtctx->session,
                                      kVTCompressionPropertyKey_MaxKeyFrameInterval,
                                      interval);

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error setting 'max key-frame interval' property: %d\n", status);
            return AVERROR_EXTERNAL;
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

    status = VTCompressionSessionPrepareToEncodeFrames(vtctx->session);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error: cannot prepare encoder: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    pthread_mutex_init(&vtctx->lock, NULL);
    pthread_cond_init(&vtctx->cv_sample_sent, NULL);
    vtctx->dts_delta = vtctx->has_b_frames ? -1 : 0;

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

/**
 * Replaces length codes with H.264 Annex B start codes.
 * length_code_size must equal sizeof(start_code).
 * On failure, the contents of data may have been modified.
 *
 * @param length_code_size Byte length of each length code
 * @param data Call with NAL units prefixed with length codes.
 *             On success, the length codes are replace with
 *             start codes.
 * @param size Length of data, excluding any padding.
 * @return 0 on success
 *         AVERROR_BUFFER_TOO_SMALL if length code size is smaller
 *         than a start code or if a length_code in data specifies
 *         data beyond the end of its buffer.
 */
static int replace_length_codes(size_t  length_code_size,
                                uint8_t *data,
                                size_t  size)
{
    size_t remaining_size = size;

    if (length_code_size != sizeof(start_code)) {
        av_log(NULL, AV_LOG_ERROR, "Start code size and length code size not equal.\n");
        return AVERROR_BUFFER_TOO_SMALL;
    }

    while (remaining_size > 0) {
        size_t box_len = 0;
        size_t i;

        for (i = 0; i < length_code_size; i++) {
            box_len <<= 8;
            box_len |= data[i];
        }

        if (remaining_size < box_len + sizeof(start_code)) {
            av_log(NULL, AV_LOG_ERROR, "Length is out of range.\n");
            AVERROR_BUFFER_TOO_SMALL;
        }

        memcpy(data, start_code, sizeof(start_code));
        data += box_len + sizeof(start_code);
        remaining_size -= box_len + sizeof(start_code);
    }

    return 0;
}

/**
 * Copies NAL units and replaces length codes with
 * H.264 Annex B start codes. On failure, the contents of
 * dst_data may have been modified.
 *
 * @param length_code_size Byte length of each length code
 * @param src_data NAL units prefixed with length codes.
 * @param src_size Length of buffer, excluding any padding.
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
    size_t        length_code_size,
    const uint8_t *src_data,
    size_t        src_size,
    uint8_t       *dst_data,
    size_t        dst_size)
{
    size_t remaining_src_size = src_size;
    size_t remaining_dst_size = dst_size;

    if (length_code_size > 4) {
        return AVERROR_INVALIDDATA;
    }

    while (remaining_src_size > 0) {
        size_t curr_src_len;
        size_t curr_dst_len;
        size_t box_len = 0;
        size_t i;

        uint8_t       *dst_box;
        const uint8_t *src_box;

        for (i = 0; i < length_code_size; i++) {
            box_len <<= 8;
            box_len |= src_data[i];
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
        src_box = src_data + length_code_size;

        memcpy(dst_data, start_code, sizeof(start_code));
        memcpy(dst_box,  src_box,    box_len);

        src_data += curr_src_len;
        dst_data += curr_dst_len;

        remaining_src_size -= curr_src_len;
        remaining_dst_size -= curr_dst_len;
    }

    return 0;
}

static int vtenc_cm_to_avpacket(
    AVCodecContext    *avctx,
    CMSampleBufferRef sample_buffer,
    AVPacket          *pkt)
{
    VTEncContext *vtctx = avctx->priv_data;

    int     status;
    bool    is_key_frame;
    bool    add_header;
    char    *buf_data;
    size_t  length_code_size;
    size_t  header_size = 0;
    size_t  in_buf_size;
    int64_t dts_delta;
    int64_t time_base_num;
    CMTime  pts;
    CMTime  dts;

    CMBlockBufferRef            block;
    CMVideoFormatDescriptionRef vid_fmt;


    vtenc_get_frame_info(sample_buffer, &is_key_frame);
    status = get_length_code_size(avctx, sample_buffer, &length_code_size);
    if (status) return status;

    add_header = is_key_frame && !(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER);

    if (add_header) {
        vid_fmt = CMSampleBufferGetFormatDescription(sample_buffer);
        if (!vid_fmt) {
            av_log(avctx, AV_LOG_ERROR, "Cannot get format description.\n");
        }

        int status = get_params_size(avctx, vid_fmt, &header_size);
        if (status) return status;
    }

    block = CMSampleBufferGetDataBuffer(sample_buffer);
    if (!block) {
        av_log(avctx, AV_LOG_ERROR, "Could not get block buffer from sample buffer.\n");
        return AVERROR_EXTERNAL;
    }


    status = CMBlockBufferGetDataPointer(block, 0, &in_buf_size, NULL, &buf_data);
    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error: cannot get data pointer: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    size_t out_buf_size = header_size + in_buf_size;
    bool can_reuse_cmbuffer = !add_header &&
                              !pkt->data  &&
                              length_code_size == sizeof(start_code);

    av_init_packet(pkt);

    if (can_reuse_cmbuffer) {
        AVBufferRef* buf_ref = av_buffer_create(
            buf_data,
            out_buf_size,
            vtenc_free_block,
            block,
            0
        );

        if (!buf_ref) return AVERROR(ENOMEM);

        CFRetain(block);

        pkt->buf  = buf_ref;
        pkt->data = buf_data;
        pkt->size = in_buf_size;

        status = replace_length_codes(length_code_size, pkt->data, pkt->size);
        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error replacing length codes: %d\n", status);
            return status;
        }
    } else {
        if (!pkt->data) {
            status = av_new_packet(pkt, out_buf_size);
            if(status) return status;
        }

        if (pkt->size < out_buf_size) {
            av_log(avctx, AV_LOG_ERROR, "Error: packet's buffer is too small.\n");
            return AVERROR_BUFFER_TOO_SMALL;
        }

        if (add_header) {
            status = copy_param_sets(avctx, vid_fmt, pkt->data, out_buf_size);
            if(status) return status;
        }

        status = copy_replace_length_codes(
            length_code_size,
            buf_data,
            in_buf_size,
            pkt->data + header_size,
            pkt->size - header_size
        );

        if (status) {
            av_log(avctx, AV_LOG_ERROR, "Error copying packet data: %d", status);
            return status;
        }
    }

    if (is_key_frame) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    pts = CMSampleBufferGetPresentationTimeStamp(sample_buffer);
    dts = CMSampleBufferGetDecodeTimeStamp      (sample_buffer);

    dts_delta = vtctx->dts_delta >= 0 ? vtctx->dts_delta : 0;
    time_base_num = avctx->time_base.num;
    pkt->pts = pts.value / time_base_num;
    pkt->dts = dts.value / time_base_num - dts_delta;

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
    int av_color_range  = av_frame_get_color_range(frame);
    int i;

    switch (av_format) {
    case AV_PIX_FMT_NV12:
        switch (av_color_range) {
        case AVCOL_RANGE_MPEG:
            *color = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
            break;

        case AVCOL_RANGE_JPEG:
            *color = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
            break;

        default:
            if (!vtctx->warned_color_range) {
                vtctx->warned_color_range = true;
                av_log(avctx, AV_LOG_WARNING, "Color range not set for NV12. Using MPEG range.\n");
            }
            *color = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        }

        *plane_count = 2;

        widths [0] = avctx->width;
        heights[0] = avctx->height;
        strides[0] = frame ? frame->linesize[0] : avctx->width;

        widths [1] = (avctx->width  + 1) / 2;
        heights[1] = (avctx->height + 1) / 2;
        strides[1] = frame ? frame->linesize[1] : (avctx->width + 1) & -2;
        break;

    case AV_PIX_FMT_YUV420P:
        switch (av_color_range) {
        case AVCOL_RANGE_MPEG:
            *color = kCVPixelFormatType_420YpCbCr8Planar;
            break;

        case AVCOL_RANGE_JPEG:
            *color = kCVPixelFormatType_420YpCbCr8PlanarFullRange;
            break;

        default:
            if (!vtctx->warned_color_range) {
                vtctx->warned_color_range = true;
                av_log(avctx, AV_LOG_WARNING, "Color range not set for YUV 4:2:0. Using MPEG range.\n");
            }
            *color = kCVPixelFormatType_420YpCbCr8Planar;
        }

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

    default: return AVERROR(EINVAL);
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

#if !TARGET_OS_IPHONE
//Not used on iOS - frame is always copied.
static void free_avframe(
    void       *release_ctx,
    const void *data,
    size_t      size,
    size_t      plane_count,
    const void *plane_addresses[])
{
    AVFrame *frame = release_ctx;
    av_frame_free(&frame);
}
#else
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
#endif //!TARGET_OS_IPHONE

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
            av_frame_get_color_range(frame),
            status
        );

        return AVERROR_EXTERNAL;
    }

#if TARGET_OS_IPHONE
    status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        frame->width,
        frame->height,
        color,
        NULL,
        cv_img
    );

    if (status) {
        return AVERROR_EXTERNAL;
    }

    status = copy_avframe_to_pixel_buffer(avctx, frame, *cv_img, strides, heights);
    if (status) {
        CFRelease(*cv_img);
        *cv_img = NULL;
        return status;
    }
#else
    AVFrame *enc_frame = av_frame_alloc();
    if (!enc_frame) return AVERROR(ENOMEM);

    status = av_frame_ref(enc_frame, frame);
    if (status) {
        av_frame_free(&enc_frame);
        return status;
    }

    status = CVPixelBufferCreateWithPlanarBytes(
        kCFAllocatorDefault,
        enc_frame->width,
        enc_frame->height,
        color,
        NULL,
        contiguous_buf_size,
        plane_count,
        (void **)enc_frame->data,
        widths,
        heights,
        strides,
        free_avframe,
        enc_frame,
        NULL,
        cv_img
    );

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Error: Could not create CVPixelBuffer: %d\n", status);
        return AVERROR_EXTERNAL;
    }
#endif

    return 0;
}

static int vtenc_send_frame(AVCodecContext *avctx,
                            VTEncContext   *vtctx,
                            const AVFrame  *frame)
{
    CMTime time;
    CVPixelBufferRef cv_img = NULL;
    int status = create_cv_pixel_buffer(avctx, frame, &cv_img);

    if (status) return status;

    time = CMTimeMake(frame->pts * avctx->time_base.num, avctx->time_base.den);
    status = VTCompressionSessionEncodeFrame(
        vtctx->session,
        cv_img,
        time,
        kCMTimeInvalid,
        NULL,
        NULL,
        NULL
    );

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

    status = vtenc_q_pop(vtctx, !frame, &buf);
    if (status) goto end_nopkt;
    if (!buf)   goto end_nopkt;

    status = vtenc_cm_to_avpacket(avctx, buf, pkt);
    CFRelease(buf);
    if (status) goto end_nopkt;

    *got_packet = 1;
    return 0;

end_nopkt:
    av_packet_unref(pkt);
    return status;
}

static av_cold int vtenc_close(AVCodecContext *avctx)
{
    VTEncContext *vtctx = avctx->priv_data;

    if(!vtctx->session) return 0;

    pthread_cond_destroy(&vtctx->cv_sample_sent);
    pthread_mutex_destroy(&vtctx->lock);
    CFRelease(vtctx->session);
    vtctx->session = NULL;

    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_NV12,
#if !TARGET_OS_IPHONE
    AV_PIX_FMT_YUV420P,
#endif
    AV_PIX_FMT_NONE
};

#define OFFSET(x) offsetof(VTEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "profile", "Profile", OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = H264_PROF_AUTO }, H264_PROF_AUTO, H264_PROF_COUNT, VE, "profile" },
    { "baseline", "Baseline Profile", 0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_BASELINE }, INT_MIN, INT_MAX, VE, "profile" },
    { "main",     "Main Profile",     0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_MAIN     }, INT_MIN, INT_MAX, VE, "profile" },
    { "high",     "High Profile",     0, AV_OPT_TYPE_CONST, { .i64 = H264_PROF_HIGH     }, INT_MIN, INT_MAX, VE, "profile" },

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

    { NULL },
};

static const AVClass h264_videotoolbox_class = {
    .class_name = "h264_videotoolbox",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_videotoolbox_encoder = {
    .name             = "h264_videotoolbox",
    .long_name        = NULL_IF_CONFIG_SMALL("VideoToolbox H.264 Encoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H264,
    .priv_data_size   = sizeof(VTEncContext),
    .pix_fmts         = pix_fmts,
    .init             = vtenc_init,
    .encode2          = vtenc_frame,
    .close            = vtenc_close,
    .capabilities     = AV_CODEC_CAP_DELAY,
    .priv_class       = &h264_videotoolbox_class,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
};
