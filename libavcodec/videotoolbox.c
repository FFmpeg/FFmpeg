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
#if CONFIG_VIDEOTOOLBOX
#  include "videotoolbox.h"
#else
#  include "vda.h"
#endif
#include "vda_vt_internal.h"
#include "libavutil/avutil.h"
#include "bytestream.h"
#include "h264dec.h"
#include "mpegvideo.h"

#ifndef kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder
#  define kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder CFSTR("RequireHardwareAcceleratedVideoDecoder")
#endif

#define VIDEOTOOLBOX_ESDS_EXTRADATA_PADDING  12

static void videotoolbox_buffer_release(void *opaque, uint8_t *data)
{
    CVPixelBufferRef cv_buffer = (CVImageBufferRef)data;
    CVPixelBufferRelease(cv_buffer);
}

static int videotoolbox_buffer_copy(VTContext *vtctx,
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

int ff_videotoolbox_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
    frame->width  = avctx->width;
    frame->height = avctx->height;
    frame->format = avctx->pix_fmt;
    frame->buf[0] = av_buffer_alloc(1);

    if (!frame->buf[0])
        return AVERROR(ENOMEM);

    return 0;
}

#define AV_W8(p, v) *(p) = (v)

CFDataRef ff_videotoolbox_avcc_extradata_create(AVCodecContext *avctx)
{
    H264Context *h     = avctx->priv_data;
    CFDataRef data = NULL;
    uint8_t *p;
    int vt_extradata_size = 6 + 2 + h->ps.sps->data_size + 3 + h->ps.pps->data_size;
    uint8_t *vt_extradata = av_malloc(vt_extradata_size);
    if (!vt_extradata)
        return NULL;

    p = vt_extradata;

    AV_W8(p + 0, 1); /* version */
    AV_W8(p + 1, h->ps.sps->data[1]); /* profile */
    AV_W8(p + 2, h->ps.sps->data[2]); /* profile compat */
    AV_W8(p + 3, h->ps.sps->data[3]); /* level */
    AV_W8(p + 4, 0xff); /* 6 bits reserved (111111) + 2 bits nal size length - 3 (11) */
    AV_W8(p + 5, 0xe1); /* 3 bits reserved (111) + 5 bits number of sps (00001) */
    AV_WB16(p + 6, h->ps.sps->data_size);
    memcpy(p + 8, h->ps.sps->data, h->ps.sps->data_size);
    p += 8 + h->ps.sps->data_size;
    AV_W8(p + 0, 1); /* number of pps */
    AV_WB16(p + 1, h->ps.pps->data_size);
    memcpy(p + 3, h->ps.pps->data, h->ps.pps->data_size);

    p += 3 + h->ps.pps->data_size;
    av_assert0(p - vt_extradata == vt_extradata_size);

    data = CFDataCreate(kCFAllocatorDefault, vt_extradata, vt_extradata_size);
    av_free(vt_extradata);
    return data;
}

int ff_videotoolbox_buffer_create(VTContext *vtctx, AVFrame *frame)
{
    av_buffer_unref(&frame->buf[0]);

    frame->buf[0] = av_buffer_create((uint8_t*)vtctx->frame,
                                     sizeof(vtctx->frame),
                                     videotoolbox_buffer_release,
                                     NULL,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        return AVERROR(ENOMEM);
    }

    frame->data[3] = (uint8_t*)vtctx->frame;
    vtctx->frame = NULL;

    return 0;
}

int ff_videotoolbox_h264_start_frame(AVCodecContext *avctx,
                                     const uint8_t *buffer,
                                     uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    H264Context *h  = avctx->priv_data;

    vtctx->bitstream_size = 0;

    if (h->is_avc == 1) {
        return videotoolbox_buffer_copy(vtctx, buffer, size);
    }

    return 0;
}

int ff_videotoolbox_h264_decode_slice(AVCodecContext *avctx,
                                      const uint8_t *buffer,
                                      uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    H264Context *h  = avctx->priv_data;
    void *tmp;

    if (h->is_avc == 1)
        return 0;

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

int ff_videotoolbox_uninit(AVCodecContext *avctx)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;
    if (vtctx) {
        av_freep(&vtctx->bitstream);
        if (vtctx->frame)
            CVPixelBufferRelease(vtctx->frame);
    }

    return 0;
}

#if CONFIG_VIDEOTOOLBOX
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
    AVCodecContext *avctx = opaque;
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    if (vtctx->frame) {
        CVPixelBufferRelease(vtctx->frame);
        vtctx->frame = NULL;
    }

    if (!image_buffer) {
        av_log(NULL, AV_LOG_DEBUG, "vt decoder cb: output image buffer is null\n");
        return;
    }

    vtctx->frame = CVPixelBufferRetain(image_buffer);
}

static OSStatus videotoolbox_session_decode_frame(AVCodecContext *avctx)
{
    OSStatus status;
    CMSampleBufferRef sample_buf;
    AVVideotoolboxContext *videotoolbox = avctx->hwaccel_context;
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

static int videotoolbox_common_end_frame(AVCodecContext *avctx, AVFrame *frame)
{
    int status;
    AVVideotoolboxContext *videotoolbox = avctx->hwaccel_context;
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    av_buffer_unref(&frame->buf[0]);

    if (!videotoolbox->session || !vtctx->bitstream)
        return AVERROR_INVALIDDATA;

    status = videotoolbox_session_decode_frame(avctx);

    if (status) {
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%d)\n", status);
        return AVERROR_UNKNOWN;
    }

    if (!vtctx->frame)
        return AVERROR_UNKNOWN;

    return ff_videotoolbox_buffer_create(vtctx, frame);
}

static int videotoolbox_h264_end_frame(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    AVFrame *frame = h->cur_pic_ptr->f;

    return videotoolbox_common_end_frame(avctx, frame);
}

static int videotoolbox_mpeg_start_frame(AVCodecContext *avctx,
                                         const uint8_t *buffer,
                                         uint32_t size)
{
    VTContext *vtctx = avctx->internal->hwaccel_priv_data;

    return videotoolbox_buffer_copy(vtctx, buffer, size);
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
    AVFrame *frame = s->current_picture_ptr->f;

    return videotoolbox_common_end_frame(avctx, frame);
}

static CFDictionaryRef videotoolbox_decoder_config_create(CMVideoCodecType codec_type,
                                                          AVCodecContext *avctx)
{
    CFMutableDictionaryRef config_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                                   0,
                                                                   &kCFTypeDictionaryKeyCallBacks,
                                                                   &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(config_info,
                         kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder,
                         kCFBooleanTrue);

    if (avctx->extradata_size) {
        CFMutableDictionaryRef avc_info;
        CFDataRef data = NULL;

        avc_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                             1,
                                             &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);

        switch (codec_type) {
        case kCMVideoCodecType_MPEG4Video :
            data = videotoolbox_esds_extradata_create(avctx);
            if (data)
                CFDictionarySetValue(avc_info, CFSTR("esds"), data);
            break;
        case kCMVideoCodecType_H264 :
            data = ff_videotoolbox_avcc_extradata_create(avctx);
            if (data)
                CFDictionarySetValue(avc_info, CFSTR("avcC"), data);
            break;
        default:
            break;
        }

        CFDictionarySetValue(config_info,
                kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
                avc_info);

        if (data)
            CFRelease(data);

        CFRelease(avc_info);
    }
    return config_info;
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

    CFDictionarySetValue(buffer_attributes, kCVPixelBufferPixelFormatTypeKey, cv_pix_fmt);
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferIOSurfacePropertiesKey, io_surface_properties);
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferWidthKey, w);
    CFDictionarySetValue(buffer_attributes, kCVPixelBufferHeightKey, h);

    CFRelease(io_surface_properties);
    CFRelease(cv_pix_fmt);
    CFRelease(w);
    CFRelease(h);

    return buffer_attributes;
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

static int videotoolbox_default_init(AVCodecContext *avctx)
{
    AVVideotoolboxContext *videotoolbox = avctx->hwaccel_context;
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
    case AV_CODEC_ID_MPEG1VIDEO :
        videotoolbox->cm_codec_type = kCMVideoCodecType_MPEG1Video;
        break;
    case AV_CODEC_ID_MPEG2VIDEO :
        videotoolbox->cm_codec_type = kCMVideoCodecType_MPEG2Video;
        break;
    case AV_CODEC_ID_MPEG4 :
        videotoolbox->cm_codec_type = kCMVideoCodecType_MPEG4Video;
        break;
    default :
        break;
    }

    decoder_spec = videotoolbox_decoder_config_create(videotoolbox->cm_codec_type, avctx);

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
    decoder_cb.decompressionOutputRefCon   = avctx;

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
    case kVTVideoDecoderUnsupportedDataFormatErr:
        return AVERROR(ENOSYS);
    case kVTVideoDecoderMalfunctionErr:
        return AVERROR(EINVAL);
    case kVTVideoDecoderBadDataErr :
        return AVERROR_INVALIDDATA;
    case 0:
        return 0;
    default:
        return AVERROR_UNKNOWN;
    }
}

static void videotoolbox_default_free(AVCodecContext *avctx)
{
    AVVideotoolboxContext *videotoolbox = avctx->hwaccel_context;

    if (videotoolbox) {
        if (videotoolbox->cm_fmt_desc)
            CFRelease(videotoolbox->cm_fmt_desc);

        if (videotoolbox->session) {
            VTDecompressionSessionInvalidate(videotoolbox->session);
            CFRelease(videotoolbox->session);
        }
    }
}

AVHWAccel ff_h263_videotoolbox_hwaccel = {
    .name           = "h263_videotoolbox",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263,
    .pix_fmt        = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

AVHWAccel ff_h264_videotoolbox_hwaccel = {
    .name           = "h264_videotoolbox",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = ff_videotoolbox_h264_start_frame,
    .decode_slice   = ff_videotoolbox_h264_decode_slice,
    .end_frame      = videotoolbox_h264_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

AVHWAccel ff_mpeg1_videotoolbox_hwaccel = {
    .name           = "mpeg1_videotoolbox",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG1VIDEO,
    .pix_fmt        = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

AVHWAccel ff_mpeg2_videotoolbox_hwaccel = {
    .name           = "mpeg2_videotoolbox",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG2VIDEO,
    .pix_fmt        = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

AVHWAccel ff_mpeg4_videotoolbox_hwaccel = {
    .name           = "mpeg4_videotoolbox",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG4,
    .pix_fmt        = AV_PIX_FMT_VIDEOTOOLBOX,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = videotoolbox_mpeg_start_frame,
    .decode_slice   = videotoolbox_mpeg_decode_slice,
    .end_frame      = videotoolbox_mpeg_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

AVVideotoolboxContext *av_videotoolbox_alloc_context(void)
{
    AVVideotoolboxContext *ret = av_mallocz(sizeof(*ret));

    if (ret) {
        ret->output_callback = videotoolbox_decoder_callback;
        ret->cv_pix_fmt_type = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    }

    return ret;
}

int av_videotoolbox_default_init(AVCodecContext *avctx)
{
    return av_videotoolbox_default_init2(avctx, NULL);
}

int av_videotoolbox_default_init2(AVCodecContext *avctx, AVVideotoolboxContext *vtctx)
{
    avctx->hwaccel_context = vtctx ?: av_videotoolbox_alloc_context();
    if (!avctx->hwaccel_context)
        return AVERROR(ENOMEM);
    return videotoolbox_default_init(avctx);
}

void av_videotoolbox_default_free(AVCodecContext *avctx)
{

    videotoolbox_default_free(avctx);
    av_freep(&avctx->hwaccel_context);
}
#endif /* CONFIG_VIDEOTOOLBOX */
