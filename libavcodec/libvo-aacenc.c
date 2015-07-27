/*
 * AAC encoder wrapper
 * Copyright (c) 2010 Martin Storsjo
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

#include <vo-aacenc/voAAC.h>
#include <vo-aacenc/cmnMemory.h>

#include "avcodec.h"
#include "audio_frame_queue.h"
#include "internal.h"
#include "mpeg4audio.h"

#define FRAME_SIZE 1024
#define ENC_DELAY  1600

typedef struct AACContext {
    VO_AUDIO_CODECAPI codec_api;
    VO_HANDLE handle;
    VO_MEM_OPERATOR mem_operator;
    VO_CODEC_INIT_USERDATA user_data;
    VO_PBYTE end_buffer;
    AudioFrameQueue afq;
    int last_frame;
    int last_samples;
} AACContext;


static int aac_encode_close(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;

    s->codec_api.Uninit(s->handle);
    av_freep(&avctx->extradata);
    ff_af_queue_close(&s->afq);
    av_freep(&s->end_buffer);

    return 0;
}

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;
    AACENC_PARAM params = { 0 };
    int index, ret;

    avctx->frame_size = FRAME_SIZE;
    avctx->initial_padding = ENC_DELAY;
    s->last_frame     = 2;
    ff_af_queue_init(avctx, &s->afq);

    s->end_buffer = av_mallocz_array(avctx->channels, avctx->frame_size * 2);
    if (!s->end_buffer) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    voGetAACEncAPI(&s->codec_api);

    s->mem_operator.Alloc = cmnMemAlloc;
    s->mem_operator.Copy = cmnMemCopy;
    s->mem_operator.Free = cmnMemFree;
    s->mem_operator.Set = cmnMemSet;
    s->mem_operator.Check = cmnMemCheck;
    s->user_data.memflag = VO_IMF_USERMEMOPERATOR;
    s->user_data.memData = &s->mem_operator;
    s->codec_api.Init(&s->handle, VO_AUDIO_CodingAAC, &s->user_data);

    params.sampleRate = avctx->sample_rate;
    params.bitRate    = avctx->bit_rate;
    params.nChannels  = avctx->channels;
    params.adtsUsed   = !(avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER);
    if (s->codec_api.SetParam(s->handle, VO_PID_AAC_ENCPARAM, &params)
        != VO_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set encoding parameters\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    for (index = 0; index < 16; index++)
        if (avctx->sample_rate == avpriv_mpeg4audio_sample_rates[index])
            break;
    if (index == 16) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample rate %d\n",
                                    avctx->sample_rate);
        ret = AVERROR(ENOSYS);
        goto error;
    }
    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        avctx->extradata_size = 2;
        avctx->extradata      = av_mallocz(avctx->extradata_size +
                                           AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            ret = AVERROR(ENOMEM);
            goto error;
        }

        avctx->extradata[0] = 0x02 << 3 | index >> 1;
        avctx->extradata[1] = (index & 0x01) << 7 | avctx->channels << 3;
    }
    return 0;
error:
    aac_encode_close(avctx);
    return ret;
}

static int aac_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    AACContext *s = avctx->priv_data;
    VO_CODECBUFFER input = { 0 }, output = { 0 };
    VO_AUDIO_OUTPUTINFO output_info = { { 0 } };
    VO_PBYTE samples;
    int ret;

    /* handle end-of-stream small frame and flushing */
    if (!frame) {
        if (s->last_frame <= 0)
            return 0;
        if (s->last_samples > 0 && s->last_samples < ENC_DELAY - FRAME_SIZE) {
            s->last_samples = 0;
            s->last_frame--;
        }
        s->last_frame--;
        memset(s->end_buffer, 0, 2 * avctx->channels * avctx->frame_size);
        samples = s->end_buffer;
    } else {
        if (frame->nb_samples < avctx->frame_size) {
            s->last_samples = frame->nb_samples;
            memcpy(s->end_buffer, frame->data[0], 2 * avctx->channels * frame->nb_samples);
            samples = s->end_buffer;
        } else {
            samples = (VO_PBYTE)frame->data[0];
        }
        /* add current frame to the queue */
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    }

    if ((ret = ff_alloc_packet2(avctx, avpkt, FFMAX(8192, 768 * avctx->channels), 0)) < 0)
        return ret;

    input.Buffer  = samples;
    input.Length  = 2 * avctx->channels * avctx->frame_size;
    output.Buffer = avpkt->data;
    output.Length = avpkt->size;

    s->codec_api.SetInputData(s->handle, &input);
    if (s->codec_api.GetOutputData(s->handle, &output, &output_info)
        != VO_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to encode frame\n");
        return AVERROR(EINVAL);
    }

    /* Get the next frame pts/duration */
    ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);

    avpkt->size = output.Length;
    *got_packet_ptr = 1;
    return 0;
}

/* duplicated from avpriv_mpeg4audio_sample_rates to avoid shared build
 * failures */
static const int mpeg4audio_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

AVCodec ff_libvo_aacenc_encoder = {
    .name           = "libvo_aacenc",
    .long_name      = NULL_IF_CONFIG_SMALL("Android VisualOn AAC (Advanced Audio Coding)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AAC,
    .priv_data_size = sizeof(AACContext),
    .init           = aac_encode_init,
    .encode2        = aac_encode_frame,
    .close          = aac_encode_close,
    .supported_samplerates = mpeg4audio_sample_rates,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_DELAY,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
};
