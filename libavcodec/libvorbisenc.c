/*
 * Copyright (c) 2002 Mark Hills <mark@pogo.org.uk>
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

#include <vorbis/vorbisenc.h>

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/fifo.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "audio_frame_queue.h"
#include "codec_internal.h"
#include "encode.h"
#include "version.h"
#include "vorbis_parser.h"


/* Number of samples the user should send in each call.
 * This value is used because it is the LCD of all possible frame sizes, so
 * an output packet will always start at the same point as one of the input
 * packets.
 */
#define LIBVORBIS_FRAME_SIZE 64

#define BUFFER_SIZE (1024 * 64)

typedef struct LibvorbisEncContext {
    AVClass *av_class;                  /**< class for AVOptions            */
    vorbis_info vi;                     /**< vorbis_info used during init   */
    vorbis_dsp_state vd;                /**< DSP state used for analysis    */
    vorbis_block vb;                    /**< vorbis_block used for analysis */
    AVFifo *pkt_fifo;                   /**< output packet buffer           */
    int eof;                            /**< end-of-file flag               */
    int dsp_initialized;                /**< vd has been initialized        */
    vorbis_comment vc;                  /**< VorbisComment info             */
    double iblock;                      /**< impulse block bias option      */
    AVVorbisParseContext *vp;           /**< parse context to get durations */
    AudioFrameQueue afq;                /**< frame queue for timestamps     */
} LibvorbisEncContext;

static const AVOption options[] = {
    { "iblock", "Sets the impulse block bias", offsetof(LibvorbisEncContext, iblock), AV_OPT_TYPE_DOUBLE, { .dbl = 0 }, -15, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const FFCodecDefault defaults[] = {
    { "b",  "0" },
    { NULL },
};

static const AVClass vorbis_class = {
    .class_name = "libvorbis",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const uint8_t vorbis_encoding_channel_layout_offsets[8][8] = {
    { 0 },
    { 0, 1 },
    { 0, 2, 1 },
    { 0, 1, 2, 3 },
    { 0, 2, 1, 3, 4 },
    { 0, 2, 1, 4, 5, 3 },
    { 0, 2, 1, 5, 6, 4, 3 },
    { 0, 2, 1, 6, 7, 4, 5, 3 },
};

static int vorbis_error_to_averror(int ov_err)
{
    switch (ov_err) {
    case OV_EFAULT: return AVERROR_BUG;
    case OV_EINVAL: return AVERROR(EINVAL);
    case OV_EIMPL:  return AVERROR(EINVAL);
    default:        return AVERROR_UNKNOWN;
    }
}

static av_cold int libvorbis_setup(vorbis_info *vi, AVCodecContext *avctx)
{
    LibvorbisEncContext *s = avctx->priv_data;
    int channels = avctx->ch_layout.nb_channels;
    double cfreq;
    int ret;

    if (avctx->flags & AV_CODEC_FLAG_QSCALE || !avctx->bit_rate) {
        /* variable bitrate
         * NOTE: we use the oggenc range of -1 to 10 for global_quality for
         *       user convenience, but libvorbis uses -0.1 to 1.0.
         */
        float q = avctx->global_quality / (float)FF_QP2LAMBDA;
        /* default to 3 if the user did not set quality or bitrate */
        if (!(avctx->flags & AV_CODEC_FLAG_QSCALE))
            q = 3.0;
        if ((ret = vorbis_encode_setup_vbr(vi, channels,
                                           avctx->sample_rate,
                                           q / 10.0)))
            goto error;
    } else {
        int minrate = avctx->rc_min_rate > 0 ? avctx->rc_min_rate : -1;
        int maxrate = avctx->rc_max_rate > 0 ? avctx->rc_max_rate : -1;

        /* average bitrate */
        if ((ret = vorbis_encode_setup_managed(vi, channels,
                                               avctx->sample_rate, maxrate,
                                               avctx->bit_rate, minrate)))
            goto error;

        /* variable bitrate by estimate, disable slow rate management */
        if (minrate == -1 && maxrate == -1)
            if ((ret = vorbis_encode_ctl(vi, OV_ECTL_RATEMANAGE2_SET, NULL)))
                goto error; /* should not happen */
    }

    /* cutoff frequency */
    if (avctx->cutoff > 0) {
        cfreq = avctx->cutoff / 1000.0;
        if ((ret = vorbis_encode_ctl(vi, OV_ECTL_LOWPASS_SET, &cfreq)))
            goto error; /* should not happen */
    }

    /* impulse block bias */
    if (s->iblock) {
        if ((ret = vorbis_encode_ctl(vi, OV_ECTL_IBLOCK_SET, &s->iblock)))
            goto error;
    }

    if ((channels == 3 &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_SURROUND)) ||
        (channels == 4 &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_2_2) &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_QUAD)) ||
        (channels == 5 &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT0) &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT0_BACK)) ||
        (channels == 6 &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1) &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1_BACK)) ||
        (channels == 7 &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_6POINT1)) ||
        (channels == 8 &&
         av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_7POINT1))) {
        if (avctx->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
            char name[32];
            av_channel_layout_describe(&avctx->ch_layout, name, sizeof(name));
            av_log(avctx, AV_LOG_ERROR, "%s not supported by Vorbis: "
                                             "output stream will have incorrect "
                                             "channel layout.\n", name);
        } else {
            av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The encoder "
                                               "will use Vorbis channel layout for "
                                               "%d channels.\n", channels);
        }
    }

    if ((ret = vorbis_encode_setup_init(vi)))
        goto error;

    return 0;
error:
    return vorbis_error_to_averror(ret);
}

/* How many bytes are needed for a buffer of length 'l' */
static int xiph_len(int l)
{
    return 1 + l / 255 + l;
}

static av_cold int libvorbis_encode_close(AVCodecContext *avctx)
{
    LibvorbisEncContext *s = avctx->priv_data;

    /* notify vorbisenc this is EOF */
    if (s->dsp_initialized)
        vorbis_analysis_wrote(&s->vd, 0);

    vorbis_block_clear(&s->vb);
    vorbis_dsp_clear(&s->vd);
    vorbis_info_clear(&s->vi);

    av_fifo_freep2(&s->pkt_fifo);
    ff_af_queue_close(&s->afq);

    av_vorbis_parse_free(&s->vp);

    return 0;
}

static av_cold int libvorbis_encode_init(AVCodecContext *avctx)
{
    LibvorbisEncContext *s = avctx->priv_data;
    ogg_packet header, header_comm, header_code;
    uint8_t *p;
    unsigned int offset;
    int ret;

    vorbis_info_init(&s->vi);
    if ((ret = libvorbis_setup(&s->vi, avctx))) {
        av_log(avctx, AV_LOG_ERROR, "encoder setup failed\n");
        goto error;
    }
    if ((ret = vorbis_analysis_init(&s->vd, &s->vi))) {
        av_log(avctx, AV_LOG_ERROR, "analysis init failed\n");
        ret = vorbis_error_to_averror(ret);
        goto error;
    }
    s->dsp_initialized = 1;
    if ((ret = vorbis_block_init(&s->vd, &s->vb))) {
        av_log(avctx, AV_LOG_ERROR, "dsp init failed\n");
        ret = vorbis_error_to_averror(ret);
        goto error;
    }

    vorbis_comment_init(&s->vc);
    if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT))
        vorbis_comment_add_tag(&s->vc, "encoder", LIBAVCODEC_IDENT);

    if ((ret = vorbis_analysis_headerout(&s->vd, &s->vc, &header, &header_comm,
                                         &header_code))) {
        ret = vorbis_error_to_averror(ret);
        goto error;
    }

    avctx->extradata_size = 1 + xiph_len(header.bytes)      +
                                xiph_len(header_comm.bytes) +
                                header_code.bytes;
    p = avctx->extradata = av_malloc(avctx->extradata_size +
                                     AV_INPUT_BUFFER_PADDING_SIZE);
    if (!p) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    p[0]    = 2;
    offset  = 1;
    offset += av_xiphlacing(&p[offset], header.bytes);
    offset += av_xiphlacing(&p[offset], header_comm.bytes);
    memcpy(&p[offset], header.packet, header.bytes);
    offset += header.bytes;
    memcpy(&p[offset], header_comm.packet, header_comm.bytes);
    offset += header_comm.bytes;
    memcpy(&p[offset], header_code.packet, header_code.bytes);
    offset += header_code.bytes;
    av_assert0(offset == avctx->extradata_size);

    s->vp = av_vorbis_parse_init(avctx->extradata, avctx->extradata_size);
    if (!s->vp) {
        av_log(avctx, AV_LOG_ERROR, "invalid extradata\n");
        return ret;
    }

    vorbis_comment_clear(&s->vc);

    avctx->frame_size = LIBVORBIS_FRAME_SIZE;
    ff_af_queue_init(avctx, &s->afq);

    s->pkt_fifo = av_fifo_alloc2(BUFFER_SIZE, 1, 0);
    if (!s->pkt_fifo) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    return 0;
error:
    libvorbis_encode_close(avctx);
    return ret;
}

static int libvorbis_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                  const AVFrame *frame, int *got_packet_ptr)
{
    LibvorbisEncContext *s = avctx->priv_data;
    ogg_packet op;
    int ret, duration;

    /* send samples to libvorbis */
    if (frame) {
        const int samples = frame->nb_samples;
        float **buffer;
        int c, channels = s->vi.channels;

        buffer = vorbis_analysis_buffer(&s->vd, samples);
        for (c = 0; c < channels; c++) {
            int co = (channels > 8) ? c :
                     vorbis_encoding_channel_layout_offsets[channels - 1][c];
            memcpy(buffer[c], frame->extended_data[co],
                   samples * sizeof(*buffer[c]));
        }
        if ((ret = vorbis_analysis_wrote(&s->vd, samples)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "error in vorbis_analysis_wrote()\n");
            return vorbis_error_to_averror(ret);
        }
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    } else {
        if (!s->eof && s->afq.frame_alloc)
            if ((ret = vorbis_analysis_wrote(&s->vd, 0)) < 0) {
                av_log(avctx, AV_LOG_ERROR, "error in vorbis_analysis_wrote()\n");
                return vorbis_error_to_averror(ret);
            }
        s->eof = 1;
    }

    /* retrieve available packets from libvorbis */
    while ((ret = vorbis_analysis_blockout(&s->vd, &s->vb)) == 1) {
        if ((ret = vorbis_analysis(&s->vb, NULL)) < 0)
            break;
        if ((ret = vorbis_bitrate_addblock(&s->vb)) < 0)
            break;

        /* add any available packets to the output packet buffer */
        while ((ret = vorbis_bitrate_flushpacket(&s->vd, &op)) == 1) {
            if (av_fifo_can_write(s->pkt_fifo) < sizeof(ogg_packet) + op.bytes) {
                av_log(avctx, AV_LOG_ERROR, "packet buffer is too small\n");
                return AVERROR_BUG;
            }
            av_fifo_write(s->pkt_fifo, &op, sizeof(ogg_packet));
            av_fifo_write(s->pkt_fifo, op.packet, op.bytes);
        }
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "error getting available packets\n");
            break;
        }
    }
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "error getting available packets\n");
        return vorbis_error_to_averror(ret);
    }

    /* Read an available packet if possible */
    if (av_fifo_read(s->pkt_fifo, &op, sizeof(ogg_packet)) < 0)
        return 0;

    if ((ret = ff_get_encode_buffer(avctx, avpkt, op.bytes, 0)) < 0)
        return ret;
    av_fifo_read(s->pkt_fifo, avpkt->data, op.bytes);

    avpkt->pts = ff_samples_to_time_base(avctx, op.granulepos);

    duration = av_vorbis_parse_frame(s->vp, avpkt->data, avpkt->size);
    if (duration > 0) {
        /* we do not know encoder delay until we get the first packet from
         * libvorbis, so we have to update the AudioFrameQueue counts */
        if (!avctx->initial_padding && s->afq.frames) {
            avctx->initial_padding    = duration;
            av_assert0(!s->afq.remaining_delay);
            s->afq.frames->duration  += duration;
            if (s->afq.frames->pts != AV_NOPTS_VALUE)
                s->afq.frames->pts       -= duration;
            s->afq.remaining_samples += duration;
        }
        ff_af_queue_remove(&s->afq, duration, &avpkt->pts, &avpkt->duration);
    }

    *got_packet_ptr = 1;
    return 0;
}

const FFCodec ff_libvorbis_encoder = {
    .p.name         = "libvorbis",
    CODEC_LONG_NAME("libvorbis"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_VORBIS,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_SMALL_LAST_FRAME,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .priv_data_size = sizeof(LibvorbisEncContext),
    .init           = libvorbis_encode_init,
    FF_CODEC_ENCODE_CB(libvorbis_encode_frame),
    .close          = libvorbis_encode_close,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .p.priv_class   = &vorbis_class,
    .defaults       = defaults,
    .p.wrapper_name = "libvorbis",
};
