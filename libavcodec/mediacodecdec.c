/*
 * Android MediaCodec MPEG-2 / H.264 / H.265 / MPEG-4 / VP8 / VP9 decoders
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include "config_components.h"

#include <stdint.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixfmt.h"
#include "libavutil/internal.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "h264_parse.h"
#include "h264_ps.h"
#include "hevc_parse.h"
#include "hwconfig.h"
#include "internal.h"
#include "mediacodec_wrapper.h"
#include "mediacodecdec_common.h"

typedef struct MediaCodecH264DecContext {

    AVClass *avclass;

    MediaCodecDecContext *ctx;

    AVPacket buffered_pkt;

    int delay_flush;
    int amlogic_mpeg2_api23_workaround;

} MediaCodecH264DecContext;

static av_cold int mediacodec_decode_close(AVCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    ff_mediacodec_dec_close(avctx, s->ctx);
    s->ctx = NULL;

    av_packet_unref(&s->buffered_pkt);

    return 0;
}

#if CONFIG_H264_MEDIACODEC_DECODER || CONFIG_HEVC_MEDIACODEC_DECODER
static int h2645_ps_to_nalu(const uint8_t *src, int src_size, uint8_t **out, int *out_size)
{
    int i;
    int ret = 0;
    uint8_t *p = NULL;
    static const uint8_t nalu_header[] = { 0x00, 0x00, 0x00, 0x01 };

    if (!out || !out_size) {
        return AVERROR(EINVAL);
    }

    p = av_malloc(sizeof(nalu_header) + src_size);
    if (!p) {
        return AVERROR(ENOMEM);
    }

    *out = p;
    *out_size = sizeof(nalu_header) + src_size;

    memcpy(p, nalu_header, sizeof(nalu_header));
    memcpy(p + sizeof(nalu_header), src, src_size);

    /* Escape 0x00, 0x00, 0x0{0-3} pattern */
    for (i = 4; i < *out_size; i++) {
        if (i < *out_size - 3 &&
            p[i + 0] == 0 &&
            p[i + 1] == 0 &&
            p[i + 2] <= 3) {
            uint8_t *new;

            *out_size += 1;
            new = av_realloc(*out, *out_size);
            if (!new) {
                ret = AVERROR(ENOMEM);
                goto done;
            }
            *out = p = new;

            i = i + 2;
            memmove(p + i + 1, p + i, *out_size - (i + 1));
            p[i] = 0x03;
        }
    }
done:
    if (ret < 0) {
        av_freep(out);
        *out_size = 0;
    }

    return ret;
}
#endif

#if CONFIG_H264_MEDIACODEC_DECODER
static int h264_set_extradata(AVCodecContext *avctx, FFAMediaFormat *format)
{
    int i;
    int ret;

    H264ParamSets ps;
    const PPS *pps = NULL;
    const SPS *sps = NULL;
    int is_avc = 0;
    int nal_length_size = 0;

    memset(&ps, 0, sizeof(ps));

    ret = ff_h264_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ps, &is_avc, &nal_length_size, 0, avctx);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < MAX_PPS_COUNT; i++) {
        if (ps.pps_list[i]) {
            pps = (const PPS*)ps.pps_list[i]->data;
            break;
        }
    }

    if (pps) {
        if (ps.sps_list[pps->sps_id]) {
            sps = (const SPS*)ps.sps_list[pps->sps_id]->data;
        }
    }

    if (pps && sps) {
        uint8_t *data = NULL;
        int data_size = 0;

        avctx->profile = ff_h264_get_profile(sps);
        avctx->level = sps->level_idc;

        if ((ret = h2645_ps_to_nalu(sps->data, sps->data_size, &data, &data_size)) < 0) {
            goto done;
        }
        ff_AMediaFormat_setBuffer(format, "csd-0", (void*)data, data_size);
        av_freep(&data);

        if ((ret = h2645_ps_to_nalu(pps->data, pps->data_size, &data, &data_size)) < 0) {
            goto done;
        }
        ff_AMediaFormat_setBuffer(format, "csd-1", (void*)data, data_size);
        av_freep(&data);
    } else {
        const int warn = is_avc && (avctx->codec_tag == MKTAG('a','v','c','1') ||
                                    avctx->codec_tag == MKTAG('a','v','c','2'));
        av_log(avctx, warn ? AV_LOG_WARNING : AV_LOG_DEBUG,
               "Could not extract PPS/SPS from extradata\n");
        ret = 0;
    }

done:
    ff_h264_ps_uninit(&ps);

    return ret;
}
#endif

#if CONFIG_HEVC_MEDIACODEC_DECODER
static int hevc_set_extradata(AVCodecContext *avctx, FFAMediaFormat *format)
{
    int i;
    int ret;

    HEVCParamSets ps;
    HEVCSEI sei;

    const HEVCVPS *vps = NULL;
    const HEVCPPS *pps = NULL;
    const HEVCSPS *sps = NULL;
    int is_nalff = 0;
    int nal_length_size = 0;

    uint8_t *vps_data = NULL;
    uint8_t *sps_data = NULL;
    uint8_t *pps_data = NULL;
    int vps_data_size = 0;
    int sps_data_size = 0;
    int pps_data_size = 0;

    memset(&ps, 0, sizeof(ps));
    memset(&sei, 0, sizeof(sei));

    ret = ff_hevc_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ps, &sei, &is_nalff, &nal_length_size, 0, 1, avctx);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < HEVC_MAX_VPS_COUNT; i++) {
        if (ps.vps_list[i]) {
            vps = (const HEVCVPS*)ps.vps_list[i]->data;
            break;
        }
    }

    for (i = 0; i < HEVC_MAX_PPS_COUNT; i++) {
        if (ps.pps_list[i]) {
            pps = (const HEVCPPS*)ps.pps_list[i]->data;
            break;
        }
    }

    if (pps) {
        if (ps.sps_list[pps->sps_id]) {
            sps = (const HEVCSPS*)ps.sps_list[pps->sps_id]->data;
        }
    }

    if (vps && pps && sps) {
        uint8_t *data;
        int data_size;

        avctx->profile = sps->ptl.general_ptl.profile_idc;
        avctx->level   = sps->ptl.general_ptl.level_idc;

        if ((ret = h2645_ps_to_nalu(vps->data, vps->data_size, &vps_data, &vps_data_size)) < 0 ||
            (ret = h2645_ps_to_nalu(sps->data, sps->data_size, &sps_data, &sps_data_size)) < 0 ||
            (ret = h2645_ps_to_nalu(pps->data, pps->data_size, &pps_data, &pps_data_size)) < 0) {
            goto done;
        }

        data_size = vps_data_size + sps_data_size + pps_data_size;
        data = av_mallocz(data_size);
        if (!data) {
            ret = AVERROR(ENOMEM);
            goto done;
        }

        memcpy(data                                , vps_data, vps_data_size);
        memcpy(data + vps_data_size                , sps_data, sps_data_size);
        memcpy(data + vps_data_size + sps_data_size, pps_data, pps_data_size);

        ff_AMediaFormat_setBuffer(format, "csd-0", data, data_size);

        av_freep(&data);
    } else {
        const int warn = is_nalff && avctx->codec_tag == MKTAG('h','v','c','1');
        av_log(avctx, warn ? AV_LOG_WARNING : AV_LOG_DEBUG,
               "Could not extract VPS/PPS/SPS from extradata\n");
        ret = 0;
    }

done:
    ff_hevc_ps_uninit(&ps);

    av_freep(&vps_data);
    av_freep(&sps_data);
    av_freep(&pps_data);

    return ret;
}
#endif

#if CONFIG_MPEG2_MEDIACODEC_DECODER || \
    CONFIG_MPEG4_MEDIACODEC_DECODER || \
    CONFIG_VP8_MEDIACODEC_DECODER   || \
    CONFIG_VP9_MEDIACODEC_DECODER
static int common_set_extradata(AVCodecContext *avctx, FFAMediaFormat *format)
{
    int ret = 0;

    if (avctx->extradata) {
        ff_AMediaFormat_setBuffer(format, "csd-0", avctx->extradata, avctx->extradata_size);
    }

    return ret;
}
#endif

static av_cold int mediacodec_decode_init(AVCodecContext *avctx)
{
    int ret;
    int sdk_int;

    const char *codec_mime = NULL;

    FFAMediaFormat *format = NULL;
    MediaCodecH264DecContext *s = avctx->priv_data;

    format = ff_AMediaFormat_new();
    if (!format) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create media format\n");
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    switch (avctx->codec_id) {
#if CONFIG_H264_MEDIACODEC_DECODER
    case AV_CODEC_ID_H264:
        codec_mime = "video/avc";

        ret = h264_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_HEVC_MEDIACODEC_DECODER
    case AV_CODEC_ID_HEVC:
        codec_mime = "video/hevc";

        ret = hevc_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_MPEG2_MEDIACODEC_DECODER
    case AV_CODEC_ID_MPEG2VIDEO:
        codec_mime = "video/mpeg2";

        ret = common_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_MPEG4_MEDIACODEC_DECODER
    case AV_CODEC_ID_MPEG4:
        codec_mime = "video/mp4v-es",

        ret = common_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_VP8_MEDIACODEC_DECODER
    case AV_CODEC_ID_VP8:
        codec_mime = "video/x-vnd.on2.vp8";

        ret = common_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_VP9_MEDIACODEC_DECODER
    case AV_CODEC_ID_VP9:
        codec_mime = "video/x-vnd.on2.vp9";

        ret = common_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
    default:
        av_assert0(0);
    }

    ff_AMediaFormat_setString(format, "mime", codec_mime);
    ff_AMediaFormat_setInt32(format, "width", avctx->width);
    ff_AMediaFormat_setInt32(format, "height", avctx->height);

    s->ctx = av_mallocz(sizeof(*s->ctx));
    if (!s->ctx) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate MediaCodecDecContext\n");
        ret = AVERROR(ENOMEM);
        goto done;
    }

    s->ctx->delay_flush = s->delay_flush;

    if ((ret = ff_mediacodec_dec_init(avctx, s->ctx, codec_mime, format)) < 0) {
        s->ctx = NULL;
        goto done;
    }

    av_log(avctx, AV_LOG_INFO,
           "MediaCodec started successfully: codec = %s, ret = %d\n",
           s->ctx->codec_name, ret);

    sdk_int = ff_Build_SDK_INT(avctx);
    if (sdk_int <= 23 &&
        strcmp(s->ctx->codec_name, "OMX.amlogic.mpeg2.decoder.awesome") == 0) {
        av_log(avctx, AV_LOG_INFO, "Enabling workaround for %s on API=%d\n",
               s->ctx->codec_name, sdk_int);
        s->amlogic_mpeg2_api23_workaround = 1;
    }

done:
    if (format) {
        ff_AMediaFormat_delete(format);
    }

    if (ret < 0) {
        mediacodec_decode_close(avctx);
    }

    return ret;
}

static int mediacodec_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    MediaCodecH264DecContext *s = avctx->priv_data;
    int ret;
    ssize_t index;

    /* In delay_flush mode, wait until the user has released or rendered
       all retained frames. */
    if (s->delay_flush && ff_mediacodec_dec_is_flushing(avctx, s->ctx)) {
        if (!ff_mediacodec_dec_flush(avctx, s->ctx)) {
            return AVERROR(EAGAIN);
        }
    }

    /* poll for new frame */
    ret = ff_mediacodec_dec_receive(avctx, s->ctx, frame, false);
    if (ret != AVERROR(EAGAIN))
        return ret;

    /* feed decoder */
    while (1) {
        if (s->ctx->current_input_buffer < 0) {
            /* poll for input space */
            index = ff_AMediaCodec_dequeueInputBuffer(s->ctx->codec, 0);
            if (index < 0) {
                /* no space, block for an output frame to appear */
                return ff_mediacodec_dec_receive(avctx, s->ctx, frame, true);
            }
            s->ctx->current_input_buffer = index;
        }

        /* try to flush any buffered packet data */
        if (s->buffered_pkt.size > 0) {
            ret = ff_mediacodec_dec_send(avctx, s->ctx, &s->buffered_pkt, false);
            if (ret >= 0) {
                s->buffered_pkt.size -= ret;
                s->buffered_pkt.data += ret;
                if (s->buffered_pkt.size <= 0) {
                    av_packet_unref(&s->buffered_pkt);
                } else {
                    av_log(avctx, AV_LOG_WARNING,
                           "could not send entire packet in single input buffer (%d < %d)\n",
                           ret, s->buffered_pkt.size+ret);
                }
            } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
                return ret;
            }

            if (s->amlogic_mpeg2_api23_workaround && s->buffered_pkt.size <= 0) {
                /* fallthrough to fetch next packet regardless of input buffer space */
            } else {
                /* poll for space again */
                continue;
            }
        }

        /* fetch new packet or eof */
        ret = ff_decode_get_packet(avctx, &s->buffered_pkt);
        if (ret == AVERROR_EOF) {
            AVPacket null_pkt = { 0 };
            ret = ff_mediacodec_dec_send(avctx, s->ctx, &null_pkt, true);
            if (ret < 0)
                return ret;
            return ff_mediacodec_dec_receive(avctx, s->ctx, frame, true);
        } else if (ret == AVERROR(EAGAIN) && s->ctx->current_input_buffer < 0) {
            return ff_mediacodec_dec_receive(avctx, s->ctx, frame, true);
        } else if (ret < 0) {
            return ret;
        }
    }

    return AVERROR(EAGAIN);
}

static void mediacodec_decode_flush(AVCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    av_packet_unref(&s->buffered_pkt);

    ff_mediacodec_dec_flush(avctx, s->ctx);
}

static const AVCodecHWConfigInternal *const mediacodec_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public          = {
            .pix_fmt     = AV_PIX_FMT_MEDIACODEC,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_AD_HOC |
                           AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_MEDIACODEC,
        },
        .hwaccel         = NULL,
    },
    NULL
};

#define OFFSET(x) offsetof(MediaCodecH264DecContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption ff_mediacodec_vdec_options[] = {
    { "delay_flush", "Delay flush until hw output buffers are returned to the decoder",
                     OFFSET(delay_flush), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, VD },
    { NULL }
};

#define DECLARE_MEDIACODEC_VCLASS(short_name)                   \
static const AVClass ff_##short_name##_mediacodec_dec_class = { \
    .class_name = #short_name "_mediacodec",                    \
    .item_name  = av_default_item_name,                         \
    .option     = ff_mediacodec_vdec_options,                   \
    .version    = LIBAVUTIL_VERSION_INT,                        \
};

#define DECLARE_MEDIACODEC_VDEC(short_name, full_name, codec_id, bsf)                          \
DECLARE_MEDIACODEC_VCLASS(short_name)                                                          \
const FFCodec ff_ ## short_name ## _mediacodec_decoder = {                                     \
    .p.name         = #short_name "_mediacodec",                                               \
    .p.long_name    = NULL_IF_CONFIG_SMALL(full_name " Android MediaCodec decoder"),           \
    .p.type         = AVMEDIA_TYPE_VIDEO,                                                      \
    .p.id           = codec_id,                                                                \
    .p.priv_class   = &ff_##short_name##_mediacodec_dec_class,                                 \
    .priv_data_size = sizeof(MediaCodecH264DecContext),                                        \
    .init           = mediacodec_decode_init,                                                  \
    FF_CODEC_RECEIVE_FRAME_CB(mediacodec_receive_frame),                                       \
    .flush          = mediacodec_decode_flush,                                                 \
    .close          = mediacodec_decode_close,                                                 \
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE, \
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,                                               \
    .bsfs           = bsf,                                                                     \
    .hw_configs     = mediacodec_hw_configs,                                                   \
    .p.wrapper_name = "mediacodec",                                                            \
};                                                                                             \

#if CONFIG_H264_MEDIACODEC_DECODER
DECLARE_MEDIACODEC_VDEC(h264, "H.264", AV_CODEC_ID_H264, "h264_mp4toannexb")
#endif

#if CONFIG_HEVC_MEDIACODEC_DECODER
DECLARE_MEDIACODEC_VDEC(hevc, "H.265", AV_CODEC_ID_HEVC, "hevc_mp4toannexb")
#endif

#if CONFIG_MPEG2_MEDIACODEC_DECODER
DECLARE_MEDIACODEC_VDEC(mpeg2, "MPEG-2", AV_CODEC_ID_MPEG2VIDEO, NULL)
#endif

#if CONFIG_MPEG4_MEDIACODEC_DECODER
DECLARE_MEDIACODEC_VDEC(mpeg4, "MPEG-4", AV_CODEC_ID_MPEG4, NULL)
#endif

#if CONFIG_VP8_MEDIACODEC_DECODER
DECLARE_MEDIACODEC_VDEC(vp8, "VP8", AV_CODEC_ID_VP8, NULL)
#endif

#if CONFIG_VP9_MEDIACODEC_DECODER
DECLARE_MEDIACODEC_VDEC(vp9, "VP9", AV_CODEC_ID_VP9, NULL)
#endif
