/*
 * dtsdec.c : free DTS Coherent Acoustics stream decoder.
 * Copyright (C) 2004 Benjamin Zores <ben@geexbox.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include <dts.h>

#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 18726
#define HEADER_SIZE 14

#define CONVERT_LEVEL 1
#define CONVERT_BIAS 0

typedef struct DTSContext {
    dts_state_t *state;
    uint8_t buf[BUFFER_SIZE];
    uint8_t *bufptr;
    uint8_t *bufpos;
} DTSContext;

static inline int16_t
convert(sample_t s)
{
    return s * 0x7fff;
}

static void
convert2s16_multi(sample_t *f, int16_t *s16, int flags)
{
    int i;

    switch(flags & (DTS_CHANNEL_MASK | DTS_LFE)){
    case DTS_MONO:
        for(i = 0; i < 256; i++){
            s16[5*i] = s16[5*i+1] = s16[5*i+2] = s16[5*i+3] = 0;
            s16[5*i+4] = convert(f[i]);
        }
    case DTS_CHANNEL:
    case DTS_STEREO:
    case DTS_DOLBY:
        for(i = 0; i < 256; i++){
            s16[2*i] = convert(f[i]);
            s16[2*i+1] = convert(f[i+256]);
        }
    case DTS_3F:
        for(i = 0; i < 256; i++){
            s16[5*i] = convert(f[i+256]);
            s16[5*i+1] = convert(f[i+512]);
            s16[5*i+2] = s16[5*i+3] = 0;
            s16[5*i+4] = convert(f[i]);
        }
    case DTS_2F2R:
        for(i = 0; i < 256; i++){
            s16[4*i] = convert(f[i]);
            s16[4*i+1] = convert(f[i+256]);
            s16[4*i+2] = convert(f[i+512]);
            s16[4*i+3] = convert(f[i+768]);
        }
    case DTS_3F2R:
        for(i = 0; i < 256; i++){
            s16[5*i] = convert(f[i+256]);
            s16[5*i+1] = convert(f[i+512]);
            s16[5*i+2] = convert(f[i+768]);
            s16[5*i+3] = convert(f[i+1024]);
            s16[5*i+4] = convert(f[i]);
        }
    case DTS_MONO | DTS_LFE:
        for(i = 0; i < 256; i++){
            s16[6*i] = s16[6*i+1] = s16[6*i+2] = s16[6*i+3] = 0;
            s16[6*i+4] = convert(f[i]);
            s16[6*i+5] = convert(f[i+256]);
        }
    case DTS_CHANNEL | DTS_LFE:
    case DTS_STEREO | DTS_LFE:
    case DTS_DOLBY | DTS_LFE:
        for(i = 0; i < 256; i++){
            s16[6*i] = convert(f[i]);
            s16[6*i+1] = convert(f[i+256]);
            s16[6*i+2] = s16[6*i+3] = s16[6*i+4] = 0;
            s16[6*i+5] = convert(f[i+512]);
        }
    case DTS_3F | DTS_LFE:
        for(i = 0; i < 256; i++){
            s16[6*i] = convert(f[i+256]);
            s16[6*i+1] = convert(f[i+512]);
            s16[6*i+2] = s16[6*i+3] = 0;
            s16[6*i+4] = convert(f[i]);
            s16[6*i+5] = convert(f[i+768]);
        }
    case DTS_2F2R | DTS_LFE:
        for(i = 0; i < 256; i++){
            s16[6*i] = convert(f[i]);
            s16[6*i+1] = convert(f[i+256]);
            s16[6*i+2] = convert(f[i+512]);
            s16[6*i+3] = convert(f[i+768]);
            s16[6*i+4] = 0;
            s16[6*i+5] = convert(f[i+1024]);
        }
    case DTS_3F2R | DTS_LFE:
        for(i = 0; i < 256; i++){
            s16[6*i] = convert(f[i+256]);
            s16[6*i+1] = convert(f[i+512]);
            s16[6*i+2] = convert(f[i+768]);
            s16[6*i+3] = convert(f[i+1024]);
            s16[6*i+4] = convert(f[i]);
            s16[6*i+5] = convert(f[i+1280]);
        }
    }
}

static int
channels_multi(int flags)
{
    switch(flags & (DTS_CHANNEL_MASK | DTS_LFE)){
    case DTS_CHANNEL:
    case DTS_STEREO:
    case DTS_DOLBY:
        return 2;
    case DTS_2F2R:
        return 4;
    case DTS_MONO:
    case DTS_3F:
    case DTS_3F2R:
        return 5;
    case DTS_MONO | DTS_LFE:
    case DTS_CHANNEL | DTS_LFE:
    case DTS_STEREO | DTS_LFE:
    case DTS_DOLBY | DTS_LFE:
    case DTS_3F | DTS_LFE:
    case DTS_2F2R | DTS_LFE:
    case DTS_3F2R | DTS_LFE:
        return 6;
    }

    return -1;
}

static int
dts_decode_frame(AVCodecContext * avctx, void *data, int *data_size,
                 uint8_t * buff, int buff_size)
{
    DTSContext *s = avctx->priv_data;
    uint8_t *start = buff;
    uint8_t *end = buff + buff_size;
    int16_t *out_samples = data;
    int sample_rate;
    int frame_length;
    int flags;
    int bit_rate;
    int len;
    level_t level;
    sample_t bias;
    int nblocks;
    int i;

    *data_size = 0;

    while(1) {
        int length;

        len = end - start;
        if(!len)
            break;
        if(len > s->bufpos - s->bufptr)
            len = s->bufpos - s->bufptr;
        memcpy(s->bufptr, start, len);
        s->bufptr += len;
        start += len;
        if(s->bufptr != s->bufpos)
            return start - buff;
        if(s->bufpos != s->buf + HEADER_SIZE)
            break;

        length = dts_syncinfo(s->state, s->buf, &flags, &sample_rate,
                              &bit_rate, &frame_length);
        if(!length) {
            av_log(NULL, AV_LOG_INFO, "skip\n");
            for(s->bufptr = s->buf; s->bufptr < s->buf + HEADER_SIZE - 1; s->bufptr++)
                s->bufptr[0] = s->bufptr[1];
            continue;
        }
        s->bufpos = s->buf + length;
    }

    level = CONVERT_LEVEL;
    bias = CONVERT_BIAS;

    flags |= DTS_ADJUST_LEVEL;
    if(dts_frame(s->state, s->buf, &flags, &level, bias)) {
        av_log(avctx, AV_LOG_ERROR, "dts_frame() failed\n");
        goto end;
    }

    avctx->sample_rate = sample_rate;
    avctx->channels = channels_multi(flags);
    avctx->bit_rate = bit_rate;

    nblocks = dts_blocks_num(s->state);

    for(i = 0; i < nblocks; i++) {
        if(dts_block(s->state)) {
            av_log(avctx, AV_LOG_ERROR, "dts_block() failed\n");
            goto end;
        }

        convert2s16_multi(dts_samples(s->state), out_samples, flags);

        out_samples += 256 * avctx->channels;
        *data_size += 256 * sizeof(int16_t) * avctx->channels;
    }

end:
    s->bufptr = s->buf;
    s->bufpos = s->buf + HEADER_SIZE;
    return start - buff;
}

static int
dts_decode_init(AVCodecContext * avctx)
{
    DTSContext *s = avctx->priv_data;
    s->bufptr = s->buf;
    s->bufpos = s->buf + HEADER_SIZE;
    s->state = dts_init(0);
    if(s->state == NULL)
        return -1;

    return 0;
}

static int
dts_decode_end(AVCodecContext * avctx)
{
    DTSContext *s = avctx->priv_data;
    dts_free(s->state);
    return 0;
}

AVCodec dts_decoder = {
    "dts",
    CODEC_TYPE_AUDIO,
    CODEC_ID_DTS,
    sizeof(DTSContext),
    dts_decode_init,
    NULL,
    dts_decode_end,
    dts_decode_frame,
};
