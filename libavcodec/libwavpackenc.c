/*
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

#include <wavpack/wavpack.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio_frame_queue.h"
#include "avcodec.h"
#include "internal.h"

#define WV_DEFAULT_BLOCK_SIZE 32768

typedef struct LibWavpackContext {
    const AVClass *class;
    WavpackContext *wv;
    AudioFrameQueue afq;

    AVPacket *pkt;
    int user_size;

    int got_output;
} LibWavpackContext;

static int wavpack_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_output)
{
    LibWavpackContext *s = avctx->priv_data;
    int ret;

    s->got_output = 0;
    s->pkt        = pkt;
    s->user_size  = pkt->size;

    if (frame) {
        ret = ff_af_queue_add(&s->afq, frame);
        if (ret < 0)
            return ret;

        ret = WavpackPackSamples(s->wv, (int32_t*)frame->data[0], frame->nb_samples);
        if (!ret) {
            av_log(avctx, AV_LOG_ERROR, "Error encoding a frame: %s\n",
                   WavpackGetErrorMessage(s->wv));
            return AVERROR_UNKNOWN;
        }
    }

    if (!s->got_output &&
        (!frame || frame->nb_samples < avctx->frame_size)) {
        ret = WavpackFlushSamples(s->wv);
        if (!ret) {
            av_log(avctx, AV_LOG_ERROR, "Error flushing the encoder: %s\n",
                   WavpackGetErrorMessage(s->wv));
            return AVERROR_UNKNOWN;
        }
    }

    if (s->got_output) {
        ff_af_queue_remove(&s->afq, avctx->frame_size, &pkt->pts, &pkt->duration);
        *got_output = 1;
    }

    return 0;
}

static int encode_callback(void *id, void *data, int32_t count)
{
    AVCodecContext *avctx = id;
    LibWavpackContext *s  = avctx->priv_data;
    int ret, offset = s->pkt->size;

    if (s->user_size) {
        if (s->user_size - count < s->pkt->size) {
            av_log(avctx, AV_LOG_ERROR, "Provided packet too small.\n");
            return 0;
        }
        s->pkt->size += count;
    } else {
        ret = av_grow_packet(s->pkt, count);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error allocating output packet.\n");
            return 0;
        }
    }

    memcpy(s->pkt->data + offset, data, count);

    s->got_output = 1;

    return 1;
}

static av_cold int wavpack_encode_init(AVCodecContext *avctx)
{
    LibWavpackContext *s = avctx->priv_data;
    WavpackConfig config = { 0 };
    int ret;

    s->wv = WavpackOpenFileOutput(encode_callback, avctx, NULL);
    if (!s->wv) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating the encoder.\n");
        return AVERROR(ENOMEM);
    }

    if (!avctx->frame_size)
        avctx->frame_size = WV_DEFAULT_BLOCK_SIZE;

    config.bytes_per_sample = 4;
    config.bits_per_sample  = 32;
    config.block_samples    = avctx->frame_size;
    config.channel_mask     = avctx->channel_layout;
    config.num_channels     = avctx->channels;
    config.sample_rate      = avctx->sample_rate;

    if (avctx->compression_level != FF_COMPRESSION_DEFAULT) {
        if (avctx->compression_level >= 3) {
            config.flags |= CONFIG_VERY_HIGH_FLAG;

            if      (avctx->compression_level >= 8)
                config.xmode = 6;
            else if (avctx->compression_level >= 7)
                config.xmode = 5;
            else if (avctx->compression_level >= 6)
                config.xmode = 4;
            else if (avctx->compression_level >= 5)
                config.xmode = 3;
            else if (avctx->compression_level >= 4)
                config.xmode = 2;
        } else if (avctx->compression_level >= 2)
            config.flags |= CONFIG_HIGH_FLAG;
        else if (avctx->compression_level < 1)
            config.flags |= CONFIG_FAST_FLAG;
    }

    ret = WavpackSetConfiguration(s->wv, &config, -1);
    if (!ret)
        goto fail;

    ret = WavpackPackInit(s->wv);
    if (!ret)
        goto fail;

    ff_af_queue_init(avctx, &s->afq);

    return 0;

fail:
    av_log(avctx, AV_LOG_ERROR, "Error configuring the encoder: %s.\n",
           WavpackGetErrorMessage(s->wv));
    WavpackCloseFile(s->wv);
    return AVERROR_UNKNOWN;
}

static av_cold int wavpack_encode_close(AVCodecContext *avctx)
{
    LibWavpackContext *s = avctx->priv_data;

    WavpackCloseFile(s->wv);

    ff_af_queue_close(&s->afq);

    return 0;
}

AVCodec ff_libwavpack_encoder = {
    .name           = "libwavpack",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_WAVPACK,
    .priv_data_size = sizeof(LibWavpackContext),
    .init           = wavpack_encode_init,
    .encode2        = wavpack_encode_frame,
    .close          = wavpack_encode_close,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S32,
                                                     AV_SAMPLE_FMT_NONE },
};
