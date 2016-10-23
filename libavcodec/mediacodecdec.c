/*
 * Android MediaCodec H.264 / H.265 / MPEG-4 / VP8 / VP9 decoders
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

#include <stdint.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixfmt.h"
#include "libavutil/atomic.h"

#include "avcodec.h"
#include "h264_parse.h"
#include "hevc_parse.h"
#include "internal.h"
#include "mediacodec_wrapper.h"
#include "mediacodecdec_common.h"

typedef struct MediaCodecH264DecContext {

    MediaCodecDecContext *ctx;

    AVBSFContext *bsf;

    AVFifoBuffer *fifo;

    AVPacket filtered_pkt;

} MediaCodecH264DecContext;

static av_cold int mediacodec_decode_close(AVCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    ff_mediacodec_dec_close(avctx, s->ctx);
    s->ctx = NULL;

    av_fifo_free(s->fifo);

    av_bsf_free(&s->bsf);
    av_packet_unref(&s->filtered_pkt);

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
        av_log(avctx, AV_LOG_ERROR, "Could not extract PPS/SPS from extradata");
        ret = AVERROR_INVALIDDATA;
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

    ret = ff_hevc_decode_extradata(avctx->extradata, avctx->extradata_size,
                                &ps, &is_nalff, &nal_length_size, 0, avctx);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < MAX_VPS_COUNT; i++) {
        if (ps.vps_list[i]) {
            vps = (const HEVCVPS*)ps.vps_list[i]->data;
            break;
        }
    }

    for (i = 0; i < MAX_PPS_COUNT; i++) {
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
        av_log(avctx, AV_LOG_ERROR, "Could not extract VPS/PPS/SPS from extradata");
        ret = AVERROR_INVALIDDATA;
    }

done:
    av_freep(&vps_data);
    av_freep(&sps_data);
    av_freep(&pps_data);

    return ret;
}
#endif

#if CONFIG_MPEG4_MEDIACODEC_DECODER
static int mpeg4_set_extradata(AVCodecContext *avctx, FFAMediaFormat *format)
{
    int ret = 0;

    if (avctx->extradata) {
        ff_AMediaFormat_setBuffer(format, "csd-0", avctx->extradata, avctx->extradata_size);
    }

    return ret;
}
#endif

#if CONFIG_VP8_MEDIACODEC_DECODER || CONFIG_VP9_MEDIACODEC_DECODER
static int vpx_set_extradata(AVCodecContext *avctx, FFAMediaFormat *format)
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

    const char *codec_mime = NULL;

    const char *bsf_name = NULL;
    const AVBitStreamFilter *bsf = NULL;

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
        bsf_name = "h264_mp4toannexb";

        ret = h264_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_HEVC_MEDIACODEC_DECODER
    case AV_CODEC_ID_HEVC:
        codec_mime = "video/hevc";
        bsf_name = "hevc_mp4toannexb";

        ret = hevc_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_MPEG4_MEDIACODEC_DECODER
    case AV_CODEC_ID_MPEG4:
        codec_mime = "video/mp4v-es",

        ret = mpeg4_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_VP8_MEDIACODEC_DECODER
    case AV_CODEC_ID_VP8:
        codec_mime = "video/x-vnd.on2.vp8";

        ret = vpx_set_extradata(avctx, format);
        if (ret < 0)
            goto done;
        break;
#endif
#if CONFIG_VP9_MEDIACODEC_DECODER
    case AV_CODEC_ID_VP9:
        codec_mime = "video/x-vnd.on2.vp9";

        ret = vpx_set_extradata(avctx, format);
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

    if ((ret = ff_mediacodec_dec_init(avctx, s->ctx, codec_mime, format)) < 0) {
        s->ctx = NULL;
        goto done;
    }

    av_log(avctx, AV_LOG_INFO, "MediaCodec started successfully, ret = %d\n", ret);

    s->fifo = av_fifo_alloc(sizeof(AVPacket));
    if (!s->fifo) {
        ret = AVERROR(ENOMEM);
        goto done;
    }

    if (bsf_name) {
    bsf = av_bsf_get_by_name(bsf_name);
    if(!bsf) {
        ret = AVERROR_BSF_NOT_FOUND;
        goto done;
    }

    if ((ret = av_bsf_alloc(bsf, &s->bsf))) {
        goto done;
    }

    if (((ret = avcodec_parameters_from_context(s->bsf->par_in, avctx)) < 0) ||
        ((ret = av_bsf_init(s->bsf)) < 0)) {
          goto done;
    }
    }

    av_init_packet(&s->filtered_pkt);

done:
    if (format) {
        ff_AMediaFormat_delete(format);
    }

    if (ret < 0) {
        mediacodec_decode_close(avctx);
    }

    return ret;
}


static int mediacodec_process_data(AVCodecContext *avctx, AVFrame *frame,
                                   int *got_frame, AVPacket *pkt)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    return ff_mediacodec_dec_decode(avctx, s->ctx, frame, got_frame, pkt);
}

static int mediacodec_decode_frame(AVCodecContext *avctx, void *data,
                                   int *got_frame, AVPacket *avpkt)
{
    MediaCodecH264DecContext *s = avctx->priv_data;
    AVFrame *frame    = data;
    int ret;

    /* buffer the input packet */
    if (avpkt->size) {
        AVPacket input_pkt = { 0 };

        if (av_fifo_space(s->fifo) < sizeof(input_pkt)) {
            ret = av_fifo_realloc2(s->fifo,
                                   av_fifo_size(s->fifo) + sizeof(input_pkt));
            if (ret < 0)
                return ret;
        }

        ret = av_packet_ref(&input_pkt, avpkt);
        if (ret < 0)
            return ret;
        av_fifo_generic_write(s->fifo, &input_pkt, sizeof(input_pkt), NULL);
    }

    /*
     * MediaCodec.flush() discards both input and output buffers, thus we
     * need to delay the call to this function until the user has released or
     * renderered the frames he retains.
     *
     * After we have buffered an input packet, check if the codec is in the
     * flushing state. If it is, we need to call ff_mediacodec_dec_flush.
     *
     * ff_mediacodec_dec_flush returns 0 if the flush cannot be performed on
     * the codec (because the user retains frames). The codec stays in the
     * flushing state.
     *
     * ff_mediacodec_dec_flush returns 1 if the flush can actually be
     * performed on the codec. The codec leaves the flushing state and can
     * process again packets.
     *
     * ff_mediacodec_dec_flush returns a negative value if an error has
     * occurred.
     *
     */
    if (ff_mediacodec_dec_is_flushing(avctx, s->ctx)) {
        if (!ff_mediacodec_dec_flush(avctx, s->ctx)) {
            return avpkt->size;
        }
    }

    /* process buffered data */
    while (!*got_frame) {
        /* prepare the input data -- convert to Annex B if needed */
        if (s->filtered_pkt.size <= 0) {
            AVPacket input_pkt = { 0 };

            av_packet_unref(&s->filtered_pkt);

            /* no more data */
            if (av_fifo_size(s->fifo) < sizeof(AVPacket)) {
                return avpkt->size ? avpkt->size :
                    ff_mediacodec_dec_decode(avctx, s->ctx, frame, got_frame, avpkt);
            }

            av_fifo_generic_read(s->fifo, &input_pkt, sizeof(input_pkt), NULL);

            if (s->bsf) {
            ret = av_bsf_send_packet(s->bsf, &input_pkt);
            if (ret < 0) {
                return ret;
            }

            ret = av_bsf_receive_packet(s->bsf, &s->filtered_pkt);
            if (ret == AVERROR(EAGAIN)) {
                goto done;
            }
            } else {
                av_packet_move_ref(&s->filtered_pkt, &input_pkt);
            }

            /* {h264,hevc}_mp4toannexb are used here and do not require flushing */
            av_assert0(ret != AVERROR_EOF);

            if (ret < 0) {
                return ret;
            }
        }

        ret = mediacodec_process_data(avctx, frame, got_frame, &s->filtered_pkt);
        if (ret < 0)
            return ret;

        s->filtered_pkt.size -= ret;
        s->filtered_pkt.data += ret;
    }
done:
    return avpkt->size;
}

static void mediacodec_decode_flush(AVCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    while (av_fifo_size(s->fifo)) {
        AVPacket pkt;
        av_fifo_generic_read(s->fifo, &pkt, sizeof(pkt), NULL);
        av_packet_unref(&pkt);
    }
    av_fifo_reset(s->fifo);

    av_packet_unref(&s->filtered_pkt);

    ff_mediacodec_dec_flush(avctx, s->ctx);
}

#if CONFIG_H264_MEDIACODEC_DECODER
AVCodec ff_h264_mediacodec_decoder = {
    .name           = "h264_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif

#if CONFIG_HEVC_MEDIACODEC_DECODER
AVCodec ff_hevc_mediacodec_decoder = {
    .name           = "hevc_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("H.265 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif

#if CONFIG_MPEG4_MEDIACODEC_DECODER
AVCodec ff_mpeg4_mediacodec_decoder = {
    .name           = "mpeg4_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-4 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG4,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif

#if CONFIG_VP8_MEDIACODEC_DECODER
AVCodec ff_vp8_mediacodec_decoder = {
    .name           = "vp8_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("VP8 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP8,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif

#if CONFIG_VP9_MEDIACODEC_DECODER
AVCodec ff_vp9_mediacodec_decoder = {
    .name           = "vp9_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("VP9 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS,
};
#endif
