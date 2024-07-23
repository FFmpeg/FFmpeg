/*
 * Videotoolbox hardware acceleration
 *
 * copyright (c) 2012 Sebastien Zwickert
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "config_components.h"
#include "videotoolbox.h"
#include "libavutil/hwcontext_videotoolbox.h"
#include "libavutil/mem.h"
#include "vt_internal.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
#include "bytestream.h"
#include "decode.h"
#include "internal.h"
#include "h264dec.h"
#include "hevc/hevcdec.h"
#include "hwaccel_internal.h"
#include "mpegvideo.h"
#include "proresdec.h"
#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>

#ifndef kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder
#  define kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder CFSTR("RequireHardwareAcceleratedVideoDecoder")
#endif
#ifndef kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder
#  define kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder CFSTR("EnableHardwareAcceleratedVideoDecoder")
#endif

#if !HAVE_KCMVIDEOCODECTYPE_HEVC
enum { kCMVideoCodecType_HEVC = 'hvc1' };
#endif

#if !HAVE_KCMVIDEOCODECTYPE_VP9
enum { kCMVideoCodecType_VP9 = 'vp09' };
#endif

#define VIDEOTOOLBOX_ESDS_EXTRADATA_PADDING  12

typedef struct VTHWFrame {
    CVPixelBufferRef pixbuf;
    AVBufferRef *hw_frames_ctx;
} VTHWFrame;

static void videotoolbox_buffer_release(void *opaque, uint8_t *data)
{
    VTHWFrame *ref = (VTHWFrame *)data;
    av_buffer_unref(&ref->hw_frames_ctx);
    CVPixelBufferRelease(ref->pixbuf);

    av_free(data);
}

int ff_videotoolbox_buffer_copy(VTContext *vtctx,
                                const uint8_t *buffer,
                                uint32_t size)
{
    void *tmp;

    tmp = av_fast_realloc(vtctx->bitstream,
                         &vtctx->allocated_size,
                         size);

    if (!tmp)
        return AVERROR(ENOMEM);

    vtctx->bitstream = tmp;
    memcpy(vtctx->bitstream, buffer, size);
    vtctx->bitstream_size = size;

    return 0;
}

static int videotoolbox_postproc_frame(void *avctx, AVFrame *frame)
{
    int ret;
    VTHWFrame *ref = (VTHWFrame *)frame->buf[0]->data;

    if (!ref->pixbuf) {
        av_log(avctx, AV_LOG_ERROR, "No frame decoded?\n");
        av_frame_unref(frame);
        return AVERROR_EXTERNAL;
    }

    frame->crop_right = 0;
    frame->crop_left = 0;
    frame->crop_top = 0;
    frame->crop_bottom = 0;

    if ((ret = av_vt_pixbuf_set_attachments(avctx, ref->pixbuf, frame)) < 0)
        return ret;

    frame->data[3] = (uint8_t*)ref->pixbuf;

    if (ref->hw_frames_ctx) {
        av_buffer_unref(&frame->hw_frames_ctx);
        frame->hw_frames_ctx = av_buffer_ref(ref->hw_frames_ctx);
        if (!frame->hw_frames_ctx)
            return AVERROR(ENOMEM);
    }

    return 0;
}

int ff_videotoolbox_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
    size_t      size = sizeof(VTHWFrame);
    uint8_t    *data = NULL;
    AVBufferRef *buf = NULL;
    int ret = ff_attach_decode_data(frame);
    FrameDecodeData *fdd;
    if (ret < 0)
        return ret;

    data = av_mallocz(size);
    if (!data)
        return AVERROR(ENOMEM);
    buf = av_buffer_create(data, size, videotoolbox_buffer_release, NULL, 0);
    if (!buf) {
        av_freep(&data);
        return AVERROR(ENOMEM);
    }
    frame->buf[0] = buf;

    fdd = (FrameDecodeData*)frame->private_ref->data;
    fdd->post_process = videotoolbox_postproc_frame;

    frame->width  = avctx->width;
    frame->height = avctx->height;
    frame->format = avctx->pix_fmt;

    return 0;
}

#define AV_W8(p, v) *(p) = (v)

static int escape_ps(uint8_t* dst, const uint8_t* src, int src_size)
{
    int i;
    int size = src_size;
    uint8_t* p = dst;

    for (i = 0; i < src_size; i++) {
        if (i + 2 < src_size &&
            src[i]     == 0x00 &&
            src[i + 1] == 0x00 &&
            src[i + 2] <= 0x03) {
            if (dst) {
                *p++ = src[i++];
                *p++ = src[i];
                *p++ = 0x03;
            } else {
                i++;
            }
            size++;
        } else if (dst)
            *p++ = src[i];
    }

    if (dst)
        av_assert0((p - dst) == size);

    return size;
}

CFDataRef ff_videotoolbox_avcc_extradata_create(AVCodecContext *avctx)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    H264Context *h = avctx->priv_data;
    CFDataRef data = NULL;
    uint8_t *p;
    int sps_size = escape_ps(NULL, h->ps.sps->data, h->ps.sps->data_size);
    int pps_size = escape_ps(NULL, h->ps.pps->data, h->ps.pps->data_size);
    int vt_extradata_size;
    uint8_t *vt_extradata;

    vt_extradata_size = 6 + 2 + sps_size + 3 + pps_size;
    vt_extradata = av_malloc(vt_extradata_size);

    if (!vt_extradata)
        return NULL;

    p = vt_extradata;

    AV_W8(p + 0, 1); /* version */
    AV_W8(p + 1, h->ps.sps->data[1]); /* profile */
    AV_W8(p + 2, h->ps.sps->data[2]); /* profile compat */
    AV_W8(p + 3, h->ps.sps->data[3]); /* level */
    AV_W8(p + 4, 0xff); /* 6 bits reserved (111111) + 2 bits nal size length - 3 (11) */
    AV_W8(p + 5, 0xe1); /* 3 bits reserved (111) + 5 bits number of sps (00001) */
    AV_WB16(p + 6, sps_size);
    p += 8;
    p += escape_ps(p, h->ps.sps->data, h->ps.sps->data_size);
    AV_W8(p + 0, 1); /* number of pps */
    AV_WB16(p + 1, pps_size);
    p += 3;
    p += escape_ps(p, h->ps.pps->data, h->ps.pps->data_size);

    av_assert0(p - vt_extradata == vt_extradata_size);

    // save sps header (profile/level) used to create decoder session,
    // so we can detect changes and recreate it.
    if (vtctx)
        memcpy(vtctx->sps, h->ps.sps->data + 1, 3);

    data = CFDataCreate(kCFAllocatorDefault, vt_extradata, vt_extradata_size);
    av_free(vt_extradata);
    return data;
}

CFDataRef ff_videotoolbox_hvcc_extradata_create(AVCodecContext *avctx)
{
    HEVCContext *h = avctx->priv_data;
    int i, num_vps = 0, num_sps = 0, num_pps = 0;
    const HEVCPPS *pps = h->pps;
    const HEVCSPS *sps = pps->sps;
    const HEVCVPS *vps = sps->vps;
    PTLCommon ptlc = vps->ptl.general_ptl;
    VUI vui = sps->vui;
    uint8_t parallelismType;
    CFDataRef data = NULL;
    uint8_t *p;
    int vt_extradata_size = 23 + 3 + 3 + 3;
    uint8_t *vt_extradata;

#define COUNT_SIZE_PS(T, t) \
    for (i = 0; i < HEVC_MAX_##T##PS_COUNT; i++) { \
        if (h->ps.t##ps_list[i]) { \
            const HEVC##T##PS *lps = h->ps.t##ps_list[i]; \
            vt_extradata_size += 2 + escape_ps(NULL, lps->data, lps->data_size); \
            num_##t##ps++; \
        } \
    }

    COUNT_SIZE_PS(V, v)
    COUNT_SIZE_PS(S, s)
    COUNT_SIZE_PS(P, p)

    vt_extradata = av_malloc(vt_extradata_size);
    if (!vt_extradata)
        return NULL;
    p = vt_extradata;

    /* unsigned int(8) configurationVersion = 1; */
    AV_W8(p + 0, 1);

    /*
     * unsigned int(2) general_profile_space;
     * unsigned int(1) general_tier_flag;
     * unsigned int(5) general_profile_idc;
     */
    AV_W8(p + 1, ptlc.profile_space << 6 |
                 ptlc.tier_flag     << 5 |
                 ptlc.profile_idc);

    /* unsigned int(32) general_profile_compatibility_flags; */
    for (i = 0; i < 4; i++) {
        AV_W8(p + 2 + i, ptlc.profile_compatibility_flag[i * 8] << 7 |
                         ptlc.profile_compatibility_flag[i * 8 + 1] << 6 |
                         ptlc.profile_compatibility_flag[i * 8 + 2] << 5 |
                         ptlc.profile_compatibility_flag[i * 8 + 3] << 4 |
                         ptlc.profile_compatibility_flag[i * 8 + 4] << 3 |
                         ptlc.profile_compatibility_flag[i * 8 + 5] << 2 |
                         ptlc.profile_compatibility_flag[i * 8 + 6] << 1 |
                         ptlc.profile_compatibility_flag[i * 8 + 7]);
    }

    /* unsigned int(48) general_constraint_indicator_flags; */
    AV_W8(p + 6, ptlc.progressive_source_flag    << 7 |
                 ptlc.interlaced_source_flag     << 6 |
                 ptlc.non_packed_constraint_flag << 5 |
                 ptlc.frame_only_constraint_flag << 4);
    AV_W8(p + 7, 0);
    AV_WN32(p + 8, 0);

    /* unsigned int(8) general_level_idc; */
    AV_W8(p + 12, ptlc.level_idc);

    /*
     * bit(4) reserved = ‘1111’b;
     * unsigned int(12) min_spatial_segmentation_idc;
     */
    AV_W8(p + 13, 0xf0 | (vui.min_spatial_segmentation_idc >> 4));
    AV_W8(p + 14, vui.min_spatial_segmentation_idc & 0xff);

    /*
     * bit(6) reserved = ‘111111’b;
     * unsigned int(2) parallelismType;
     */
    if (!vui.min_spatial_segmentation_idc)
        parallelismType = 0;
    else if (pps->entropy_coding_sync_enabled_flag && pps->tiles_enabled_flag)
        parallelismType = 0;
    else if (pps->entropy_coding_sync_enabled_flag)
        parallelismType = 3;
    else if (pps->tiles_enabled_flag)
        parallelismType = 2;
    else
        parallelismType = 1;
    AV_W8(p + 15, 0xfc | parallelismType);

    /*
     * bit(6) reserved = ‘111111’b;
     * unsigned int(2) chromaFormat;
     */
    AV_W8(p + 16, sps->chroma_format_idc | 0xfc);

    /*
     * bit(5) reserved = ‘11111’b;
     * unsigned int(3) bitDepthLumaMinus8;
     */
    AV_W8(p + 17, (sps->bit_depth - 8) | 0xf8);

    /*
     * bit(5) reserved = ‘11111’b;
     * unsigned int(3) bitDepthChromaMinus8;
     */
    AV_W8(p + 18, (sps->bit_depth_chroma - 8) | 0xf8);

    /* bit(16) avgFrameRate; */
    AV_WB16(p + 19, 0);

    /*
     * bit(2) constantFrameRate;
     * bit(3) numTemporalLayers;
     * bit(1) temporalIdNested;
     * unsigned int(2) lengthSizeMinusOne;
     */
    AV_W8(p + 21, 0                             << 6 |
                  sps->max_sub_layers           << 3 |
                  sps->temporal_id_nesting      << 2 |
                  3);

    /* unsigned int(8) numOfArrays; */
    AV_W8(p + 22, 3);

    p += 23;

#define APPEND_PS(T, t) \
    /* \
     * bit(1) array_completeness; \
     * unsigned int(1) reserved = 0; \
     * unsigned int(6) NAL_unit_type; \
     */ \
    AV_W8(p, 1 << 7 | \
             HEVC_NAL_##T##PS & 0x3f); \
    /* unsigned int(16) numNalus; */ \
    AV_WB16(p + 1, num_##t##ps); \
    p += 3; \
    for (i = 0; i < HEVC_MAX_##T##PS_COUNT; i++) { \
        if (h->ps.t##ps_list[i]) { \
            const HEVC##T##PS *lps = h->ps.t##ps_list[i]; \
            int size = escape_ps(p + 2, lps->data, lps->data_size); \
            /* unsigned int(16) nalUnitLength; */ \
            AV_WB16(p, size); \
            /* bit(8*nalUnitLength) nalUnit; */ \
            p += 2 + size; \
        } \
    }

    APPEND_PS(V, v)
    APPEND_PS(S, s)
    APPEND_PS(P, p)

    av_assert0(p - vt_extradata == vt_extradata_size);

    data = CFDataCreate(kCFAllocatorDefault, vt_extradata, vt_extradata_size);
    av_free(vt_extradata);
    return data;
}

int ff_videotoolbox_h264_start_frame(AVCodecContext *avctx,
                                     const uint8_t *buffer,
                                     uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    H264Context *h = avctx->priv_data;

    if (h->is_avc == 1) {
        return ff_videotoolbox_buffer_copy(vtctx, buffer, size);
    }

    return 0;
}

static int videotoolbox_h264_decode_params(AVCodecContext *avctx,
                                           int type,
                                           const uint8_t *buffer,
                                           uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    H264Context *h = avctx->priv_data;

    // save sps header (profile/level) used to create decoder session
    if (!vtctx->sps[0])
        memcpy(vtctx->sps, h->ps.sps->data + 1, 3);

    if (type == H264_NAL_SPS) {
        if (size > 4 && memcmp(vtctx->sps, buffer + 1, 3) != 0) {
            vtctx->reconfig_needed = true;
            memcpy(vtctx->sps, buffer + 1, 3);
        }
    }

    // pass-through SPS/PPS changes to the decoder
    return ff_videotoolbox_h264_decode_slice(avctx, buffer, size);
}

static int videotoolbox_common_decode_slice(AVCodecContext *avctx,
                                            const uint8_t *buffer,
                                            uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    void *tmp;

    tmp = av_fast_realloc(vtctx->bitstream,
                          &vtctx->allocated_size,
                          vtctx->bitstream_size+size+4);
    if (!tmp)
        return AVERROR(ENOMEM);

    vtctx->bitstream = tmp;

    AV_WB32(vtctx->bitstream + vtctx->bitstream_size, size);
    memcpy(vtctx->bitstream + vtctx->bitstream_size + 4, buffer, size);

    vtctx->bitstream_size += size + 4;

    return 0;
}

int ff_videotoolbox_h264_decode_slice(AVCodecContext *avctx,
                                      const uint8_t *buffer,
                                      uint32_t size)
{
    H264Context *h = avctx->priv_data;

    if (h->is_avc == 1)
        return 0;

    return videotoolbox_common_decode_slice(avctx, buffer, size);
}

#if CONFIG_VIDEOTOOLBOX
// Return the AVVideotoolboxContext that matters currently. Where it comes from
// depends on the API used.
static AVVideotoolboxContext *videotoolbox_get_context(AVCodecContext *avctx)
{
    // Somewhat tricky because the user can call av_videotoolbox_default_free()
    // at any time, even when the codec is closed.
    if (avctx->internal && avctx->internal->hwaccel_priv_data) {
        VTContext *vtctx = avctx->internal->hwaccel_priv_data;
        if (vtctx->vt_ctx)
            return vtctx->vt_ctx;
    }
    return avctx->hwaccel_context;
}

static void videotoolbox_stop(AVCodecContext *avctx)
{
    AVVideotoolboxContext *videotoolbox = videotoolbox_get_context(avctx);
    if (!videotoolbox)
        return;

    if (videotoolbox->cm_fmt_desc) {
        CFRelease(videotoolbox->cm_fmt_desc);
        videotoolbox->cm_fmt_desc = NULL;
    }

    if (videotoolbox->session) {
        VTDecompressionSessionInvalidate(videotoolbox->session);
        CFRelease(videotoolbox->session);
        videotoolbox->session = NULL;
    }
}

int ff_videotoolbox_uninit(AVCodecContext *avctx)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    if (!vtctx)
        return 0;

    av_freep(&vtctx->bitstream);
    if (vtctx->frame)
        CVPixelBufferRelease(vtctx->frame);

    if (vtctx->vt_ctx)
        videotoolbox_stop(avctx);

    av_buffer_unref(&vtctx->cached_hw_frames_ctx);
    av_freep(&vtctx->vt_ctx);

    return 0;
}

static int videotoolbox_buffer_create(AVCodecContext *avctx, AVFrame *frame)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    CVPixelBufferRef pixbuf = (CVPixelBufferRef)vtctx->frame;
    OSType pixel_format = CVPixelBufferGetPixelFormatType(pixbuf);
    enum AVPixelFormat sw_format = av_map_videotoolbox_format_to_pixfmt(pixel_format);
    int width = CVPixelBufferGetWidth(pixbuf);
    int height = CVPixelBufferGetHeight(pixbuf);
    AVHWFramesContext *cached_frames;
    VTHWFrame *ref;
    int ret;

    if (!frame->buf[0] || frame->data[3]) {
        av_log(avctx, AV_LOG_ERROR, "videotoolbox: invalid state\n");
        av_frame_unref(frame);
        return AVERROR_EXTERNAL;
    }

    ref = (VTHWFrame *)frame->buf[0]->data;

    if (ref->pixbuf)
        CVPixelBufferRelease(ref->pixbuf);
    ref->pixbuf = vtctx->frame;
    vtctx->frame = NULL;

    // Old API code path.
    if (!vtctx->cached_hw_frames_ctx)
        return 0;

    cached_frames = (AVHWFramesContext*)vtctx->cached_hw_frames_ctx->data;

    if (cached_frames->sw_format != sw_format ||
        cached_frames->width != width ||
        cached_frames->height != height) {
        AVBufferRef *hw_frames_ctx = av_hwframe_ctx_alloc(cached_frames->device_ref);
        AVHWFramesContext *hw_frames;
        AVVTFramesContext *hw_ctx;
        if (!hw_frames_ctx)
            return AVERROR(ENOMEM);

        hw_frames = (AVHWFramesContext*)hw_frames_ctx->data;
        hw_frames->format = cached_frames->format;
        hw_frames->sw_format = sw_format;
        hw_frames->width = width;
        hw_frames->height = height;
        hw_ctx = hw_frames->hwctx;
        hw_ctx->color_range = avctx->color_range;

        ret = av_hwframe_ctx_init(hw_frames_ctx);
        if (ret < 0) {
            av_buffer_unref(&hw_frames_ctx);
            return ret;
        }

        av_buffer_unref(&vtctx->cached_hw_frames_ctx);
        vtctx->cached_hw_frames_ctx = hw_frames_ctx;
    }

    av_buffer_unref(&ref->hw_frames_ctx);
    ref->hw_frames_ctx = av_buffer_ref(vtctx->cached_hw_frames_ctx);
    if (!ref->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static void videotoolbox_write_mp4_descr_length(PutByteContext *pb, int length)
{
    int i;
    uint8_t b;

    for (i = 3; i >= 0; i--) {
        b = (length >> (i * 7)) & 0x7F;
        if (i != 0)
            b |= 0x80;

        bytestream2_put_byteu(pb, b);
    }
}

static CFDataRef videotoolbox_esds_extradata_create(AVCodecContext *avctx)
{
    CFDataRef data;
    uint8_t *rw_extradata;
    PutByteContext pb;
    int full_size = 3 + 5 + 13 + 5 + avctx->extradata_size + 3;
    // ES_DescrTag data + DecoderConfigDescrTag + data + DecSpecificInfoTag + size + SLConfigDescriptor
    int config_size = 13 + 5 + avctx->extradata_size;
    int s;

    if (!(rw_extradata = av_mallocz(full_size + VIDEOTOOLBOX_ESDS_EXTRADATA_PADDING)))
        return NULL;

    bytestream2_init_writer(&pb, rw_extradata, full_size + VIDEOTOOLBOX_ESDS_EXTRADATA_PADDING);
    bytestream2_put_byteu(&pb, 0);        // version
    bytestream2_put_ne24(&pb, 0);         // flags

    // elementary stream descriptor
    bytestream2_put_byteu(&pb, 0x03);     // ES_DescrTag
    videotoolbox_write_mp4_descr_length(&pb, full_size);
    bytestream2_put_ne16(&pb, 0);         // esid
    bytestream2_put_byteu(&pb, 0);        // stream priority (0-32)

    // decoder configuration descriptor
    bytestream2_put_byteu(&pb, 0x04);     // DecoderConfigDescrTag
    videotoolbox_write_mp4_descr_length(&pb, config_size);
    bytestream2_put_byteu(&pb, 32);       // object type indication. 32 = AV_CODEC_ID_MPEG4
    bytestream2_put_byteu(&pb, 0x11);     // stream type
    bytestream2_put_ne24(&pb, 0);         // buffer size
    bytestream2_put_ne32(&pb, 0);         // max bitrate
    bytestream2_put_ne32(&pb, 0);         // avg bitrate

    // decoder specific descriptor
    bytestream2_put_byteu(&pb, 0x05);     ///< DecSpecificInfoTag
    videotoolbox_write_mp4_descr_length(&pb, avctx->extradata_size);

    bytestream2_put_buffer(&pb, avctx->extradata, avctx->extradata_size);

    // SLConfigDescriptor
    bytestream2_put_byteu(&pb, 0x06);     // SLConfigDescrTag
    bytestream2_put_byteu(&pb, 0x01);     // length
    bytestream2_put_byteu(&pb, 0x02);     //

    s = bytestream2_size_p(&pb);

    data = CFDataCreate(kCFAllocatorDefault, rw_extradata, s);

    av_freep(&rw_extradata);
    return data;
}

static CMSampleBufferRef videotoolbox_sample_buffer_create(CMFormatDescriptionRef fmt_desc,
                                                           void *buffer,
                                                           int size)
{
    OSStatus status;
    CMBlockBufferRef  block_buf;
    CMSampleBufferRef sample_buf;

    block_buf  = NULL;
    sample_buf = NULL;

    status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,// structureAllocator
                                                buffer,             // memoryBlock
                                                size,               // blockLength
                                                kCFAllocatorNull,   // blockAllocator
                                                NULL,               // customBlockSource
                                                0,                  // offsetToData
                                                size,               // dataLength
                                                0,                  // flags
                                                &block_buf);

    if (!status) {
        status = CMSampleBufferCreate(kCFAllocatorDefault,  // allocator
                                      block_buf,            // dataBuffer
                                      TRUE,                 // dataReady
                                      0,                    // makeDataReadyCallback
                                      0,                    // makeDataReadyRefcon
                                      fmt_desc,             // formatDescription
                                      1,                    // numSamples
                                      0,                    // numSampleTimingEntries
                                      NULL,                 // sampleTimingArray
                                      0,                    // numSampleSizeEntries
                                      NULL,                 // sampleSizeArray
                                      &sample_buf);
    }

    if (block_buf)
        CFRelease(block_buf);

    return sample_buf;
}

static void videotoolbox_decoder_callback(void *opaque,
                                          void *sourceFrameRefCon,
                                          OSStatus status,
                                          VTDecodeInfoFlags flags,
                                          CVImageBufferRef image_buffer,
                                          CMTime pts,
                                          CMTime duration)
{
    VTContext *vtctx = opaque;

    if (vtctx->frame) {
        CVPixelBufferRelease(vtctx->frame);
        vtctx->frame = NULL;
    }

    if (!image_buffer) {
        av_log(vtctx->logctx, status ? AV_LOG_WARNING : AV_LOG_DEBUG,
               "vt decoder cb: output image buffer is null: %i\n", status);
        return;
    }

    vtctx->frame = CVPixelBufferRetain(image_buffer);
}

static OSStatus videotoolbox_session_decode_frame(AVCodecContext *avctx)
{
    OSStatus status;
    CMSampleBufferRef sample_buf;
    AVVideotoolboxContext *videotoolbox = videotoolbox_get_context(avctx);
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    sample_buf = videotoolbox_sample_buffer_create(videotoolbox->cm_fmt_desc,
                                                   vtctx->bitstream,
                                                   vtctx->bitstream_size);

    if (!sample_buf)
        return -1;

    status = VTDecompressionSessionDecodeFrame(videotoolbox->session,
                                               sample_buf,
                                               0,       // decodeFlags
                                               NULL,    // sourceFrameRefCon
                                               0);      // infoFlagsOut
    if (status == noErr)
        status = VTDecompressionSessionWaitForAsynchronousFrames(videotoolbox->session);

    CFRelease(sample_buf);

    return status;
}

static CMVideoFormatDescriptionRef videotoolbox_format_desc_create(CMVideoCodecType codec_type,
                                                                   CFDictionaryRef decoder_spec,
                                                                   int width,
                                                                   int height)
{
    CMFormatDescriptionRef cm_fmt_desc;
    OSStatus status;

    status = CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                            codec_type,
                                            width,
                                            height,
                                            decoder_spec, // Dictionary of extension
                                            &cm_fmt_desc);

    if (status)
        return NULL;

    return cm_fmt_desc;
}

static CFDictionaryRef videotoolbox_buffer_attributes_create(int width,
                                                             int height,
                                                             OSType pix_fmt)
{
    CFMutableDictionaryRef buffer_attributes;
    CFMutableDictionaryRef io_surface_properties;
    CFNumberRef cv_pix_fmt;
    CFNumberRef w;
    CFNumberRef h;

    w = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &width);
    h = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &height);
    cv_pix_fmt = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pix_fmt);

    buffer_attributes = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                  4,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    io_surface_properties = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                      0,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks);

    if (pix_fmt)
        CFDictionarySetValue(buffer_attributes, kCVPixelBufferPixelFormatTypeKey, cv_pix_fmt);
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferIOSurfacePropertiesKey, io_surface_properties);
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferWidthKey, w);
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferHeightKey, h);
#if TARGET_OS_IPHONE
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferOpenGLESCompatibilityKey, kCFBooleanTrue);
#else
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferIOSurfaceOpenGLTextureCompatibilityKey, kCFBooleanTrue);
#endif

    CFRelease(io_surface_properties);
    CFRelease(cv_pix_fmt);
    CFRelease(w);
    CFRelease(h);

    return buffer_attributes;
}

static CFDictionaryRef videotoolbox_decoder_config_create(CMVideoCodecType codec_type,
                                                          AVCodecContext *avctx)
{
    CFMutableDictionaryRef avc_info;
    CFDataRef data = NULL;

    CFMutableDictionaryRef config_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                   0,
                                                                   &kCFTypeDictionaryKeyCallBacks,
                                                                   &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(config_info,
                         codec_type == kCMVideoCodecType_HEVC ?
                            kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder :
                            kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder,
                         kCFBooleanTrue);

    avc_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                         1,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);

    switch (codec_type) {
    case kCMVideoCodecType_MPEG4Video :
        if (avctx->extradata_size)
            data = videotoolbox_esds_extradata_create(avctx);
        if (data)
            CFDictionarySetValue(avc_info, CFSTR("esds"), data);
        break;
    case kCMVideoCodecType_H264 :
        data = ff_videotoolbox_avcc_extradata_create(avctx);
        if (data)
            CFDictionarySetValue(avc_info, CFSTR("avcC"), data);
        break;
    case kCMVideoCodecType_HEVC :
        data = ff_videotoolbox_hvcc_extradata_create(avctx);
        if (data)
            CFDictionarySetValue(avc_info, CFSTR("hvcC"), data);
        break;
#if CONFIG_VP9_VIDEOTOOLBOX_HWACCEL
    case kCMVideoCodecType_VP9 :
        data = ff_videotoolbox_vpcc_extradata_create(avctx);
        if (data)
            CFDictionarySetValue(avc_info, CFSTR("vpcC"), data);
        break;
#endif
    default:
        break;
    }

    CFDictionarySetValue(config_info,
            kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
            avc_info);

    if (data)
        CFRelease(data);

    CFRelease(avc_info);
    return config_info;
}

static int videotoolbox_start(AVCodecContext *avctx)
{
    AVVideotoolboxContext *videotoolbox = videotoolbox_get_context(avctx);
    OSStatus status;
    VTDecompressionOutputCallbackRecord decoder_cb;
    CFDictionaryRef decoder_spec;
    CFDictionaryRef buf_attr;

    if (!videotoolbox) {
        av_log(avctx, AV_LOG_ERROR, "hwaccel context is not set\n");
        return -1;
    }

    switch( avctx->codec_id ) {
    case AV_CODEC_ID_H263 :
        videotoolbox->cm_codec_type = kCMVideoCodecType_H263;
        break;
    case AV_CODEC_ID_H264 :
        videotoolbox->cm_codec_type = kCMVideoCodecType_H264;
        break;
    case AV_CODEC_ID_HEVC :
        videotoolbox->cm_codec_type = kCMVideoCodecType_HEVC;
        break;
    case AV_CODEC_ID_MPEG1VIDEO :
        videotoolbox->cm_codec_type = kCMVideoCodecType_MPEG1Video;
        break;
    case AV_CODEC_ID_MPEG2VIDEO :
        videotoolbox->cm_codec_type = kCMVideoCodecType_MPEG2Video;
        break;
    case AV_CODEC_ID_MPEG4 :
        videotoolbox->cm_codec_type = kCMVideoCodecType_MPEG4Video;
        break;
    case AV_CODEC_ID_PRORES :
        switch (avctx->codec_tag) {
        default:
            av_log(avctx, AV_LOG_WARNING, "Unknown prores profile %d\n", avctx->codec_tag);
        // fall-through
        case MKTAG('a','p','c','o'): // kCMVideoCodecType_AppleProRes422Proxy
        case MKTAG('a','p','c','s'): // kCMVideoCodecType_AppleProRes422LT
        case MKTAG('a','p','c','n'): // kCMVideoCodecType_AppleProRes422
        case MKTAG('a','p','c','h'): // kCMVideoCodecType_AppleProRes422HQ
        case MKTAG('a','p','4','h'): // kCMVideoCodecType_AppleProRes4444
        case MKTAG('a','p','4','x'): // kCMVideoCodecType_AppleProRes4444XQ
            videotoolbox->cm_codec_type = av_bswap32(avctx->codec_tag);
            break;
        }
        break;
    case AV_CODEC_ID_VP9 :
        videotoolbox->cm_codec_type = kCMVideoCodecType_VP9;
        break;
    default :
        break;
    }

#if defined(MAC_OS_X_VERSION_10_9) && !TARGET_OS_IPHONE && (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_9) && AV_HAS_BUILTIN(__builtin_available)
    if (avctx->codec_id == AV_CODEC_ID_PRORES) {
        if (__builtin_available(macOS 10.9, *)) {
            VTRegisterProfessionalVideoWorkflowVideoDecoders();
        }
    }
#endif

#if defined(MAC_OS_VERSION_11_0) && !TARGET_OS_IPHONE && (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_11_0) && AV_HAS_BUILTIN(__builtin_available)
    if (__builtin_available(macOS 11.0, *)) {
        VTRegisterSupplementalVideoDecoderIfAvailable(videotoolbox->cm_codec_type);
    }
#endif

    decoder_spec = videotoolbox_decoder_config_create(videotoolbox->cm_codec_type, avctx);

    if (!decoder_spec) {
        av_log(avctx, AV_LOG_ERROR, "decoder specification creation failed\n");
        return -1;
    }

    videotoolbox->cm_fmt_desc = videotoolbox_format_desc_create(videotoolbox->cm_codec_type,
                                                                decoder_spec,
                                                                avctx->width,
                                                                avctx->height);
    if (!videotoolbox->cm_fmt_desc) {
        if (decoder_spec)
            CFRelease(decoder_spec);

        av_log(avctx, AV_LOG_ERROR, "format description creation failed\n");
        return -1;
    }

    buf_attr = videotoolbox_buffer_attributes_create(avctx->width,
                                                     avctx->height,
                                                     videotoolbox->cv_pix_fmt_type);

    decoder_cb.decompressionOutputCallback = videotoolbox_decoder_callback;
    decoder_cb.decompressionOutputRefCon   = avctx->internal->hwaccel_priv_data;

    status = VTDecompressionSessionCreate(NULL,                      // allocator
                                          videotoolbox->cm_fmt_desc, // videoFormatDescription
                                          decoder_spec,              // videoDecoderSpecification
                                          buf_attr,                  // destinationImageBufferAttributes
                                          &decoder_cb,               // outputCallback
                                          &videotoolbox->session);   // decompressionSessionOut

    if (decoder_spec)
        CFRelease(decoder_spec);
    if (buf_attr)
        CFRelease(buf_attr);

    switch (status) {
    case kVTVideoDecoderNotAvailableNowErr:
        av_log(avctx, AV_LOG_VERBOSE, "VideoToolbox session not available.\n");
        return AVERROR(ENOSYS);
    case kVTVideoDecoderUnsupportedDataFormatErr:
        av_log(avctx, AV_LOG_VERBOSE, "VideoToolbox does not support this format.\n");
        return AVERROR(ENOSYS);
    case kVTCouldNotFindVideoDecoderErr:
        av_log(avctx, AV_LOG_VERBOSE, "VideoToolbox decoder for this format not found.\n");
        return AVERROR(ENOSYS);
    case kVTVideoDecoderMalfunctionErr:
        av_log(avctx, AV_LOG_VERBOSE, "VideoToolbox malfunction.\n");
        return AVERROR(EINVAL);
    case kVTVideoDecoderBadDataErr:
        av_log(avctx, AV_LOG_VERBOSE, "VideoToolbox reported invalid data.\n");
        return AVERROR_INVALIDDATA;
    case 0:
        return 0;
    default:
        av_log(avctx, AV_LOG_VERBOSE, "Unknown VideoToolbox session creation error %d\n", (int)status);
        return AVERROR_UNKNOWN;
    }
}

static const char *videotoolbox_error_string(OSStatus status)
{
    switch (status) {
        case kVTVideoDecoderBadDataErr:
            return "bad data";
        case kVTVideoDecoderMalfunctionErr:
            return "decoder malfunction";
        case kVTInvalidSessionErr:
            return "invalid session";
    }
    return "unknown";
}

int ff_videotoolbox_common_end_frame(AVCodecContext *avctx, AVFrame *frame)
{
    OSStatus status;
    AVVideotoolboxContext *videotoolbox = videotoolbox_get_context(avctx);
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    if (vtctx->reconfig_needed == true) {
        vtctx->reconfig_needed = false;
        av_log(avctx, AV_LOG_VERBOSE, "VideoToolbox decoder needs reconfig, restarting..\n");
        videotoolbox_stop(avctx);
        if (videotoolbox_start(avctx) != 0) {
            return AVERROR_EXTERNAL;
        }
    }

    if (!videotoolbox->session || !vtctx->bitstream || !vtctx->bitstream_size)
        return AVERROR_INVALIDDATA;

    status = videotoolbox_session_decode_frame(avctx);
    if (status != noErr) {
        if (status == kVTVideoDecoderMalfunctionErr || status == kVTInvalidSessionErr)
            vtctx->reconfig_needed = true;
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%s, %d)\n", videotoolbox_error_string(status), (int)status);
        return AVERROR_UNKNOWN;
    }

    if (!vtctx->frame) {
        vtctx->reconfig_needed = true;
        return AVERROR_UNKNOWN;
    }

    return videotoolbox_buffer_create(avctx, frame);
}

static int videotoolbox_h264_end_frame(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    AVFrame *frame = h->cur_pic_ptr->f;
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    int ret = ff_videotoolbox_common_end_frame(avctx, frame);
    vtctx->bitstream_size = 0;
    return ret;
}

static int videotoolbox_hevc_start_frame(AVCodecContext *avctx,
                                         const uint8_t *buffer,
                                         uint32_t size)
{
    HEVCContext *h = avctx->priv_data;
    AVFrame *frame = h->cur_frame->f;

    frame->crop_right  = 0;
    frame->crop_left   = 0;
    frame->crop_top    = 0;
    frame->crop_bottom = 0;

    return 0;
}

static int videotoolbox_hevc_decode_slice(AVCodecContext *avctx,
                                          const uint8_t *buffer,
                                          uint32_t size)
{
    return videotoolbox_common_decode_slice(avctx, buffer, size);
}


static int videotoolbox_hevc_decode_params(AVCodecContext *avctx,
                                           int type,
                                           const uint8_t *buffer,
                                           uint32_t size)
{
    return videotoolbox_common_decode_slice(avctx, buffer, size);
}

static int videotoolbox_hevc_end_frame(AVCodecContext *avctx)
{
    HEVCContext *h = avctx->priv_data;
    AVFrame *frame = h->cur_frame->f;
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    int ret;

    ret = ff_videotoolbox_common_end_frame(avctx, frame);
    vtctx->bitstream_size = 0;
    return ret;
}

static int videotoolbox_mpeg_start_frame(AVCodecContext *avctx,
                                         const uint8_t *buffer,
                                         uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    return ff_videotoolbox_buffer_copy(vtctx, buffer, size);
}

static int videotoolbox_mpeg_decode_slice(AVCodecContext *avctx,
                                          const uint8_t *buffer,
                                          uint32_t size)
{
    return 0;
}

static int videotoolbox_mpeg_end_frame(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    AVFrame *frame = s->cur_pic.ptr->f;

    return ff_videotoolbox_common_end_frame(avctx, frame);
}

static int videotoolbox_prores_start_frame(AVCodecContext *avctx,
                                         const uint8_t *buffer,
                                         uint32_t size)
{
    return 0;
}

static int videotoolbox_prores_decode_slice(AVCodecContext *avctx,
                                          const uint8_t *buffer,
                                          uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    return ff_videotoolbox_buffer_copy(vtctx, buffer, size);
}

static int videotoolbox_prores_end_frame(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;
    AVFrame *frame = ctx->frame;

    return ff_videotoolbox_common_end_frame(avctx, frame);
}

static enum AVPixelFormat videotoolbox_best_pixel_format(AVCodecContext *avctx) {
    int depth;
    const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!descriptor)
        return AV_PIX_FMT_NV12; // same as av_videotoolbox_alloc_context()


    if (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA)
        return AV_PIX_FMT_AYUV64;

    depth = descriptor->comp[0].depth;

#if HAVE_KCVPIXELFORMATTYPE_444YPCBCR16BIPLANARVIDEORANGE
    if (depth > 10)
        return descriptor->log2_chroma_w == 0 ? AV_PIX_FMT_P416 : AV_PIX_FMT_P216;
#endif

#if HAVE_KCVPIXELFORMATTYPE_444YPCBCR10BIPLANARVIDEORANGE
    if (descriptor->log2_chroma_w == 0) {
#if HAVE_KCVPIXELFORMATTYPE_444YPCBCR8BIPLANARVIDEORANGE
        if (depth <= 8)
            return AV_PIX_FMT_NV24;
#endif
        return AV_PIX_FMT_P410;
    }
#endif
#if HAVE_KCVPIXELFORMATTYPE_422YPCBCR10BIPLANARVIDEORANGE
    if (descriptor->log2_chroma_h == 0) {
#if HAVE_KCVPIXELFORMATTYPE_422YPCBCR8BIPLANARVIDEORANGE
        if (depth <= 8)
            return AV_PIX_FMT_NV16;
#endif
        return AV_PIX_FMT_P210;
    }
#endif
#if HAVE_KCVPIXELFORMATTYPE_420YPCBCR10BIPLANARVIDEORANGE
    if (depth > 8) {
        return AV_PIX_FMT_P010;
    }
#endif

    return AV_PIX_FMT_NV12;
}

static AVVideotoolboxContext *videotoolbox_alloc_context_with_pix_fmt(enum AVPixelFormat pix_fmt,
                                                                      bool full_range)
{
    AVVideotoolboxContext *ret = av_mallocz(sizeof(*ret));

    if (ret) {
        OSType cv_pix_fmt_type = av_map_videotoolbox_format_from_pixfmt2(pix_fmt, full_range);
        if (cv_pix_fmt_type == 0) {
            cv_pix_fmt_type = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        }
        ret->cv_pix_fmt_type = cv_pix_fmt_type;
    }

    return ret;
}

int ff_videotoolbox_common_init(AVCodecContext *avctx)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    AVHWFramesContext *hw_frames;
    AVVTFramesContext *hw_ctx;
    int err;
    bool full_range;

    vtctx->logctx = avctx;

    if (!avctx->hw_frames_ctx && !avctx->hw_device_ctx &&
            avctx->hwaccel_context)
        return videotoolbox_start(avctx);

    if (!avctx->hw_frames_ctx && !avctx->hw_device_ctx) {
        av_log(avctx, AV_LOG_ERROR,
               "Either hw_frames_ctx or hw_device_ctx must be set.\n");
        return AVERROR(EINVAL);
    }

    vtctx->vt_ctx = videotoolbox_alloc_context_with_pix_fmt(AV_PIX_FMT_NONE, false);
    if (!vtctx->vt_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (avctx->hw_frames_ctx) {
        hw_frames = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    } else {
        avctx->hw_frames_ctx = av_hwframe_ctx_alloc(avctx->hw_device_ctx);
        if (!avctx->hw_frames_ctx) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        hw_frames = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        hw_frames->format = AV_PIX_FMT_VIDEOTOOLBOX;
        hw_frames->sw_format = videotoolbox_best_pixel_format(avctx);
        hw_frames->width = avctx->width;
        hw_frames->height = avctx->height;
        hw_ctx = hw_frames->hwctx;
        hw_ctx->color_range = avctx->color_range;

        err = av_hwframe_ctx_init(avctx->hw_frames_ctx);
        if (err < 0) {
            av_buffer_unref(&avctx->hw_frames_ctx);
            goto fail;
        }
    }

    vtctx->cached_hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
    if (!vtctx->cached_hw_frames_ctx) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    full_range = avctx->color_range == AVCOL_RANGE_JPEG;
    vtctx->vt_ctx->cv_pix_fmt_type =
        av_map_videotoolbox_format_from_pixfmt2(hw_frames->sw_format, full_range);
    if (!vtctx->vt_ctx->cv_pix_fmt_type) {
        const AVPixFmtDescriptor *attempted_format =
            av_pix_fmt_desc_get(hw_frames->sw_format);
        av_log(avctx, AV_LOG_ERROR,
               "Failed to map underlying FFmpeg pixel format %s (%s range) to "
               "a VideoToolbox format!\n",
               attempted_format ? attempted_format->name : "<unknown>",
               av_color_range_name(avctx->color_range));
        err = AVERROR(EINVAL);
        goto fail;
    }

    err = videotoolbox_start(avctx);
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_videotoolbox_uninit(avctx);
    return err;
}

int ff_videotoolbox_frame_params(AVCodecContext *avctx,
                                 AVBufferRef *hw_frames_ctx)
{
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;

    frames_ctx->format            = AV_PIX_FMT_VIDEOTOOLBOX;
    frames_ctx->width             = avctx->coded_width;
    frames_ctx->height            = avctx->coded_height;
    frames_ctx->sw_format         = videotoolbox_best_pixel_format(avctx);

    return 0;
}

const FFHWAccel ff_h263_videotoolbox_hwaccel = {
    .p.name         = "h263_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H263,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

const FFHWAccel ff_hevc_videotoolbox_hwaccel = {
    .p.name         = "hevc_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_HEVC,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_hevc_start_frame,
    .decode_slice   = videotoolbox_hevc_decode_slice,
    .decode_params  = videotoolbox_hevc_decode_params,
    .end_frame      = videotoolbox_hevc_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

const FFHWAccel ff_h264_videotoolbox_hwaccel = {
    .p.name         = "h264_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H264,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = ff_videotoolbox_h264_start_frame,
    .decode_slice   = ff_videotoolbox_h264_decode_slice,
    .decode_params  = videotoolbox_h264_decode_params,
    .end_frame      = videotoolbox_h264_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

const FFHWAccel ff_mpeg1_videotoolbox_hwaccel = {
    .p.name         = "mpeg1_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG1VIDEO,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

const FFHWAccel ff_mpeg2_videotoolbox_hwaccel = {
    .p.name         = "mpeg2_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG2VIDEO,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

const FFHWAccel ff_mpeg4_videotoolbox_hwaccel = {
    .p.name         = "mpeg4_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MPEG4,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

const FFHWAccel ff_prores_videotoolbox_hwaccel = {
    .p.name         = "prores_videotoolbox",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PRORES,
    .p.pix_fmt      = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_prores_start_frame,
    .decode_slice   = videotoolbox_prores_decode_slice,
    .end_frame      = videotoolbox_prores_end_frame,
    .frame_params   = ff_videotoolbox_frame_params,
    .init           = ff_videotoolbox_common_init,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

#endif /* CONFIG_VIDEOTOOLBOX */
