/*
 * AAC encoder wrapper
 * Copyright (c) 2010 Martin Storsjo
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vo-aacenc/voAAC.h>
#include <vo-aacenc/cmnMemory.h>

#include "avcodec.h"
#include "mpeg4audio.h"

typedef struct AACContext {
    VO_AUDIO_CODECAPI codec_api;
    VO_HANDLE handle;
    VO_MEM_OPERATOR mem_operator;
    VO_CODEC_INIT_USERDATA user_data;
} AACContext;

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;
    AACENC_PARAM params = { 0 };
    int index;

    avctx->coded_frame = avcodec_alloc_frame();
    avctx->frame_size = 1024;

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
    params.adtsUsed   = !(avctx->flags & CODEC_FLAG_GLOBAL_HEADER);
    if (s->codec_api.SetParam(s->handle, VO_PID_AAC_ENCPARAM, &params)
        != VO_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set encoding parameters\n");
        return AVERROR(EINVAL);
    }

    for (index = 0; index < 16; index++)
        if (avctx->sample_rate == ff_mpeg4audio_sample_rates[index])
            break;
    if (index == 16) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample rate %d\n",
                                    avctx->sample_rate);
        return AVERROR(ENOSYS);
    }
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {
        avctx->extradata_size = 2;
        avctx->extradata      = av_mallocz(avctx->extradata_size +
                                           FF_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata)
            return AVERROR(ENOMEM);

        avctx->extradata[0] = 0x02 << 3 | index >> 1;
        avctx->extradata[1] = (index & 0x01) << 7 | avctx->channels << 3;
    }
    return 0;
}

static int aac_encode_close(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;

    s->codec_api.Uninit(s->handle);
    av_freep(&avctx->coded_frame);

    return 0;
}

static int aac_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame/*out*/,
                            int buf_size, void *data/*in*/)
{
    AACContext *s = avctx->priv_data;
    VO_CODECBUFFER input = { 0 }, output = { 0 };
    VO_AUDIO_OUTPUTINFO output_info = { { 0 } };

    input.Buffer = data;
    input.Length = 2 * avctx->channels * avctx->frame_size;
    output.Buffer = frame;
    output.Length = buf_size;

    s->codec_api.SetInputData(s->handle, &input);
    if (s->codec_api.GetOutputData(s->handle, &output, &output_info)
        != VO_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Unable to encode frame\n");
        return AVERROR(EINVAL);
    }
    return output.Length;
}

AVCodec libvo_aacenc_encoder = {
    "libvo_aacenc",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AAC,
    sizeof(AACContext),
    aac_encode_init,
    aac_encode_frame,
    aac_encode_close,
    NULL,
    .sample_fmts = (const enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("Android VisualOn AAC"),
};

